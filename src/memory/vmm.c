#include "vmm.h"
#include "../framebuffer.h"
#include "../terminal/terminal.h"
#include "memory.h"
#include "pmm.h"

#define ENTRIES 512
#define PAGE_MASK 0x000FFFFFFFFFF000ULL
#define HUGE_PAGE (1ULL << 7)

static uint64_t *current_pml4;
static uint64_t *kernel_pml4;
static uint64_t kernel_cr3;
static inline uint64_t read_cr3(void) {
    uint64_t value;

    __asm__ volatile("mov %%cr3, %0" : "=r"(value));

    return value;
}

static inline void invalidate_page(uint64_t address) {
    __asm__ volatile("invlpg (%0)" : : "r"(address) : "memory");
}

static uint64_t *physical_to_table(uint64_t address) {
    return (uint64_t *)(uintptr_t)(address & PAGE_MASK);
}

static uint64_t *allocate_table(void) {
    uint64_t physical = pmm_alloc_page();

    if (physical == 0)
        return 0;

    uint64_t *table = (uint64_t *)(uintptr_t)physical;

    for (int i = 0; i < ENTRIES; i++)
        table[i] = 0;

    return table;
}

static int map_page_in(uint64_t *pml4, uint64_t virtual_address, uint64_t physical_address,
                       uint64_t flags) {
    if (virtual_address & (PAGE_SIZE - 1))
        return -1;

    if (physical_address & (PAGE_SIZE - 1))
        return -1;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1FF;

    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;

    uint64_t pd_index = (virtual_address >> 21) & 0x1FF;

    uint64_t pt_index = (virtual_address >> 12) & 0x1FF;

    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;

    if (!(pml4[pml4_index] & PAGE_PRESENT)) {
        pdpt = allocate_table();

        if (!pdpt)
            return -2;

        pml4[pml4_index] = (uint64_t)(uintptr_t)pdpt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        pdpt = physical_to_table(pml4[pml4_index]);
    }

    if (!(pdpt[pdpt_index] & PAGE_PRESENT)) {
        pd = allocate_table();

        if (!pd)
            return -3;

        pdpt[pdpt_index] = (uint64_t)(uintptr_t)pd | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        pd = physical_to_table(pdpt[pdpt_index]);
    }

    if (pd[pd_index] & HUGE_PAGE)
        return -4;

    if (!(pd[pd_index] & PAGE_PRESENT)) {
        pt = allocate_table();

        if (!pt)
            return -5;

        pd[pd_index] = (uint64_t)(uintptr_t)pt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        pt = physical_to_table(pd[pd_index]);
    }

    if (pt[pt_index] & PAGE_PRESENT) {
        uint64_t existing = pt[pt_index] & PAGE_MASK;

        uint64_t requested = physical_address & PAGE_MASK;

        if (existing == requested)
            return 0;

        return -6;
    }

    pt[pt_index] = (physical_address & PAGE_MASK) | flags | PAGE_PRESENT;

    return 0;
}

static uint64_t get_physical_from(uint64_t *pml4, uint64_t virtual_address) {
    uint64_t pml4_index = (virtual_address >> 39) & 0x1FF;

    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;

    uint64_t pd_index = (virtual_address >> 21) & 0x1FF;

    uint64_t pt_index = (virtual_address >> 12) & 0x1FF;

    uint64_t offset = virtual_address & 0xFFF;

    if (!(pml4[pml4_index] & PAGE_PRESENT))
        return 0;

    uint64_t *pdpt = physical_to_table(pml4[pml4_index]);

    if (!(pdpt[pdpt_index] & PAGE_PRESENT))
        return 0;

    uint64_t *pd = physical_to_table(pdpt[pdpt_index]);

    if (!(pd[pd_index] & PAGE_PRESENT))
        return 0;

    if (pd[pd_index] & HUGE_PAGE) {
        uint64_t base = pd[pd_index] & 0x000FFFFFFFE00000ULL;

        return base + (virtual_address & 0x1FFFFF);
    }

    uint64_t *pt = physical_to_table(pd[pd_index]);

    if (!(pt[pt_index] & PAGE_PRESENT))
        return 0;

    return (pt[pt_index] & PAGE_MASK) + offset;
}

