#include "process.h"
#include "../framebuffer.h"
#include "../gdt.h"
#include "../libc/string.h"
#include "../memory/vmm.h"
#include "../terminal/terminal.h"
#include "../riru/loader.h"
#include "../riru/user.h"
#include "../terminal/shell.h"
#include "../drivers/mouse/mouse.h"
#include "../gui/gui.h"

// The process table. Each slot is either UNUSED (never used, or reclaimed),
// RUNNING, or EXITED (finished but its slot/pid/exit_code are kept around
// until something overwrites the slot - so exit codes can still be read
// after the fact instead of vanishing the instant the process dies).
static process_t process_table[MAX_PROCESSES];

// Index into process_table of whichever process's registers/CR3 are
// actually loaded on the CPU RIGHT NOW - only the scheduler ever changes
// this. -1 means the CPU is running kernel/idle code, not any process.
static int current_index = -1;

static uint64_t next_pid = 1;
static uint64_t busy_ticks = 0;

void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].pid = 0;
        process_table[i].state = PROCESS_UNUSED;
        process_table[i].entry = 0;
        process_table[i].stack = 0;
        process_table[i].cr3 = 0;
        process_table[i].exit_code = 0;
        memset(&process_table[i].frame, 0, sizeof(process_table[i].frame));
    }

    current_index = -1;
}

// Find a slot that isn't holding a live process. EXITED slots count as
// free - once you've read an exit code you don't need, you can reuse it.
static int find_free_slot(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_UNUSED || process_table[i].state == PROCESS_EXITED)
            return i;
    }

    return -1;
}

int process_create(uint64_t entry, uint64_t stack, uint64_t cr3) {
    int slot = find_free_slot();

    if (slot < 0)
        return -1; // table full - every slot is RUNNING

    process_t *proc = &process_table[slot];

    proc->pid = next_pid++;
    proc->state = PROCESS_RUNNING;
    proc->entry = entry;
    proc->stack = stack;
    proc->cr3 = cr3;
    proc->exit_code = 0;

    // Build the register state this process starts execution with. The
    // scheduler will copy this straight into a trap frame on its first
    // timer tick, so it must look exactly like a frame the CPU pushed for
    // a ring-3 -> ring-0 interrupt: RIP/CS/RFLAGS/RSP/SS plus all the GPRs
    // isr_common saves. A brand-new process has no meaningful register
    // values yet, so everything but the five control fields is zero.
    memset(&proc->frame, 0, sizeof(proc->frame));

    proc->frame.rip = entry;
    proc->frame.cs = GDT_USER_CODE | 3;
    proc->frame.rflags = 0x202; // reserved bit 1 + IF (interrupts enabled)
    proc->frame.rsp = stack;
    proc->frame.ss = GDT_USER_DATA | 3;

    return slot;
}

int process_exec(const char *name) {
    if (!name || !*name)
        return -1;

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

    int slot = process_create(result.entry, stack, cr3);

    if (slot < 0) {
        terminal_print("Process creation failed (table full)\n");
        fb_present();
        return -5;
    }

    terminal_print("Queued process PID ");
    print_uint(process_table[slot].pid);
    terminal_print(" (starts on next timer tick)\n");
    fb_present();

    // No riru_enter_user() here anymore - process_create() only sets the
    // slot's initial saved frame to RUNNING. scheduler_tick() (called from
    // the timer IRQ) is what actually copies that frame into a real trap
    // frame and iretq's into it. This lets more than one process exist at
    // once instead of this function itself jumping straight into ring 3
    // and never returning.
    return 0;
}

void process_exit_from_syscall(interrupt_frame_t *frame) {
    if (!frame || current_index < 0)
        return;

    process_t *proc = &process_table[current_index];

    proc->exit_code = (int)frame->rdi;
    proc->state = PROCESS_EXITED;

    uint64_t cr3 = proc->cr3;

    vmm_switch_kernel_space();

    if (cr3)
        vmm_destroy_user_space(cr3);

    proc->cr3 = 0;

    // The CPU is about to go back to running plain kernel code (not any
    // process), so nothing is "current" until the scheduler picks a new
    // process on the next timer tick.
    current_index = -1;

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

    for (;;) {
        mouse_tick();
        gui_update();
        gui_draw();

        __asm__ volatile("hlt");
    }
}

void scheduler_tick(interrupt_frame_t *frame) {
    if (!frame)
        return;

    // If a process was actually executing when this tick interrupted it,
    // save its live register state back into its slot so it can be
    // resumed later exactly where it was preempted. If current_index is
    // -1, the CPU was just running kernel/idle code - nothing to save.
    if (current_index >= 0 && process_table[current_index].state == PROCESS_RUNNING)
        process_table[current_index].frame = *frame;

    // Round-robin: search forward from the process after the one that was
    // just interrupted (or from the start, if nothing was running) for the
    // next RUNNING slot.
    int start = (current_index >= 0) ? current_index + 1 : 0;
    int next = -1;

    for (int offset = 0; offset < MAX_PROCESSES; offset++) {
        int i = (start + offset) % MAX_PROCESSES;

        if (process_table[i].state == PROCESS_RUNNING) {
            next = i;
            break;
        }
    }

    if (next < 0) {
        // Nothing runnable. Make sure we're on the kernel's own address
        // space and leave *frame completely alone - the ISR epilogue will
        // then just iretq back into whatever kernel code (the idle hlt
        // loop) was actually interrupted.
        if (current_index != -1)
            vmm_switch_kernel_space();

        current_index = -1;
        return;
    }

    // Load the chosen process's saved state into the real trap frame and
    // switch to its address space - the ISR epilogue (isr.s) will pop
    // these registers and iretq, landing directly in that process.
    *frame = process_table[next].frame;

    vmm_switch_address_space(process_table[next].cr3);

    current_index = next;
    busy_ticks++;
}

uint64_t process_get_busy_ticks(void) {
    return busy_ticks;
}

process_t *process_current(void) {
    if (current_index < 0)
        return 0;

    return &process_table[current_index];
}

process_t *process_by_pid(uint64_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROCESS_UNUSED && process_table[i].pid == pid)
            return &process_table[i];
    }

    return 0;
}