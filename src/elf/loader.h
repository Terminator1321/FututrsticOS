#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct
{
    uint64_t entry;
    uint64_t image_base;
    uint64_t image_end;
    uint64_t pages;
} elf_load_result_t;

int elf_load(
    const void *file,
    size_t size,
    elf_load_result_t *result
);