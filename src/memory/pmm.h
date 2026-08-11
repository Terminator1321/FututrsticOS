#pragma once

#include <stdint.h>

#define PAGE_SIZE 4096ULL

void pmm_init(void *mb2_info);

uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t address);

uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_free_pages(void);


void pmm_print_stats(void);

uint64_t pmm_alloc_pages(uint64_t count);