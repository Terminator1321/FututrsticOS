#include "drivers/keyboard/keyboard.h"
#include "drivers/mouse/mouse.h"
#include "drivers/timer/timer.h"
#include "elf/elf.h"
#include "elf/loader.h"
#include "framebuffer.h"
#include "gdt.h"
#include "gui/gui.h"
#include "idt.h"
#include "memory/kmalloc.h"
#include "memory/memory.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "riru/loader.h"
#include "system/system.h"
#include "terminal/shell.h"
#include "terminal/terminal.h"
#include "riru/user.h"
static void print_hex64(uint64_t value) {
    const char *hex = "0123456789ABCDEF";
    char buffer[19];

    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[18] = '\0';

    for (int i = 0; i < 16; i++) {
        buffer[17 - i] = hex[value & 0xF];
        value >>= 4;
    }

    terminal_print(buffer);
}

static void print_uint64(uint64_t value) {
    char buffer[21];
    int i = 20;

    buffer[i] = '\0';

    if (value == 0) {
        buffer[--i] = '0';
    } else {
        while (value) {
            buffer[--i] = '0' + (value % 10);
            value /= 10;
        }
    }

    terminal_print(&buffer[i]);
}

void kmain(void *mb2_info) {
    if (fb_init(mb2_info) != 0) {
        for (;;)
            __asm__ volatile("hlt");
    }

    system_width = fb_width();
    system_height = fb_height();
    system_timer_hz = 60;

    terminal_init(system_width, system_height);

    terminal_print("TinyOS 64-bit\n");
    terminal_print("-------------------------\n");
    terminal_print("Kernel initialized.\n");

    memory_detect(mb2_info);
    pmm_init(mb2_info);
    kmalloc_init();

    terminal_print("Memory initialized.\n");

    vmm_init();
    vmm_prepare_kernel_space();

    terminal_print("Kernel address space prepared.\n");

    fb_enable_backbuffer();

    terminal_print("Backbuffer initialized.\n");
    fb_present();

    if (vmm_map_kernel_memory() != 0) {
        terminal_print("VMM: kernel memory mapping failed.\n");
        fb_present();

        for (;;)
            __asm__ volatile("hlt");
    }

    terminal_print("Kernel memory mapped.\n");
    fb_present();

    vmm_switch_kernel_space();

    terminal_print("CR3 switched successfully.\n");
    fb_present();

    fb_clear(COLOR_DARK);
    fb_present();

    gdt_init();

    terminal_print("GDT initialized.\n");
    fb_present();

    idt_init();

    terminal_print("IDT initialized.\n");
    fb_present();

    keyboard_init();

    terminal_print("Keyboard initialized.\n");
    fb_present();

    mouse_init();

    terminal_print("Mouse initialized.\n");
    fb_present();

    timer_init(system_timer_hz);

    terminal_print("Timer initialized.\n");
    fb_present();

    terminal_print("Mounting filesystem...\n");
    fb_present();

    if (fs_mount() == 0) {
        terminal_print("Filesystem mount failed.\n");
        fb_present();

        for (;;)
            __asm__ volatile("hlt");
    }

    terminal_print("Filesystem mounted.\n");
    fb_present();

    shell_init();

    terminal_print("Shell initialized.\n");
    terminal_print("-------------------------\n");
    fb_present();

    terminal_print("Testing RIRU loader...\n");
    fb_present();

    riru_load_result_t result;

    int riru_result = riru_load_file("hello.riru", &result);

    if (riru_result == 0) {
        terminal_print("RIRU LOAD OK\n");

        terminal_print("Entry: ");
        print_hex64(result.entry);

        terminal_print("\nPages: ");
        print_uint64(result.pages);

        terminal_print("\n");

        terminal_print("Entering Ring 3...\n");
        fb_present();

        vmm_switch_address_space(riru_get_user_cr3());

        riru_enter_user(result.entry, 0x000000003FFFF000ULL);
    } else {
        terminal_print("RIRU LOAD FAILED: ");

        print_uint64((uint64_t)(-riru_result));

        terminal_print("\n");
    }

    terminal_print("-------------------------\n");
    fb_present();

    uint64_t last_tick = 0;

    for (;;) {
        __asm__ volatile("hlt");

        if (timer_ticks == last_tick)
            continue;

        last_tick = timer_ticks;

        mouse_tick();
        gui_update();
        gui_draw();
    }
}