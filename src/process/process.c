#include "process.h"
#include "../framebuffer.h"
#include "../gdt.h"
#include "../memory/vmm.h"
#include "../terminal/terminal.h"
#include "../riru/loader.h"
#include "../riru/user.h"
#include "../terminal/shell.h"

static process_t current_process;
static uint64_t next_pid = 1;

void process_init(void) {
    current_process.pid = 0;
    current_process.state = PROCESS_UNUSED;
    current_process.entry = 0;
    current_process.stack = 0;
    current_process.cr3 = 0;
    current_process.exit_code = 0;
}

int process_create(uint64_t entry, uint64_t stack, uint64_t cr3) {
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

int process_exec(const char *name) {
    if (!name || !*name)
        return -1;

    if (current_process.state == PROCESS_RUNNING)
        return -2;

    riru_load_result_t result;

    terminal_print("RIRU: loading ");
    terminal_print(name);
    terminal_print("...\n");
    fb_present();

    int r = riru_load_file(name, &result);

    if (r != 0) {
        terminal_print("RIRU: load failed\n");
        fb_present();
        return -3;
    }

    uint64_t cr3 = riru_get_user_cr3();
    uint64_t stack = 0x000000003FFFF000ULL;

    if (!cr3) {
        terminal_print("RIRU: invalid user CR3\n");
        fb_present();
        return -4;
    }

    if (process_create(result.entry, stack, cr3) != 0) {
        terminal_print("Process creation failed\n");
        fb_present();
        return -5;
    }

    terminal_print("Starting process PID ");
    print_uint(current_process.pid);
    terminal_print("\n");
    fb_present();

    // riru_load() leaves us on the KERNEL address space (it switches back
    // after copying the image). The kernel's own identity map has this
    // low-memory range as supervisor-only, so jumping to ring 3 without
    // switching CR3 back to the user address space first causes an
    // instant #PF(present, user) the moment the CPU fetches the first
    // instruction at the entry point.
    vmm_switch_address_space(cr3);

    riru_enter_user(result.entry, stack);

    return -6;
}

process_t *process_current(void) {
    return &current_process;
}

void process_exit_from_syscall(interrupt_frame_t *frame) {
    if (!frame)
        return;

    current_process.exit_code = (int)frame->rdi;
    current_process.state = PROCESS_EXITED;

    uint64_t cr3 = current_process.cr3;

    vmm_switch_kernel_space();

    if (cr3)
        vmm_destroy_user_space(cr3);

    current_process.cr3 = 0;

    frame->rip = (uint64_t)(uintptr_t)process_exit_return;
    frame->cs = 0x08;
    frame->rflags |= 0x200;
    frame->rsp = gdt_kernel_stack();
    frame->ss = 0x10;
}

void process_exit_return(void) {
    terminal_print("Process exited.\n");
    terminal_print("> ");
    fb_present();

    for (;;)
        __asm__ volatile("hlt");
}