void vmm_init(void) {
    current_pml4 = physical_to_table(read_cr3());

    terminal_print("VMM initialized.\n");
}

int vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags) {
    int result = map_page_in(current_pml4, virtual_address, physical_address, flags);

    if (result == 0)
        invalidate_page(virtual_address);

    return result;
}

int vmm_map_range(uint64_t virtual_address, uint64_t physical_address, uint64_t size,
                  uint64_t flags) {
    if (size == 0)
        return 0;

    uint64_t v_start = virtual_address & ~(PAGE_SIZE - 1);

    uint64_t p_start = physical_address & ~(PAGE_SIZE - 1);

    uint64_t offset = virtual_address & (PAGE_SIZE - 1);

    uint64_t total = size + offset;

    uint64_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t i = 0; i < pages; i++) {
        int result = vmm_map_page(v_start + i * PAGE_SIZE, p_start + i * PAGE_SIZE, flags);

        if (result != 0)
            return result;
    }

    return 0;
}

void vmm_unmap_page(uint64_t virtual_address) {
    uint64_t pml4_index = (virtual_address >> 39) & 0x1FF;

    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;

    uint64_t pd_index = (virtual_address >> 21) & 0x1FF;

    uint64_t pt_index = (virtual_address >> 12) & 0x1FF;

    if (!(current_pml4[pml4_index] & PAGE_PRESENT))
        return;

    uint64_t *pdpt = physical_to_table(current_pml4[pml4_index]);

    if (!(pdpt[pdpt_index] & PAGE_PRESENT))
        return;

    uint64_t *pd = physical_to_table(pdpt[pdpt_index]);

    if (!(pd[pd_index] & PAGE_PRESENT))
        return;

    if (pd[pd_index] & HUGE_PAGE)
        return;

    uint64_t *pt = physical_to_table(pd[pd_index]);

    pt[pt_index] = 0;

    invalidate_page(virtual_address);
}

uint64_t vmm_get_physical(uint64_t virtual_address) {
    return get_physical_from(current_pml4, virtual_address);
}

static int vmm_identity_map_physical_memory(void) {
    const uint64_t size = 4ULL * 1024 * 1024 * 1024;

    for (uint64_t address = 0; address < size; address += PAGE_SIZE) {
        int result = map_page_in(kernel_pml4, address, address, PAGE_WRITABLE);

        if (result != 0)
            return result;
    }

    return 0;
}

void vmm_prepare_kernel_space(void) {
    kernel_pml4 = allocate_table();

    if (!kernel_pml4) {
        terminal_print("VMM: kernel PML4 allocation failed\n");

        for (;;)
            __asm__ volatile("hlt");
    }

    uint64_t start = memory_kernel_start();

    uint64_t end = memory_kernel_end();

    start &= ~(PAGE_SIZE - 1);
    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint64_t address = start; address < end; address += PAGE_SIZE) {
        int result = map_page_in(kernel_pml4, address, address, PAGE_WRITABLE);

        if (result != 0) {
            terminal_print("VMM: kernel mapping failed\n");

            for (;;)
                __asm__ volatile("hlt");
        }
    }

    uint64_t framebuffer_address = fb_physical_address();

    uint64_t framebuffer_size = fb_memory_size();

    if (framebuffer_address && framebuffer_size) {
        uint64_t fb_start = framebuffer_address & ~(PAGE_SIZE - 1);

        uint64_t fb_end = framebuffer_address + framebuffer_size;

        fb_end = (fb_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        for (uint64_t address = fb_start; address < fb_end; address += PAGE_SIZE) {
            int result = map_page_in(kernel_pml4, address, address, PAGE_WRITABLE);

            if (result != 0) {
                terminal_print("VMM: framebuffer mapping failed\n");

                for (;;)
                    __asm__ volatile("hlt");
            }
        }
    }
    // idt.c's panic/debug path writes directly to the legacy VGA text buffer
    // at 0xB8000. That address is outside [kernel_start,kernel_end) and
    // outside the framebuffer range, so without this it stays unmapped in
    // kernel_pml4 -> the very first write to it (e.g. from an exception, or
    // the IRQ12 mouse debug marker) page-faults, and because the panic
    // handler itself does the same faulting write, that turns into a
    // double fault -> triple fault -> the machine resets.
    int vga_result = map_page_in(kernel_pml4, 0xB8000, 0xB8000, PAGE_WRITABLE);
    terminal_print("VMM: mapping physical memory...\n");

    int identity_result = vmm_identity_map_physical_memory();

    if (identity_result != 0) {
        terminal_print("VMM: physical memory mapping failed\n");

        for (;;)
            __asm__ volatile("hlt");
    }

    terminal_print("VMM: physical memory mapped\n");

    if (vga_result != 0) {
        terminal_print("VMM: VGA mapping failed\n");

        for (;;)
            __asm__ volatile("hlt");
    }

    kernel_cr3 = (uint64_t)(uintptr_t)kernel_pml4;

    terminal_print("Kernel address space prepared.\n");
}

