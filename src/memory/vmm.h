#pragma once

#include <stdint.h>

#define PAGE_SIZE 4096ULL

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER (1ULL << 2)
#define PAGE_NX (1ULL << 63)

void vmm_init(void);

int vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);

int vmm_map_range(uint64_t virtual_address, uint64_t physical_address, uint64_t size, uint64_t flags);

void vmm_unmap_page(uint64_t virtual_address);

uint64_t vmm_get_physical(uint64_t virtual_address);

void vmm_prepare_kernel_space(void);
void vmm_switch_kernel_space(void);
int vmm_map_kernel_memory(void);

int vmm_map_kernel_range(uint64_t virtual_address, uint64_t physical_address, uint64_t size, uint64_t flags);