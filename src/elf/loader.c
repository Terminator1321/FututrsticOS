#include "loader.h"
#include "elf.h"

#include "../memory/pmm.h"
#include "../memory/vmm.h"

#include <stddef.h>
#include <stdint.h>

#define USER_MIN_ADDRESS 0x0000000000400000ULL
#define USER_MAX_ADDRESS 0x0000000040000000ULL

static uint64_t align_down(uint64_t value) { return value & ~(PAGE_SIZE - 1); }

static uint64_t align_up(uint64_t value) { return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); }

static int range_valid(uint64_t start, uint64_t size) {
    if (size == 0)
        return 1;

    if (start < USER_MIN_ADDRESS)
        return 0;

    if (start > USER_MAX_ADDRESS)
        return 0;

    if (size > USER_MAX_ADDRESS - start)
        return 0;

    return 1;
}

int elf_load(const void *file, size_t size, elf_load_result_t *result) {
    if (!file || !result)
        return -1;

    int validation = elf_validate(file, size);

    if (validation != 0)
        return validation;

    elf64_header_t elf;

    if (elf_get_header(file, size, &elf) != 0)
        return -2;

    uint64_t image_base = UINT64_MAX;
    uint64_t image_end = 0;
    uint64_t total_pages = 0;

    for (uint16_t i = 0; i < elf.e_phnum; i++) {
        elf64_program_header_t ph;

        if (elf_get_program_header(file, size, i, &ph) != 0)
            return -3;

        if (!elf_is_loadable(&ph))
            continue;

        if (ph.p_memsz < ph.p_filesz)
            return -4;

        if (ph.p_offset > size)
            return -5;

        if (ph.p_filesz > size - ph.p_offset)
            return -6;

        if (!range_valid(ph.p_vaddr, ph.p_memsz))
            return -7;

        uint64_t segment_start = align_down(ph.p_vaddr);
        uint64_t segment_end = align_up(ph.p_vaddr + ph.p_memsz);

        if (segment_start < image_base)
            image_base = segment_start;

        if (segment_end > image_end)
            image_end = segment_end;
    }

    if (image_base == UINT64_MAX)
        return -8;

    total_pages = (image_end - image_base) / PAGE_SIZE;

    if (total_pages == 0)
        return -9;

    for (uint16_t i = 0; i < elf.e_phnum; i++) {
        elf64_program_header_t ph;

        if (elf_get_program_header(file, size, i, &ph) != 0)
            return -10;

        if (!elf_is_loadable(&ph))
            continue;

        uint64_t segment_start = align_down(ph.p_vaddr);
        uint64_t segment_end = align_up(ph.p_vaddr + ph.p_memsz);

        uint64_t pages = (segment_end - segment_start) / PAGE_SIZE;

        for (uint64_t page = 0; page < pages; page++) {
            uint64_t virtual_address = segment_start + page * PAGE_SIZE;

            if (vmm_get_physical(virtual_address) != 0)
                continue;

            uint64_t physical = pmm_alloc_page();

            if (physical == 0)
                return -11;

            int map_result = vmm_map_page(virtual_address, physical, PAGE_WRITABLE | PAGE_USER);

            if (map_result != 0) {
                pmm_free_page(physical);
                return -12;
            }
        }

        uint8_t *destination = (uint8_t *)(uintptr_t)ph.p_vaddr;

        const uint8_t *source = (const uint8_t *)file + ph.p_offset;

        for (uint64_t j = 0; j < ph.p_filesz; j++)
            destination[j] = source[j];

        for (uint64_t j = ph.p_filesz; j < ph.p_memsz; j++)
            destination[j] = 0;
    }

    if (elf.e_entry < image_base || elf.e_entry >= image_end)
        return -13;

    result->entry = elf.e_entry;
    result->image_base = image_base;
    result->image_end = image_end;
    result->pages = total_pages;

    return 0;
}