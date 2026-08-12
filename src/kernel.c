#include "framebuffer.h"
#include "terminal/shell.h"
#include "terminal/terminal.h"

#include "memory/kmalloc.h"
#include "memory/memory.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "system/system.h"

#include "drivers/keyboard/keyboard.h"
#include "drivers/timer/timer.h"
#include "fs/fs.h"
#include "gdt.h"
#include "idt.h"
#include "syscalls.h"

#include <stdint.h>

void kmain(void *mb2_info) {
    if (fb_init(mb2_info) != 0) {
        for (;;)
            __asm__ volatile("cli; hlt");
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

    terminal_print("VMM initialized.\n");

    vmm_prepare_kernel_space();

    terminal_print("Kernel address space prepared.\n");

    fb_enable_backbuffer();

    terminal_print("Backbuffer initialized.\n");

    if (vmm_map_kernel_memory() != 0) {
        terminal_print("VMM: kernel memory mapping failed.\n");
        fb_present();

        for (;;)
            __asm__ volatile("cli; hlt");
    }

    terminal_print("Kernel memory mapped.\n");

    vmm_switch_kernel_space();

    terminal_print("CR3 switched successfully.\n");

    gdt_init();

    terminal_print("GDT initialized.\n");

    idt_init();

    terminal_print("IDT initialized.\n");

    keyboard_init();

    terminal_print("Keyboard initialized.\n");

    timer_init(system_timer_hz);

    terminal_print("Timer initialized.\n");

    syscall_init();

    terminal_print("Syscall initialized.\n");

    terminal_print("Mounting filesystem...\n");

    if (fs_mount() == 0) {
        terminal_print("Filesystem mount failed.\n");
        fb_present();

        for (;;)
            __asm__ volatile("cli; hlt");
    }

    terminal_print("Filesystem mounted.\n");

    shell_init();

    fb_present();

    __asm__ volatile("sti");

    for (;;) {
        __asm__ volatile("hlt");
    }
}