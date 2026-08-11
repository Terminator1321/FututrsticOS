#pragma once

#include <stdint.h>

void memory_detect(void *mb2_info);

uint64_t memory_kernel_start(void);
uint64_t memory_kernel_end(void);