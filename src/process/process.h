#pragma once

#include <stdint.h>
#include "../idt.h"

typedef enum {
    PROCESS_UNUSED = 0,
    PROCESS_RUNNING,
    PROCESS_EXITED
} process_state_t;

typedef struct {
    uint64_t pid;
    process_state_t state;
    uint64_t entry;
    uint64_t stack;
    uint64_t cr3;
    int exit_code;

    // Full saved register/CPU state for this process. On creation this is
    // built as the process's starting state (RIP=entry, etc). Every time
    // the scheduler preempts this process, its live state is copied back
    // in here so it can be resumed exactly where it left off.
    interrupt_frame_t frame;
} process_t;

// How many process slots exist. Raise this later if a scheduler needs more
// concurrently-alive processes; for now it just bounds the table.
#define MAX_PROCESSES 4

void process_init(void);

// Creates a new process (builds its initial register state, marks it
// RUNNING) without entering it - the scheduler enters it on the next timer
// tick. Returns the process's slot index (>= 0) on success, or -1 if the
// table is full.
int process_create(uint64_t entry, uint64_t stack, uint64_t cr3);

void process_exit_from_syscall(interrupt_frame_t *frame);
process_t *process_current(void);
process_t *process_by_pid(uint64_t pid);
void process_exit_return(void);
int process_exec(const char *name);

// Called from the timer IRQ. Saves whatever was interrupted (if it was a
// RUNNING process), then round-robins to the next RUNNING process and
// rewrites *frame so the ISR epilogue's iretq lands there instead. If no
// process is RUNNING, frame is left untouched and control just falls back
// into the kernel's idle loop.
void scheduler_tick(interrupt_frame_t *frame);

uint64_t process_get_busy_ticks(void);