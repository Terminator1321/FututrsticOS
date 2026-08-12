#include "loader.h"

#include "../fs/fs.h"
#include "../memory/kmalloc.h"
#include "../memory/pmm.h"
#include "../memory/vmm.h"

#include "riru.h"
#include "user.h"

#include "../framebuffer.h"
#include "../terminal/terminal.h"

#include <stddef.h>
#include <stdint.h>

#define RIRU_USER_MIN 0x0000000000400000ULL
#define RIRU_USER_MAX 0x0000000040000000ULL
#define RIRU_USER_STACK_TOP 0x000000003FFFF000ULL
#define RIRU_USER_STACK_SIZE (16ULL * 1024)

static uint64_t riru_user_cr3 = 0;

static uint64_t align_down(uint64_t value) { return value & ~(PAGE_SIZE - 1); }

static uint64_t align_up(uint64_t value) { return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); }

static int valid_range(uint64_t address, uint64_t size) {
    if (size == 0)
        return 1;

    if (address < RIRU_USER_MIN)
        return 0;

    if (address >= RIRU_USER_MAX)
        return 0;

    if (size > RIRU_USER_MAX - address)
        return 0;

    return 1;
}

static int map_range(uint64_t cr3, uint64_t address, uint64_t size, uint64_t flags) {
    if (size == 0)
        return 0;

    uint64_t start = align_down(address);
    uint64_t end = align_up(address + size);

    for (uint64_t va = start; va < end; va += PAGE_SIZE) {
        if (vmm_get_physical_in(cr3, va) != 0)
            continue;

        uint64_t physical = pmm_alloc_page();

        if (physical == 0)
            return -1;

        int result = vmm_map_user_page(cr3, va, physical, flags | PAGE_USER);

        if (result != 0) {
            pmm_free_page(physical);
            return -2;
        }
    }

    return 0;
}

static int map_user_stack(uint64_t cr3) {
    uint64_t stack_bottom = RIRU_USER_STACK_TOP - RIRU_USER_STACK_SIZE;

    for (uint64_t va = stack_bottom; va < RIRU_USER_STACK_TOP; va += PAGE_SIZE) {
        if (vmm_get_physical_in(cr3, va) != 0)
            continue;

        uint64_t physical = pmm_alloc_page();

        if (physical == 0)
            return -1;

        int result = vmm_map_user_page(cr3, va, physical, PAGE_USER | PAGE_WRITABLE);

        if (result != 0) {
            pmm_free_page(physical);
            return -2;
        }
    }

    return 0;
}

static void zero_memory(uint8_t *address, uint64_t size) {
    for (uint64_t i = 0; i < size; i++)
        address[i] = 0;
}

