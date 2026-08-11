#include "drivers/keyboard/keyboard.h"
#include "drivers/mouse/mouse.h"
#include "drivers/timer/timer.h"
#include "framebuffer.h"
#include "gui/gui.h"
#include "idt.h"
#include "memory/kmalloc.h"
#include "memory/memory.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "system/system.h"
#include "terminal/shell.h"
#include "terminal/terminal.h"

#define TEST_PAGES 16

/* The GUI real deal*/
// void kmain(void *mb2_info) {
//     if (fb_init(mb2_info) != 0)
//         for (;;) __asm__ volatile("hlt");

//     kmalloc_init();
//     fb_enable_backbuffer();

//     system_width    = fb_width();
//     system_height   = fb_height();
//     system_timer_hz = 60;        // 60 ticks/sec = 60fps max

//     terminal_init(system_width, system_height);

//     idt_init();
//     keyboard_init();
//     mouse_init();
//     timer_init(system_timer_hz);
//     shell_init();

//     uint64_t last_tick = 0;

//     for (;;) {
//         __asm__ volatile("hlt");  // sleep until any interrupt

//         // only render when a new timer tick has fired
//         if (timer_ticks == last_tick)
//             continue;
//         last_tick = timer_ticks;

//         mouse_tick();
//         gui_update();
//         gui_draw();
//     }
// }

// void kmain(void *mb2_info)
// {
//     if (fb_init(mb2_info) != 0)
//     {
//         for (;;)
//             __asm__ volatile("hlt");
//     }

//     kmalloc_init();

//     system_width    = fb_width();
//     system_height   = fb_height();
//     system_timer_hz = 60;

//     terminal_init(system_width, system_height);
//     terminal_print("TinyOS 64-bit\n");
//     terminal_print("-------------------------\n");
//     terminal_print("Kernel initialized.\n");

//     idt_init();
//     keyboard_init();
//     mouse_init();
//     timer_init(system_timer_hz);

//     shell_init();

//     terminal_print("Interrupts initialized.\n");
//     terminal_print("Keyboard initialized.\n");
//     terminal_print("Mouse initialized.\n");
//     terminal_print("Timer initialized.\n");
//     terminal_print("-------------------------\n");

//     for (;;)
//     {
//         __asm__ volatile("hlt");
//     }
// }

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

void kmain(void *mb2_info)
{
    if (fb_init(mb2_info) != 0)
    {
        for (;;)
            __asm__ volatile("hlt");
    }

    system_width = fb_width();
    system_height = fb_height();
    system_timer_hz = 60;

    terminal_init(system_width, system_height);
    memory_detect(mb2_info);
    pmm_init(mb2_info);
    kmalloc_init();
    vmm_init();
    vmm_prepare_kernel_space();
    terminal_print("Switching address space...\n");
    vmm_switch_kernel_space();
    terminal_print("CR3 switch successful.\n");

    for (;;)
        __asm__ volatile("hlt");
}