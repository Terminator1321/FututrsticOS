OUTPUT_FORMAT("elf64-x86-64")

ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    __kernel_start = .;

    .text : ALIGN(4096)
    {
        *(.multiboot2)
        *(.text*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata*)
    }

    .data : ALIGN(4096)
    {
        *(.data*)
    }

    .bss : ALIGN(4096)
    {
        *(COMMON)
        *(.bss*)
    }

    . = ALIGN(4096);

    __kernel_end = .;
}