int riru_load(const void *file, size_t size, riru_load_result_t *result) {
    if (!file || !result)
        return -1;

    riru_image_t image;

    int parse_result = riru_parse(file, size, &image);

    if (parse_result != 0)
        return parse_result;

    const riru_header_t *header = image.header;

    uint64_t image_base = UINT64_MAX;
    uint64_t image_end = 0;

    if (header->code_size) {
        if (!valid_range(header->code_vaddr, header->code_size))
            return -2;

        uint64_t start = align_down(header->code_vaddr);

        uint64_t end = align_up(header->code_vaddr + header->code_size);

        image_base = start;
        image_end = end;
    }

    if (header->rodata_size) {
        if (!valid_range(header->rodata_vaddr, header->rodata_size))
            return -3;

        uint64_t start = align_down(header->rodata_vaddr);

        uint64_t end = align_up(header->rodata_vaddr + header->rodata_size);

        if (start < image_base)
            image_base = start;

        if (end > image_end)
            image_end = end;
    }

    if (header->data_size) {
        if (!valid_range(header->data_vaddr, header->data_size))
            return -4;

        uint64_t start = align_down(header->data_vaddr);

        uint64_t end = align_up(header->data_vaddr + header->data_size);

        if (start < image_base)
            image_base = start;

        if (end > image_end)
            image_end = end;
    }

    if (image_base == UINT64_MAX)
        return -5;

    terminal_print("RIRU: creating user address space...\n");
    fb_present();

    riru_user_cr3 = vmm_create_user_space();

    if (riru_user_cr3 == 0) {
        terminal_print("RIRU: failed to create user address space\n");
        fb_present();
        return -6;
    }

    terminal_print("RIRU: user address space created\n");
    fb_present();

    terminal_print("RIRU: mapping code...\n");
    fb_present();

    if (map_range(riru_user_cr3, header->code_vaddr, header->code_size, PAGE_WRITABLE) != 0) {
        terminal_print("RIRU: code mapping failed\n");
        fb_present();
        return -7;
    }

    terminal_print("RIRU: mapping rodata...\n");
    fb_present();

    if (map_range(riru_user_cr3, header->rodata_vaddr, header->rodata_size, PAGE_WRITABLE) != 0) {
        terminal_print("RIRU: rodata mapping failed\n");
        fb_present();
        return -8;
    }

    terminal_print("RIRU: mapping data...\n");
    fb_present();

    if (map_range(riru_user_cr3, header->data_vaddr, header->data_size, PAGE_WRITABLE) != 0) {
        terminal_print("RIRU: data mapping failed\n");
        fb_present();
        return -9;
    }

    uint64_t bss_start = 0;

    if (header->bss_size) {
        if (header->data_size) {
            bss_start = header->data_vaddr + header->data_size;
        } else if (header->code_size) {
            bss_start = header->code_vaddr + header->code_size;
        } else {
            bss_start = image_end;
        }

        if (!valid_range(bss_start, header->bss_size))
            return -10;

        terminal_print("RIRU: mapping BSS...\n");
        fb_present();

        if (map_range(riru_user_cr3, bss_start, header->bss_size, PAGE_WRITABLE) != 0) {
            terminal_print("RIRU: BSS mapping failed\n");
            fb_present();
            return -11;
        }
    }

    terminal_print("RIRU: mapping user stack...\n");
    fb_present();

    if (map_user_stack(riru_user_cr3) != 0) {
        terminal_print("RIRU: user stack mapping failed\n");
        fb_present();
        return -12;
    }

    terminal_print("RIRU: user stack mapped\n");
    fb_present();

    terminal_print("RIRU: switching to user address space...\n");
    fb_present();

    vmm_switch_address_space(riru_user_cr3);

    if (header->code_size) {
        uint8_t *destination = (uint8_t *)(uintptr_t)header->code_vaddr;

        for (uint64_t i = 0; i < header->code_size; i++) {
            destination[i] = image.code[i];
        }
    }

    if (header->rodata_size) {
        uint8_t *destination = (uint8_t *)(uintptr_t)header->rodata_vaddr;

        for (uint64_t i = 0; i < header->rodata_size; i++) {
            destination[i] = image.rodata[i];
        }
    }

    if (header->data_size) {
        uint8_t *destination = (uint8_t *)(uintptr_t)header->data_vaddr;

        for (uint64_t i = 0; i < header->data_size; i++) {
            destination[i] = image.data[i];
        }
    }

    if (header->bss_size) {
        zero_memory((uint8_t *)(uintptr_t)bss_start, header->bss_size);
    }

    terminal_print("RIRU: returning to kernel address space...\n");
    fb_present();

    vmm_switch_kernel_space();

    if (header->entry < image_base || header->entry >= image_end) {
        terminal_print("RIRU: invalid entry point\n");
        fb_present();
        return -13;
    }

    result->entry = header->entry;
    result->image_base = image_base;
    result->image_end = image_end;
    result->pages = (image_end - image_base) / PAGE_SIZE;

    terminal_print("RIRU: load successful\n");
    fb_present();

    return 0;
}

uint64_t riru_get_user_cr3(void) { return riru_user_cr3; }

int riru_load_file(const char *name, riru_load_result_t *result) {
    terminal_print("RIRU: fs_stat...\n");
    fb_present();

    fs_inode_t inode;

    int stat_result = fs_stat(name, &inode);

    terminal_print("RIRU: fs_stat returned\n");
    fb_present();

    if (stat_result != 0) {
        terminal_print("RIRU: fs_stat FAILED\n");
        fb_present();
        return -2;
    }

    terminal_print("RIRU: fs_stat OK\n");
    fb_present();

    terminal_print("RIRU: allocating buffer...\n");
    fb_present();

    uint8_t *buffer = kmalloc(inode.size);

    terminal_print("RIRU: kmalloc returned\n");
    fb_present();

    if (!buffer) {
        terminal_print("RIRU: kmalloc returned NULL\n");
        fb_present();
        return -3;
    }

    terminal_print("RIRU: fs_read...\n");
    fb_present();

    int read_result = fs_read(name, buffer, inode.size);

    terminal_print("RIRU: fs_read returned\n");
    fb_present();

    if (read_result < 0) {
        terminal_print("RIRU: fs_read FAILED\n");
        fb_present();

        kfree(buffer);

        return -4;
    }

    terminal_print("RIRU: fs_read OK\n");
    fb_present();

    terminal_print("RIRU: loading image...\n");
    fb_present();

    int result_code = riru_load(buffer, (size_t)read_result, result);

    terminal_print("RIRU: loader returned\n");
    fb_present();

    kfree(buffer);

    return result_code;
}