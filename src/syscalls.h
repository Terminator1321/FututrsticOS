#pragma once

#include <stdint.h>
#include "idt.h"

#define SYS_EXIT  1
#define SYS_WRITE 2

void syscall_init(void);
void syscall_handler(interrupt_frame_t *frame);