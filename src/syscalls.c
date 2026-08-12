#include "syscalls.h"
#include "framebuffer.h"
#include "gdt.h"
#include "memory/vmm.h"
#include "process/process.h"
#include "terminal/terminal.h"

#define USER_MIN 0x0000000000400000ULL
#define USER_MAX 0x0000000040000000ULL

static int user_range_valid(uint64_t address, uint64_t size) {
    if (size == 0)
        return 1;

    if (address < USER_MIN)
        return 0;

    if (address >= USER_MAX)
        return 0;

    if (size > USER_MAX - address)
        return 0;

    return 1;
}

void syscall_init(void) {
    terminal_print("Syscall initialized.\n");
    fb_present();
}

void syscall_handler(interrupt_frame_t *frame) {
    if (!frame)
        return;

    switch (frame->rax) {
    case SYS_WRITE: {
        const char *buffer = (const char *)(uintptr_t)frame->rdi;
        uint64_t length = frame->rsi;

        if (!user_range_valid((uint64_t)(uintptr_t)buffer, length)) {
            frame->rax = (uint64_t)-1;
            return;
        }

        for (uint64_t i = 0; i < length; i++)
            terminal_putchar(buffer[i]);

        fb_present();

        frame->rax = length;
        break;
    }

    case SYS_EXIT: {
        process_exit_from_syscall(frame);
        break;
    }

    default:
        frame->rax = (uint64_t)-1;
        break;
    }
}