void vmm_switch_kernel_space(void) {
    current_pml4 = kernel_pml4;
    __asm__ volatile("mov %0, %%cr3" : : "r"(kernel_cr3) : "memory");
}

int vmm_map_kernel_range(uint64_t virtual_address, uint64_t physical_address, uint64_t size,
                         uint64_t flags) {
    if (size == 0)
        return 0;

    uint64_t v_start = virtual_address & ~(PAGE_SIZE - 1);

    uint64_t p_start = physical_address & ~(PAGE_SIZE - 1);

    uint64_t offset = virtual_address & (PAGE_SIZE - 1);

    uint64_t total = size + offset;

    uint64_t pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t i = 0; i < pages; i++) {
        int result =
            map_page_in(kernel_pml4, v_start + i * PAGE_SIZE, p_start + i * PAGE_SIZE, flags);

        if (result != 0)
            return result;
    }

    return 0;
}

int vmm_map_kernel_memory(void) {
    uint64_t size = fb_buffer_size();

    uint64_t draw = fb_draw_buffer_address();

    uint64_t display = fb_display_buffer_address();

    if (!draw || !display)
        return -1;

    int result = vmm_map_kernel_range(draw, draw, size, PAGE_WRITABLE);

    if (result != 0)
        return result;

    return vmm_map_kernel_range(display, display, size, PAGE_WRITABLE);
}

uint64_t vmm_create_user_space(void) {
    uint64_t *user_pml4 = allocate_table();

    if (!user_pml4)
        return 0;

    /*
     * Copy the kernel half of the address space.
     *
     * User space will use the lower half.
     * Kernel mappings remain available after switching
     * to the user address space.
     */
    for (int i = 256; i < ENTRIES; i++)
        user_pml4[i] = kernel_pml4[i];

    return (uint64_t)(uintptr_t)user_pml4;
}

int vmm_map_user_page(uint64_t cr3, uint64_t virtual_address, uint64_t physical_address,
                      uint64_t flags) {
    if (cr3 == 0)
        return -1;

    if (virtual_address >= 0x0000800000000000ULL)
        return -2;

    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & PAGE_MASK);

    int result = map_page_in(pml4, virtual_address, physical_address, flags | PAGE_USER);

    return result;
}

uint64_t vmm_get_physical_in(uint64_t cr3, uint64_t virtual_address) {
    if (cr3 == 0)
        return 0;

    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & PAGE_MASK);

    return get_physical_from(pml4, virtual_address);
}

void vmm_switch_address_space(uint64_t cr3) {
    if (cr3 == 0)
        return;

    current_pml4 = (uint64_t *)(uintptr_t)(cr3 & PAGE_MASK);

    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3 & PAGE_MASK) : "memory");
}