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
} process_t;

void process_init(void);
int process_create(uint64_t entry, uint64_t stack, uint64_t cr3);
void process_exit_from_syscall(interrupt_frame_t *frame);
process_t *process_current(void);
void process_exit_return(void);
int process_exec(const char *name);