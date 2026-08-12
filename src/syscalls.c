#include "syscalls.h"
#include "terminal/terminal.h"
#include "framebuffer.h"

void syscall_init(void)
{
    terminal_print("Syscall initialized.\n");
    fb_present();
}

void syscall_handler(interrupt_frame_t *frame)
{
    if (!frame)
        return;

    switch (frame->rax) {

    case SYS_WRITE:
        terminal_putchar((char)frame->rdi);
        fb_present();
        frame->rax = 0;
        break;

    case SYS_EXIT:
        terminal_print("System process exit\n");
        fb_present();

        for (;;)
            __asm__ volatile("cli; hlt");

    default:
        frame->rax = (uint64_t)-1;
        break;
    }
}