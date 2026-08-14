#include "framebuffer.h"
#include "terminal/shell.h"
#include "terminal/terminal.h"

#include "memory/kmalloc.h"
#include "memory/memory.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "system/system.h"

#include "drivers/keyboard/keyboard.h"
#include "drivers/mouse/mouse.h"
#include "drivers/timer/timer.h"
#include "fs/fs.h"
#include "gdt.h"
#include "gui/gui.h"
#include "gui/wallpaper.h"
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

    memory_detect(mb2_info);

    pmm_init(mb2_info);
    kmalloc_init();

    vmm_init();
    vmm_prepare_kernel_space();

    fb_enable_backbuffer();

    if (vmm_map_kernel_memory() != 0) {
        terminal_print("VMM: kernel memory mapping failed.\n");

        for (;;)
            __asm__ volatile("cli; hlt");
    }

    vmm_switch_kernel_space();

    gdt_init();
    idt_init();
    keyboard_init();
    mouse_init();
    timer_init(100);
    syscall_init();

    if (fs_mount() == 0) {
        terminal_print("Filesystem mount failed.\n");
        terminal_print("(No valid NANOFS2 superblock found on disk 0 -\n");
        terminal_print(" make sure disk.img is attached, e.g. via\n");
        terminal_print(" 'qemu-system-x86_64 ... -drive file=disk.img,format=raw,if=ide'.)\n");
        __asm__ volatile("sti");

        for (;;)
            __asm__ volatile("hlt");
    }

    shell_init();

    wallpaper_load();

    gui_init();

    __asm__ volatile("sti");

    for (;;) {
        mouse_tick();
        gui_update();
        gui_draw();

        __asm__ volatile("hlt");
    }
}