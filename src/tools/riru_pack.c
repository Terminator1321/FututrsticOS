#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RIRU_MAGIC0 'R'
#define RIRU_MAGIC1 'I'
#define RIRU_MAGIC2 'R'
#define RIRU_MAGIC3 'U'

#define RIRU_VERSION 1
#define RIRU_ARCH_X86_64 1
#define RIRU_TYPE_EXECUTABLE 1

#define PF_X 1
#define PF_W 2

#define PT_LOAD 1

#define DEFAULT_STACK_SIZE (1024ULL * 1024ULL)

typedef struct {
    uint8_t e_ident[16];

    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;

    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;

    uint32_t e_flags;

    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;

    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;

} elf64_header_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;

    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;

    uint64_t p_filesz;
    uint64_t p_memsz;

    uint64_t p_align;

} elf64_program_header_t;

typedef struct {
    uint8_t magic[4];

    uint16_t version;
    uint16_t architecture;
    uint16_t type;
    uint16_t flags;

    uint64_t entry;

    uint64_t code_offset;
    uint64_t code_vaddr;
    uint64_t code_size;

    uint64_t rodata_offset;
    uint64_t rodata_vaddr;
    uint64_t rodata_size;

    uint64_t data_offset;
    uint64_t data_vaddr;
    uint64_t data_size;

    uint64_t bss_size;

    uint64_t stack_size;

    uint64_t image_size;

} riru_header_t;

typedef struct {
    int found;

    uint64_t offset;
    uint64_t vaddr;

    uint64_t filesz;
    uint64_t memsz;

    uint32_t flags;

} segment_info_t;

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static int range_valid(uint64_t offset, uint64_t size, uint64_t file_size) {
    if (offset > file_size)
        return 0;

    if (size > file_size - offset)
        return 0;

    return 1;
}

static void *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");

    if (!file)
        return NULL;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long length = ftell(file);

    if (length <= 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    void *buffer = malloc((size_t)length);

    if (!buffer) {
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);

    *size = (size_t)length;

    return buffer;
}

