#pragma once

#include <stddef.h>
#include <stdint.h>

#define RIRU_MAGIC0 'R'
#define RIRU_MAGIC1 'I'
#define RIRU_MAGIC2 'R'
#define RIRU_MAGIC3 'U'

#define RIRU_VERSION 1

#define RIRU_ARCH_X86_64 1

#define RIRU_TYPE_EXECUTABLE 1
#define RIRU_TYPE_SHARED 2

#define RIRU_FLAG_EXEC 0x01
#define RIRU_FLAG_WRITE 0x02
#define RIRU_FLAG_READ 0x04

typedef struct {
    uint8_t magic[4];

    uint16_t version;
    uint16_t architecture;
    uint16_t type;
    uint16_t flags;

    uint64_t entry;

    uint64_t code_offset;
    uint64_t code_vaddr;
    uint64_t code_size;

    uint64_t rodata_offset;
    uint64_t rodata_vaddr;
    uint64_t rodata_size;

    uint64_t data_offset;
    uint64_t data_vaddr;
    uint64_t data_size;

    uint64_t bss_size;

    uint64_t stack_size;

    uint64_t image_size;

} riru_header_t;

typedef struct {
    const riru_header_t *header;

    const uint8_t *code;
    const uint8_t *rodata;
    const uint8_t *data;

    uint64_t code_vaddr;
    uint64_t rodata_vaddr;
    uint64_t data_vaddr;

    uint64_t entry;

} riru_image_t;

int riru_validate(const void *file, size_t size);

int riru_parse(const void *file, size_t size, riru_image_t *image);