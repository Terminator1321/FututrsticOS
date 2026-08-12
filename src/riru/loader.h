#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t entry;
    uint64_t image_base;
    uint64_t image_end;
    uint64_t pages;
} riru_load_result_t;

int riru_load(const void *file, size_t size, riru_load_result_t *result);
uint64_t riru_get_user_cr3(void);
int riru_load_file(const char *name, riru_load_result_t *result);