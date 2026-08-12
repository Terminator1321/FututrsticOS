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
#include "io.h"
void kmain(void *mb2_info)
{
    __asm__ volatile("cli");

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

    terminal_print("Memory initialized.\n");

    vmm_init();
    vmm_prepare_kernel_space();

    terminal_print("Kernel space prepared.\n");

    fb_enable_backbuffer();

    terminal_print("Backbuffer allocated.\n");
    fb_present();

    if (vmm_map_kernel_memory() != 0)
    {
        terminal_print("VMM mapping failed.\n");
        fb_present();

        for (;;)
            __asm__ volatile("hlt");
    }

    vmm_switch_kernel_space();

    fb_clear(COLOR_DARK);
    fb_present();

    terminal_print("CR3 OK\n");
    fb_present();

    idt_init();

    terminal_print("IDT OK\n");
    fb_present();

    keyboard_init();

    terminal_print("KEYBOARD OK\n");
    fb_present();

    mouse_init();

    terminal_print("MOUSE OK\n");
    fb_present();

    timer_init(system_timer_hz);

    terminal_print("TIMER OK\n");
    fb_present();

    uint8_t mask = inb(0x21);
    outb(0x21, mask | 0x01);

    terminal_print("TIMER MASKED\n");
    fb_present();

    __asm__ volatile("sti");

    terminal_print("INTERRUPTS ENABLED\n");
    fb_present();

    for (;;)
    {
        __asm__ volatile("hlt");
    }
}