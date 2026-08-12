#include "gdt.h"

#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdtr_t;

typedef struct __attribute__((packed)) {
    uint32_t reserved0;

    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;

    uint64_t reserved1;

    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint64_t reserved2;

    uint16_t reserved3;
    uint16_t iomap_base;
} tss_t;

static uint64_t gdt[7] __attribute__((aligned(8)));

static tss_t tss __attribute__((aligned(16)));

static uint8_t kernel_stack[16384] __attribute__((aligned(16)));

static uint64_t kernel_stack_top = (uint64_t)(uintptr_t)(kernel_stack + sizeof(kernel_stack));

static void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access,
                          uint8_t granularity) {
    gdt[index] = 0;

    gdt[index] |= (uint64_t)(limit & 0xFFFF);

    gdt[index] |= (uint64_t)(base & 0xFFFFFF) << 16;

    gdt[index] |= (uint64_t)access << 40;

    gdt[index] |= (uint64_t)((limit >> 16) & 0x0F) << 48;

    gdt[index] |= (uint64_t)(granularity & 0x0F) << 52;

    gdt[index] |= (uint64_t)((base >> 24) & 0xFF) << 56;
}

static void gdt_set_tss(void) {
    uint64_t base = (uint64_t)(uintptr_t)&tss;

    uint64_t limit = sizeof(tss) - 1;

    gdt[5] = 0;
    gdt[6] = 0;

    gdt[5] |= limit & 0xFFFF;

    gdt[5] |= (base & 0xFFFFFF) << 16;

    gdt[5] |= 0x89ULL << 40;

    gdt[5] |= ((limit >> 16) & 0x0F) << 48;

    gdt[5] |= ((base >> 24) & 0xFF) << 56;

    gdt[6] = (base >> 32) & 0xFFFFFFFF;
}

void gdt_init(void) {
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xC);

    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC);

    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xC);

    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xC);

    tss.rsp0 = kernel_stack_top;
    tss.rsp1 = 0;
    tss.rsp2 = 0;

    tss.ist1 = 0;
    tss.ist2 = 0;
    tss.ist3 = 0;
    tss.ist4 = 0;
    tss.ist5 = 0;
    tss.ist6 = 0;
    tss.ist7 = 0;

    tss.iomap_base = sizeof(tss);

    gdt_set_tss();

    gdtr_t gdtr;

    gdtr.limit = sizeof(gdt) - 1;

    gdtr.base = (uint64_t)(uintptr_t)&gdt;

    /*
     * Load our new GDT first.
     */
    __asm__ volatile("lgdt %0" : : "m"(gdtr) : "memory");

    /*
     * Reload CS.
     */
    __asm__ volatile("pushq $0x08\n"
                     "lea 1f(%%rip), %%rax\n"
                     "pushq %%rax\n"
                     "lretq\n"
                     "1:\n"
                     :
                     :
                     : "rax", "memory");


    __asm__ volatile("mov $0x10, %%ax\n"
                     "mov %%ax, %%ds\n"
                     "mov %%ax, %%es\n"
                     "mov %%ax, %%ss\n"
                     :
                     :
                     : "rax", "memory");

    __asm__ volatile("ltr %0" : : "r"((uint16_t)GDT_TSS) : "memory");
}

void tss_set_rsp0(uint64_t rsp0) { tss.rsp0 = rsp0; }

uint64_t gdt_kernel_stack(void) { return kernel_stack_top; }