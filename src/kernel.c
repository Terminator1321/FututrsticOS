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

void kmain(void *mb2_info)
{
    if (fb_init(mb2_info) != 0)
    {
        for (;;)
            __asm__ volatile("hlt");
    }

    system_width    = fb_width();
    system_height   = fb_height();
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

    if (vmm_map_kernel_memory() != 0)
    {
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

    shell_init();

    terminal_print("Shell initialized.\n");
    terminal_print("-------------------------\n");
    fb_present();

    uint64_t last_tick = 0;

    for (;;)
    {
        __asm__ volatile("hlt");

        if (timer_ticks == last_tick)
            continue;

        last_tick = timer_ticks;

        mouse_tick();
        gui_update();
        gui_draw();
    }
}