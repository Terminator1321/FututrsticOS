#include "pmm.h"
#include "../terminal/terminal.h"
#include "../framebuffer.h"

#define MULTIBOOT_TAG_TYPE_END 0
#define MULTIBOOT_TAG_TYPE_MMAP 6
#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MAX_PHYSICAL_MEMORY (4ULL * 1024 * 1024 * 1024)
#define MAX_PAGES (MAX_PHYSICAL_MEMORY / PAGE_SIZE)
#define BITMAP_SIZE (MAX_PAGES / 8)

extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];

static uint8_t page_bitmap[BITMAP_SIZE];
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;


static void page_mark_used(uint64_t page) { page_bitmap[page / 8] |= (uint8_t)(1 << (page % 8)); }

static void page_mark_free(uint64_t page) { page_bitmap[page / 8] &= (uint8_t)~(1 << (page % 8)); }

static int page_is_used(uint64_t page) {
    return page_bitmap[page / 8] & (uint8_t)(1 << (page % 8));
}


static void mark_range_free(uint64_t base, uint64_t length) {
    uint64_t start = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t end = (base + length) & ~(PAGE_SIZE - 1);

    if (start >= MAX_PHYSICAL_MEMORY)
        return;

    if (end > MAX_PHYSICAL_MEMORY)
        end = MAX_PHYSICAL_MEMORY;

    for (uint64_t address = start; address < end; address += PAGE_SIZE) {
        uint64_t page = address / PAGE_SIZE;
        if (page_is_used(page)) {
            page_mark_free(page);
            free_pages++;
        }
    }
}

static void mark_range_used(uint64_t base, uint64_t length) {
    uint64_t start = base & ~(PAGE_SIZE - 1);
    uint64_t end = (base + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (start >= MAX_PHYSICAL_MEMORY)
        return;

    if (end > MAX_PHYSICAL_MEMORY)
        end = MAX_PHYSICAL_MEMORY;

    for (uint64_t address = start; address < end; address += PAGE_SIZE) {
        uint64_t page = address / PAGE_SIZE;

        if (!page_is_used(page)) {
            page_mark_used(page);
            free_pages--;
        }
    }
}

static void print_decimal(uint64_t value)
{
    char buffer[21];
    int i = 20;
    buffer[i] = '\0';

    if (value == 0)
    {
        terminal_print("0");
        return;
    }

    while (value > 0)
    {
        buffer[--i] = '0' + (value % 10);
        value /= 10;
    }
    terminal_print(&buffer[i]);
}

void pmm_init(void *mb2_info)
{
    for (uint64_t i = 0; i < BITMAP_SIZE; i++)
        page_bitmap[i] = 0xFF;

    total_pages = MAX_PAGES;
    free_pages = 0;

    uint8_t *info = (uint8_t *)mb2_info;
    uint32_t total_size = *(uint32_t *)info;
    uint8_t *ptr = info + 8;
    uint8_t *end = info + total_size;

    while (ptr < end)
    {
        uint32_t type = *(uint32_t *)(ptr + 0);
        uint32_t size = *(uint32_t *)(ptr + 4);

        if (type == MULTIBOOT_TAG_TYPE_END)
            break;

        if (type == MULTIBOOT_TAG_TYPE_MMAP)
        {
            uint32_t entry_size = *(uint32_t *)(ptr + 8);
            uint8_t *entry_ptr = ptr + 16;
            uint8_t *entry_end = ptr + size;

            while (entry_ptr < entry_end)
            {
                uint64_t base = *(uint64_t *)(entry_ptr + 0);
                uint64_t length = *(uint64_t *)(entry_ptr + 8);
                uint32_t region_type = *(uint32_t *)(entry_ptr + 16);

                if (region_type == MULTIBOOT_MEMORY_AVAILABLE)
                    mark_range_free(base, length);

                entry_ptr += entry_size;
            }
        }

        ptr += (size + 7) & ~7;
    }

    mark_range_used(0, 0x100000);
    mark_range_used((uint64_t)(uintptr_t)__kernel_start,(uint64_t)((uintptr_t)__kernel_end -(uintptr_t)__kernel_start));
    mark_range_used((uint64_t)(uintptr_t)mb2_info,*(uint32_t *)mb2_info);
    mark_range_used(fb_physical_address(),fb_memory_size());
    terminal_print("PMM initialized.\n");
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t page = 0; page < total_pages; page++) {
        if (!page_is_used(page)) {
            page_mark_used(page);

            free_pages--;

            return page * PAGE_SIZE;
        }
    }

    return 0;
}

void pmm_free_page(uint64_t address) {
    if (address == 0)
        return;

    if (address >= MAX_PHYSICAL_MEMORY)
        return;

    if (address & (PAGE_SIZE - 1))
        return;

    uint64_t page = address / PAGE_SIZE;

    if (page_is_used(page)) {
        page_mark_free(page);
        free_pages++;
    }
}

uint64_t pmm_get_total_pages(void) { return total_pages; }

uint64_t pmm_get_free_pages(void) { return free_pages; }


void pmm_print_stats(void)
{
    terminal_print("\nPMM Statistics\n");
    terminal_print("Total pages: ");
    print_decimal(total_pages);
    terminal_print("\n");
    terminal_print("Free pages:  ");
    print_decimal(free_pages);
    terminal_print("\n");
    terminal_print("Free RAM:    ");
    print_decimal(
        (free_pages * PAGE_SIZE) /
        (1024 * 1024)
    );
    terminal_print(" MB\n");

}

uint64_t pmm_alloc_pages(uint64_t count)
{
    if (count == 0)
        return 0;

    uint64_t consecutive = 0;
    uint64_t start_page = 0;

    for (uint64_t page = 0;
         page < total_pages;
         page++)
    {
        if (!page_is_used(page))
        {
            if (consecutive == 0)
                start_page = page;

            consecutive++;

            if (consecutive == count)
            {
                for (uint64_t i = 0; i < count; i++)
                {
                    page_mark_used(start_page + i);
                }

                free_pages -= count;
                return start_page * PAGE_SIZE;
            }
        }
        else
        {
            consecutive = 0;
        }
    }

    return 0;
}