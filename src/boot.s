section .multiboot2
align 8
mb2_start:
    dd 0xE85250D6                               ; magic
    dd 0                                        ; arch: i386 protected mode
    dd mb2_end - mb2_start                      ; header length
    dd -(0xE85250D6 + 0 + (mb2_end - mb2_start)); checksum

    ; framebuffer request tag (type 5)
    align 8
    dw 5        ; type
    dw 0        ; flags: required
    dd 20       ; size
    dd 1920     ; preferred width
    dd 1080     ; preferred height
    dd 32       ; preferred bpp

    ; end tag
    align 8
    dw 0
    dw 0
    dd 8
mb2_end:

section .bss
align 4096
pml4_table: resb 4096
pdpt_table: resb 4096
pd_table_0: resb 4096   ; 0 – 1 GB
pd_table_1: resb 4096   ; 1 – 2 GB
pd_table_2: resb 4096   ; 2 – 3 GB
pd_table_3: resb 4096   ; 3 – 4 GB  <- framebuffer lives here
stack_bottom: resb 16384
stack_top:
mb2_info_ptr: resb 8    ; save EBX here before we clobber registers

; 64-bit GDT
section .rodata
gdt64:
    dq 0
.code: equ $ - gdt64
    dq (1<<43)|(1<<44)|(1<<47)|(1<<53)
gdt64_ptr:
    dw $ - gdt64 - 1
    dq gdt64

; 32-bit entry
section .text
bits 32
global _start
extern kmain

_start:
     mov [mb2_info_ptr], ebx ; save Multiboot2 info pointer before anything else
    mov esp, stack_top

    ; Page tables: identity-map all 4 GB with 2 MB huge pages
    ; PML4[0] -> PDPT
    mov eax, pdpt_table
    or  eax, 0x3
    mov [pml4_table], eax

    ; PDPT[0..3] -> PD0..PD3
    mov eax, pd_table_0
    or  eax, 0x3
    mov [pdpt_table + 0*8], eax

    mov eax, pd_table_1
    or  eax, 0x3
    mov [pdpt_table + 1*8], eax

    mov eax, pd_table_2
    or  eax, 0x3
    mov [pdpt_table + 2*8], eax

    mov eax, pd_table_3
    or  eax, 0x3
    mov [pdpt_table + 3*8], eax

    ; fill all 4 PDs: 2048 entries × 2 MB = 4 GB
    mov ecx, 512 * 4
    xor eax, eax
    or  eax, 0x83               ; present + writable + PS (huge page)
    mov edi, pd_table_0
.fill:
    mov [edi], eax
    add eax, 0x200000           ; next 2 MB physical address
    add edi, 8
    loop .fill

    ; Enter long mode
    mov eax, pml4_table
    mov cr3, eax

    mov eax, cr4
    or  eax, (1 << 5)           ; PAE
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)           ; LME
    wrmsr

    mov eax, cr0
    or  eax, (1 << 31)          ; PG
    mov cr0, eax

    lgdt [gdt64_ptr]
    jmp  gdt64.code:long_mode_start

; 64-bit
bits 64
long_mode_start:
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rdi, [mb2_info_ptr]     ; first argument to kmain
    call kmain

.hang:
    cli
    hlt
    jmp .hang
    