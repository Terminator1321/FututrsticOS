#include "process.h"
#include "../terminal/terminal.h"
#include "../framebuffer.h"
#include "../memory/vmm.h"
#include "../gdt.h"

static process_t current_process;
static uint64_t next_pid = 1;

void process_init(void)
{
    current_process.pid = 0;
    current_process.state = PROCESS_UNUSED;
    current_process.entry = 0;
    current_process.stack = 0;
    current_process.cr3 = 0;
    current_process.exit_code = 0;
}

int process_create(uint64_t entry, uint64_t stack, uint64_t cr3)
{
    if (current_process.state == PROCESS_RUNNING)
        return -1;

    current_process.pid = next_pid++;
    current_process.state = PROCESS_RUNNING;
    current_process.entry = entry;
    current_process.stack = stack;
    current_process.cr3 = cr3;
    current_process.exit_code = 0;

    return 0;
}

process_t *process_current(void)
{
    return &current_process;
}

void process_exit_from_syscall(interrupt_frame_t *frame)
{
    if (!frame)
        return;

    current_process.exit_code = (int)frame->rdi;
    current_process.state = PROCESS_EXITED;

    vmm_switch_kernel_space();

    frame->rip = (uint64_t)(uintptr_t)process_exit_return;
    frame->cs = 0x08;
    frame->rflags |= 0x200;
    frame->rsp = gdt_kernel_stack();
    frame->ss = 0x10;
}

void process_exit_return(void)
{
    terminal_print("Process exited.\n");
    terminal_print("> ");
    fb_present();

    for (;;)
        __asm__ volatile("hlt");
}