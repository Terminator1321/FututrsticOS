#include "riru.h"

static int riru_check_magic(const riru_header_t *header) {
    return header->magic[0] == RIRU_MAGIC0 && header->magic[1] == RIRU_MAGIC1 &&
           header->magic[2] == RIRU_MAGIC2 && header->magic[3] == RIRU_MAGIC3;
}

static int range_valid(uint64_t offset, uint64_t size, size_t file_size) {
    if (offset > file_size)
        return 0;

    if (size > file_size - offset)
        return 0;

    return 1;
}

int riru_validate(const void *file, size_t size) {
    if (!file)
        return -1;

    if (size < sizeof(riru_header_t))
        return -2;

    const riru_header_t *header = (const riru_header_t *)file;

    if (!riru_check_magic(header))
        return -3;

    if (header->version != RIRU_VERSION)
        return -4;

    if (header->architecture != RIRU_ARCH_X86_64)
        return -5;

    if (header->type != RIRU_TYPE_EXECUTABLE)
        return -6;

    if (header->image_size > size)
        return -7;

    if (header->image_size < sizeof(riru_header_t))
        return -8;

    if (!range_valid(header->code_offset, header->code_size, header->image_size))
        return -9;

    if (!range_valid(header->rodata_offset, header->rodata_size, header->image_size))
        return -10;

    if (!range_valid(header->data_offset, header->data_size, header->image_size))
        return -11;

    if (header->entry == 0)
        return -12;

    if (header->code_size && header->code_vaddr == 0)
        return -13;

    if (header->rodata_size && header->rodata_vaddr == 0)
        return -14;

    if (header->data_size && header->data_vaddr == 0)
        return -15;

    return 0;
}

int riru_parse(const void *file, size_t size, riru_image_t *image) {
    if (!image)
        return -1;

    int result = riru_validate(file, size);

    if (result != 0)
        return result;

    const uint8_t *base = (const uint8_t *)file;

    const riru_header_t *header = (const riru_header_t *)file;

    image->header = header;

    image->code = header->code_size ? base + header->code_offset : 0;

    image->rodata = header->rodata_size ? base + header->rodata_offset : 0;

    image->data = header->data_size ? base + header->data_offset : 0;

    image->code_vaddr = header->code_vaddr;

    image->rodata_vaddr = header->rodata_vaddr;

    image->data_vaddr = header->data_vaddr;

    image->entry = header->entry;

    return 0;
}