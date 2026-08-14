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

static void vmm_print_hex(uint64_t value)
{
    const char hex[] = "0123456789ABCDEF";
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

        pml4[pml4_index] =
            (uint64_t)(uintptr_t)pdpt | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        pdpt = physical_to_table(pml4[pml4_index]);

        // x86 requires PAGE_USER set at EVERY level of the walk for a ring-3
        // access to succeed, not just on the final page. This entry may
        // already exist from the kernel's own (non-user) identity map -
        // e.g. RIRU user pages share the same low-memory tables the kernel
        // built for itself - so merge the bit in rather than leaving a
        // supervisor-only directory that silently blocks every page under
        // it, no matter how the leaf PTE is flagged.
        pml4[pml4_index] |= (flags & PAGE_USER);
    }

    if (!(pdpt[pdpt_index] & PAGE_PRESENT)) {
        pd = allocate_table();

        if (!pd)
            return -3;

        pdpt[pdpt_index] =
            (uint64_t)(uintptr_t)pd | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        pd = physical_to_table(pdpt[pdpt_index]);

        pdpt[pdpt_index] |= (flags & PAGE_USER);
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

        pd[pd_index] |= (flags & PAGE_USER);
    }

    if (pt[pt_index] & PAGE_PRESENT) {
        uint64_t existing = pt[pt_index] & PAGE_MASK;

        uint64_t requested = physical_address & PAGE_MASK;

        if (existing == requested) {
            /*
             * Same physical page already mapped here (this happens for
             * RIRU user pages: they live below 4 GB, the same range the
             * kernel's blanket identity map already covers as
             * supervisor-only). Merge in any extra permission bits
             * (PAGE_USER, PAGE_WRITABLE) being requested instead of
             * silently keeping the old, more restrictive entry - otherwise
             * the mapping call "succeeds" but ring 3 still can't touch
             * the page (a page fault the moment the user program is
             * entered).
             */
            pt[pt_index] |= (flags & (PAGE_USER | PAGE_WRITABLE));
            return 0;
        }

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
    uint64_t draw = fb_draw_buffer_address();
    uint64_t display = fb_display_buffer_address();

    uint64_t fb_phys = fb_physical_address();
    uint64_t fb_size = fb_memory_size();

    uint64_t buffer_size = fb_buffer_size();

    if (!draw || !display)
        return -1;

    if (!fb_phys || !fb_size)
        return -2;

    int result;

    result = vmm_map_kernel_range(draw, draw, buffer_size, PAGE_WRITABLE);

    if (result != 0)
        return result;

    result = vmm_map_kernel_range(display, display, buffer_size, PAGE_WRITABLE);

    if (result != 0)
        return result;

    result = vmm_map_kernel_range(fb_phys, fb_phys, fb_size, PAGE_WRITABLE);

    if (result != 0)
        return result;

    return 0;
}

uint64_t vmm_create_user_space(void) {
    uint64_t *user_pml4 = allocate_table();

    if (!user_pml4)
        return 0;

    /*
     * Copy the kernel's mappings into the new address space.
     *
     * This kernel is loaded at 0x100000 and identity-maps physical
     * memory 1:1, so everything the kernel needs (its own code/data,
     * the IDT handlers, the framebuffer, and the RIRU user program
     * range at 0x400000-0x40000000) all falls inside PML4 index 0
     * (any address below 512 GB). Copying indices 256-511 (the
     * higher-half convention) copies nothing useful here - it left
     * user_pml4 with an *empty* index 0, so the instant CR3 was
     * switched to this address space, the very next instruction
     * fetch (still in low kernel memory) had no mapping at all and
     * page-faulted; since the fault handler itself lives in that
     * same now-unmapped memory, that turned into a double fault ->
     * triple fault -> the machine reset. Copying index 0 keeps the
     * kernel (and its identity map) resident in every address space.
     */
    user_pml4[0] = kernel_pml4[0];

    for (int i = 256; i < ENTRIES; i++)
        user_pml4[i] = kernel_pml4[i];

    if (map_page_in(user_pml4, 0xB8000, 0xB8000, PAGE_WRITABLE) != 0)
        return 0;

    uint64_t framebuffer_address = fb_physical_address();

    uint64_t framebuffer_size = fb_memory_size();

    if (framebuffer_address && framebuffer_size) {
        uint64_t fb_start = framebuffer_address & ~(PAGE_SIZE - 1);

        uint64_t fb_end = framebuffer_address + framebuffer_size;

        fb_end = (fb_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        for (uint64_t address = fb_start; address < fb_end; address += PAGE_SIZE) {
            if (map_page_in(user_pml4, address, address, PAGE_WRITABLE) != 0)
                return 0;
        }
    }

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

int vmm_destroy_user_space(uint64_t cr3) {
    if (cr3 == 0)
        return -1;

    vmm_switch_kernel_space();

    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & PAGE_MASK);

    for (uint64_t va = 0x00400000ULL; va < 0x40000000ULL; va += PAGE_SIZE) {

        uint64_t physical = get_physical_from(pml4, va);

        if (physical != 0) {
            uint64_t page = physical & PAGE_MASK;
            pmm_free_page(page);
        }
    }

    pmm_free_page((uint64_t)(uintptr_t)pml4);

    return 0;
}

void vmm_debug_user_page(uint64_t cr3, uint64_t virtual_address)
{
    uint64_t *pml4 = (uint64_t *)(uintptr_t)(cr3 & PAGE_MASK);

    uint64_t pml4_i = (virtual_address >> 39) & 0x1FF;
    uint64_t pdpt_i = (virtual_address >> 30) & 0x1FF;
    uint64_t pd_i   = (virtual_address >> 21) & 0x1FF;
    uint64_t pt_i   = (virtual_address >> 12) & 0x1FF;

    uint64_t pml4e = pml4[pml4_i];

    terminal_print("VMM DEBUG USER PAGE\n");
    terminal_print("PML4E=");
    vmm_print_hex(pml4e);
    terminal_print("\n");

    if (!(pml4e & PAGE_PRESENT))
        return;

    uint64_t *pdpt = physical_to_table(pml4e);
    uint64_t pdpte = pdpt[pdpt_i];

    terminal_print("PDPTE=");
    vmm_print_hex(pdpte);
    terminal_print("\n");

    if (!(pdpte & PAGE_PRESENT))
        return;

    uint64_t *pd = physical_to_table(pdpte);
    uint64_t pde = pd[pd_i];

    terminal_print("PDE=");
    vmm_print_hex(pde);
    terminal_print("\n");

    if (!(pde & PAGE_PRESENT))
        return;

    if (pde & HUGE_PAGE) {
        terminal_print("HUGE PAGE\n");
        return;
    }

    uint64_t *pt = physical_to_table(pde);
    uint64_t pte = pt[pt_i];

    terminal_print("PTE=");
    vmm_print_hex(pte);
    terminal_print("\n");

    terminal_print("FLAGS: ");

    if (pte & PAGE_PRESENT)
        terminal_print("P ");

    if (pte & PAGE_WRITABLE)
        terminal_print("W ");

    if (pte & PAGE_USER)
        terminal_print("U ");

    terminal_print("\n");
}

