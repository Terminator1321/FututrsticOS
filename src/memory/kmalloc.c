#include "kmalloc.h"
#include "pmm.h"

#include <stdint.h>

typedef struct {
    uint64_t magic;
    uint64_t pages;
    uint64_t size;
} allocation_header_t;

#define KMALLOC_MAGIC 0x4B4D414C4C4F43ULL

static size_t heap_used = 0;
static size_t heap_total = 0;

void kmalloc_init(void) {
    heap_used = 0;
    heap_total = 0;
}

void *kmalloc(size_t size) {
    if (size == 0)
        return 0;

    size_t total_size = sizeof(allocation_header_t) + size;
    uint64_t pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t address = pmm_alloc_pages(pages);

    if (address == 0)
        return 0;

    allocation_header_t *header = (allocation_header_t *)(uintptr_t)address;

    header->magic = KMALLOC_MAGIC;
    header->pages = pages;
    header->size = size;
    heap_used += pages * PAGE_SIZE;
    heap_total += pages * PAGE_SIZE;
    return (void *)(header + 1);
}

void kfree(void *ptr) {
    if (!ptr)
        return;

    allocation_header_t *header = ((allocation_header_t *)ptr) - 1;

    if (header->magic != KMALLOC_MAGIC)
        return;

    uint64_t pages = header->pages;
    uint64_t address = (uint64_t)(uintptr_t)header;

    for (uint64_t i = 0; i < pages; i++)
        pmm_free_page(address + i * PAGE_SIZE);

    if (heap_used >= pages * PAGE_SIZE)
        heap_used -= pages * PAGE_SIZE;
    else
        heap_used = 0;

    header->magic = 0;
}

size_t kmalloc_used(void) { return heap_used; }
size_t kmalloc_total(void) { return heap_total; }