#include "kmalloc.h"
#include <stdint.h>

#define KERNEL_HEAP_SIZE (16 * 1024 * 1024)

static uint8_t kernel_heap[KERNEL_HEAP_SIZE];
static size_t heap_offset = 0;

void kmalloc_init(void) {
    heap_offset = 0;
}

void *kmalloc(size_t size) {
    if (heap_offset + size > KERNEL_HEAP_SIZE)
        return 0;

    void *ptr = &kernel_heap[heap_offset];

    heap_offset += (size + 7) & ~7;

    return ptr;
}

size_t kmalloc_used(void) {
    return heap_offset;
}

size_t kmalloc_total(void) {
    return KERNEL_HEAP_SIZE;
}