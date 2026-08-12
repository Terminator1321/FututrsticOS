#include "elf.h"

static int elf_check_magic(const uint8_t *ident) {
    return ident[0] == 0x7F && ident[1] == 'E' && ident[2] == 'L' && ident[3] == 'F';
}

int elf_validate(const void *file, size_t size) {
    if (!file)
        return -1;

    if (size < sizeof(elf64_header_t))
        return -2;

    const elf64_header_t *header = (const elf64_header_t *)file;

    if (!elf_check_magic(header->e_ident))
        return -3;

    if (header->e_ident[4] != ELFCLASS64)
        return -4;

    if (header->e_ident[5] != ELFDATA2LSB)
        return -5;

    if (header->e_machine != EM_X86_64)
        return -6;

    if (header->e_type != ET_EXEC && header->e_type != ET_DYN)
        return -7;

    if (header->e_phentsize != sizeof(elf64_program_header_t))
        return -8;

    if (header->e_phoff > size)
        return -9;

    uint64_t ph_end = header->e_phoff + ((uint64_t)header->e_phnum * header->e_phentsize);

    if (ph_end > size)
        return -10;

    if (header->e_entry == 0)
        return -11;

    return 0;
}

int elf_get_header(const void *file, size_t size, elf64_header_t *header) {
    if (!header)
        return -1;

    if (elf_validate(file, size) != 0)
        return -2;

    const elf64_header_t *source = (const elf64_header_t *)file;

    *header = *source;

    return 0;
}

int elf_get_program_header(const void *file, size_t size, uint16_t index,
                           elf64_program_header_t *header) {
    if (!header)
        return -1;

    if (elf_validate(file, size) != 0)
        return -2;

    const elf64_header_t *elf = (const elf64_header_t *)file;

    if (index >= elf->e_phnum)
        return -3;

    const uint8_t *base = (const uint8_t *)file;

    const elf64_program_header_t *ph =
        (const elf64_program_header_t *)(base + elf->e_phoff +
                                         ((uint64_t)index * elf->e_phentsize));

    *header = *ph;

    return 0;
}

int elf_is_loadable(const elf64_program_header_t *header) {
    if (!header)
        return 0;

    return header->p_type == PT_LOAD;
}