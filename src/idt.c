#include "idt.h"
#include "drivers/keyboard/keyboard.h"
#include "drivers/mouse/mouse.h"
#include "drivers/timer/timer.h"
#include "framebuffer.h"
#include "pic.h"
#include "syscalls.h"
#include "terminal/terminal.h"
#include <stdint.h>

#define IDT_ENTRIES 256
#define USER_INT_GATE 0xEE
#define INT_GATE 0x8E // present | ring0 | 64-bit interrupt gate
#define CODE_SEG 0x08 // our GDT64 code entry (second slot = offset 8)

static idt_entry_t idt[IDT_ENTRIES];
static idt_ptr_t idtr;

extern void *isr_stubs[IDT_ENTRIES]; // defined in isr.s

static void set_gate(int n, void *handler) {
    uint64_t addr = (uint64_t)handler;
    idt[n].offset_low = (uint16_t)(addr & 0xFFFF);
    idt[n].selector = CODE_SEG;
    idt[n].ist = 0;
    idt[n].type_attr = INT_GATE;
    idt[n].offset_mid = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[n].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[n].reserved = 0;
}

void idt_init(void) {
    pic_init();

    for (int i = 0; i < IDT_ENTRIES; i++)
        set_gate(i, isr_stubs[i]);

    idt[0x80].type_attr = USER_INT_GATE;

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint64_t)idt;

    __asm__ volatile("lidt %0" : : "m"(idtr));
}

// exception names
static const char *exc_names[32] = {
    "Division Error",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "Bound Range",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Seg Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Exception",
    "Virtualization",
    "Control Protection",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection",
    "VMM Communication",
    "Security Exception",
    "Reserved",
};

// minimal VGA helpers (no stdlib in kernel)
static volatile unsigned short *vga = (volatile unsigned short *)0xB8000;

static void vga_puts(const char *s, int col, unsigned short color) {
    for (int i = 0; s[i]; i++)
        vga[col + i] = color | (unsigned char)s[i];
}

static void vga_puthex(uint64_t v, int col) {
    const char hex[] = "0123456789ABCDEF";
    vga[col++] = 0x0F00 | '0';
    vga[col++] = 0x0F00 | 'x';
    for (int shift = 60; shift >= 0; shift -= 4)
        vga[col++] = 0x0F00 | hex[(v >> shift) & 0xF];
}

// terminal_print only understands strings, so give the panic path its own
// tiny hex formatter (no snprintf in a freestanding kernel).
static void term_puthex64(uint64_t v) {
    const char hex[] = "0123456789ABCDEF";
    char buffer[19];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[18] = '\0';
    for (int i = 0; i < 16; i++) {
        buffer[17 - i] = hex[v & 0xF];
        v >>= 4;
    }
    terminal_print(buffer);
}

// the one C handler called by all 256 stubs
void isr_handler(interrupt_frame_t *frame)
{
    if (!frame)
        return;

    uint64_t n = frame->int_num;

    if (n == 0x80) {
        terminal_print("\nSYSCALL 80\n");
        fb_present();

        syscall_handler(frame);

        terminal_print("SYSCALL RETURNED\n");
        fb_present();

        return;
    }

    if (n < 32) {
        for (int i = 80; i < 160; i++)
            vga[i] = 0x4F00 | ' ';

        vga_puts("EXCEPTION #", 80, 0x4F00);

        vga[91] = 0x4F00 | ('0' + n / 10);
        vga[92] = 0x4F00 | ('0' + n % 10);

        vga_puts("  ", 93, 0x4F00);
        vga_puts(exc_names[n], 95, 0x4F00);

        vga_puts("  ERR=", 120, 0x4F00);
        vga_puthex(frame->error_code, 126);

        vga_puts("  RIP=", 144, 0x4F00);
        vga_puthex(frame->rip, 150);

        terminal_print("\n!! CPU EXCEPTION #");

        char digits[3] = {
            (char)('0' + n / 10),
            (char)('0' + n % 10),
            '\0'
        };

        terminal_print(digits);
        terminal_print("  ");
        terminal_print(exc_names[n]);

        terminal_print("\nERR=");
        term_puthex64(frame->error_code);

        terminal_print("  RIP=");
        term_puthex64(frame->rip);

        terminal_print("  CS=");
        term_puthex64(frame->cs);

        terminal_print("\n");
        fb_present();

        for (;;)
            __asm__ volatile("cli; hlt");
    }

    if (n >= 0x20 && n < 0x30) {
        uint8_t irq = (uint8_t)(n - 0x20);

        switch (irq) {
        case 0:
            timer_handler();
            break;

        case 1:
            keyboard_handler();
            break;

        case 12:
            vga[0] = 0x2F00 | 'M';
            mouse_handler();
            break;
        }

        pic_send_eoi(irq);
        return;
    }
}