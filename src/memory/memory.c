#include "memory.h"
#include "../terminal/terminal.h"

#define MULTIBOOT_TAG_TYPE_END 0
#define MULTIBOOT_TAG_TYPE_MMAP 6

#define MULTIBOOT_MEMORY_AVAILABLE 1

typedef struct {
    uint32_t total_size;
    uint32_t reserved;
} multiboot_info_t;

typedef struct {
    uint32_t type;
    uint32_t size;
} multiboot_tag_t;

typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} multiboot_tag_mmap_t;

typedef struct {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} multiboot_mmap_entry_t;

static void print_hex64(uint64_t value) {
    const char *hex = "0123456789ABCDEF";

    char buffer[19];

    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[18] = '\0';

    for (int i = 0; i < 16; i++) {
        buffer[17 - i] = hex[value & 0xF];
        value >>= 4;
    }

    terminal_print(buffer);
}

static void print_decimal(uint64_t value) {
    char buffer[21];
    int i = 20;

    buffer[i] = '\0';

    if (value == 0) {
        terminal_print("0");
        return;
    }

    while (value > 0) {
        buffer[--i] = '0' + (value % 10);
        value /= 10;
    }

    terminal_print(&buffer[i]);
}

void memory_detect(void *mb2_info) {
    multiboot_info_t *info = (multiboot_info_t *)mb2_info;

    uint8_t *ptr = (uint8_t *)info + 8;

    uint8_t *end = (uint8_t *)info + info->total_size;

    uint64_t total_usable = 0;
    uint32_t region_count = 0;

    terminal_print("\n");
    terminal_print("##TinyOS Memory Map\n");

    while (ptr < end) {
        multiboot_tag_t *tag = (multiboot_tag_t *)ptr;
        if (tag->type == MULTIBOOT_TAG_TYPE_END)
            break;

        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            multiboot_tag_mmap_t *mmap = (multiboot_tag_mmap_t *)tag;

            uint8_t *entry_ptr = ptr + sizeof(multiboot_tag_mmap_t);

            uint8_t *entry_end = ptr + tag->size;

            while (entry_ptr < entry_end) {
                multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *)entry_ptr;
                terminal_print("\nRegion ");
                print_decimal(region_count++);
                terminal_print("\n");
                terminal_print("  Base:   ");
                print_hex64(entry->base_addr);
                terminal_print("\n  Length: ");
                print_hex64(entry->length);
                terminal_print("\n  Type:   ");
                if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    terminal_print("AVAILABLE");
                    total_usable += entry->length;
                } else {
                    terminal_print("RESERVED");
                }
                terminal_print("\n");
                entry_ptr += mmap->entry_size;
            }
        }

        ptr += (tag->size + 7) & ~7;
    }
    terminal_print("Usable RAM: ");
    print_decimal(total_usable / (1024 * 1024));
    terminal_print(" MB\n");

}

extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];

uint64_t memory_kernel_start(void)
{
    return (uint64_t)(uintptr_t)__kernel_start;
}

uint64_t memory_kernel_end(void)
{
    return (uint64_t)(uintptr_t)__kernel_end;
}