static void add_segment(segment_info_t *segment, const elf64_program_header_t *ph) {
    if (!segment->found) {
        segment->found = 1;

        segment->offset = ph->p_offset;

        segment->vaddr = ph->p_vaddr;

        segment->filesz = ph->p_filesz;

        segment->memsz = ph->p_memsz;

        segment->flags = ph->p_flags;

        return;
    }

    uint64_t file_end = ph->p_offset + ph->p_filesz;

    uint64_t old_file_end = segment->offset + segment->filesz;

    if (file_end > old_file_end) {
        segment->filesz = file_end - segment->offset;
    }

    uint64_t mem_end = ph->p_vaddr + ph->p_memsz;

    uint64_t old_mem_end = segment->vaddr + segment->memsz;

    if (mem_end > old_mem_end) {
        segment->memsz = mem_end - segment->vaddr;
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("usage: riru_pack input.elf output.riru\n");

        return 1;
    }

    size_t elf_size = 0;

    uint8_t *elf_data = read_file(argv[1], &elf_size);

    if (!elf_data) {
        printf("failed to read ELF\n");
        return 1;
    }

    if (elf_size < sizeof(elf64_header_t)) {
        printf("invalid ELF\n");

        free(elf_data);
        return 1;
    }

    elf64_header_t *elf = (elf64_header_t *)elf_data;

    if (elf->e_ident[0] != 0x7F || elf->e_ident[1] != 'E' || elf->e_ident[2] != 'L' ||
        elf->e_ident[3] != 'F') {
        printf("not an ELF file\n");

        free(elf_data);
        return 1;
    }

    if (elf->e_ident[4] != 2) {
        printf("not ELF64\n");

        free(elf_data);
        return 1;
    }

    if (elf->e_machine != 62) {
        printf("not x86-64\n");

        free(elf_data);
        return 1;
    }

    if (elf->e_phentsize != sizeof(elf64_program_header_t)) {
        printf("unsupported program header size\n");

        free(elf_data);
        return 1;
    }

    uint64_t ph_end = elf->e_phoff + ((uint64_t)elf->e_phnum * elf->e_phentsize);

    if (ph_end > elf_size) {
        printf("program headers outside ELF\n");

        free(elf_data);
        return 1;
    }

    elf64_program_header_t *ph = (elf64_program_header_t *)(elf_data + elf->e_phoff);

    uint64_t first_code_vaddr = UINT64_MAX;

    for (uint16_t i = 0; i < elf->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;

        if (ph[i].p_memsz < ph[i].p_filesz) {
            printf("invalid segment %u\n", i);

            free(elf_data);
            return 1;
        }

        if (!range_valid(ph[i].p_offset, ph[i].p_filesz, elf_size)) {
            printf("segment outside ELF: %u\n", i);

            free(elf_data);
            return 1;
        }

        if (ph[i].p_flags & PF_X) {
            if (ph[i].p_vaddr < first_code_vaddr) {
                first_code_vaddr = ph[i].p_vaddr;
            }
        }
    }

    if (first_code_vaddr == UINT64_MAX) {
        printf("no executable segment found\n");

        free(elf_data);
        return 1;
    }

    segment_info_t code = {0};
    segment_info_t rodata = {0};
    segment_info_t data = {0};

    uint64_t bss_size = 0;

    for (uint16_t i = 0; i < elf->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;

        if (ph[i].p_flags & PF_X) {
            add_segment(&code, &ph[i]);
        } else if (ph[i].p_flags & PF_W) {
            add_segment(&data, &ph[i]);

            if (ph[i].p_memsz > ph[i].p_filesz) {
                bss_size += ph[i].p_memsz - ph[i].p_filesz;
            }
        } else {
            if (ph[i].p_vaddr < first_code_vaddr) {
                continue;
            }

            add_segment(&rodata, &ph[i]);
        }
    }

    if (!code.found) {
        printf("code segment not found\n");

        free(elf_data);
        return 1;
    }

    uint64_t header_size = sizeof(riru_header_t);

    uint64_t current_offset = align_up(header_size, 16);

    uint64_t code_offset = 0;
    uint64_t rodata_offset = 0;
    uint64_t data_offset = 0;

    if (code.filesz) {
        code_offset = current_offset;

        current_offset = align_up(current_offset + code.filesz, 16);
    }

    if (rodata.found && rodata.filesz) {
        rodata_offset = current_offset;

        current_offset = align_up(current_offset + rodata.filesz, 16);
    }

    if (data.found && data.filesz) {
        data_offset = current_offset;

        current_offset = align_up(current_offset + data.filesz, 16);
    }

    uint64_t image_size = current_offset;

    riru_header_t header;

    memset(&header, 0, sizeof(header));

    header.magic[0] = RIRU_MAGIC0;

    header.magic[1] = RIRU_MAGIC1;

    header.magic[2] = RIRU_MAGIC2;

    header.magic[3] = RIRU_MAGIC3;

    header.version = RIRU_VERSION;

    header.architecture = RIRU_ARCH_X86_64;

    header.type = RIRU_TYPE_EXECUTABLE;

    header.flags = 0;

    header.entry = elf->e_entry;

    header.code_offset = code_offset;

    header.code_vaddr = code.vaddr;

    header.code_size = code.filesz;

    header.rodata_offset = rodata_offset;

    header.rodata_vaddr = rodata.found ? rodata.vaddr : 0;

    header.rodata_size = rodata.filesz;

    header.data_offset = data_offset;

    header.data_vaddr = data.found ? data.vaddr : 0;

    header.data_size = data.filesz;

    header.bss_size = bss_size;

    header.stack_size = DEFAULT_STACK_SIZE;

    header.image_size = image_size;

    uint8_t *output = calloc(1, image_size);

    if (!output) {
        printf("out of memory\n");

        free(elf_data);
        return 1;
    }

    memcpy(output, &header, sizeof(header));

    if (code.filesz) {
        memcpy(output + code_offset, elf_data + code.offset, code.filesz);
    }

    if (rodata.found && rodata.filesz) {
        memcpy(output + rodata_offset, elf_data + rodata.offset, rodata.filesz);
    }

    if (data.found && data.filesz) {
        memcpy(output + data_offset, elf_data + data.offset, data.filesz);
    }

    FILE *out = fopen(argv[2], "wb");

    if (!out) {
        printf("failed to create output\n");

        free(output);
        free(elf_data);

        return 1;
    }

    if (fwrite(output, 1, image_size, out) != image_size) {
        printf("failed to write output\n");

        fclose(out);

        free(output);
        free(elf_data);

        return 1;
    }

    fclose(out);

    printf("RIRU created successfully\n");

    printf("Header: %llu bytes\n", (unsigned long long)sizeof(riru_header_t));

    printf("Entry: 0x%llX\n", (unsigned long long)header.entry);

    printf("Code: offset=0x%llX "
           "vaddr=0x%llX size=%llu\n",
           (unsigned long long)header.code_offset, (unsigned long long)header.code_vaddr,
           (unsigned long long)header.code_size);

    printf("Rodata: offset=0x%llX "
           "vaddr=0x%llX size=%llu\n",
           (unsigned long long)header.rodata_offset, (unsigned long long)header.rodata_vaddr,
           (unsigned long long)header.rodata_size);

    printf("Data: offset=0x%llX "
           "vaddr=0x%llX size=%llu\n",
           (unsigned long long)header.data_offset, (unsigned long long)header.data_vaddr,
           (unsigned long long)header.data_size);

    printf("BSS: %llu bytes\n", (unsigned long long)header.bss_size);

    printf("Stack: %llu bytes\n", (unsigned long long)header.stack_size);

    printf("Image: %llu bytes\n", (unsigned long long)header.image_size);

    free(output);
    free(elf_data);

    return 0;
}