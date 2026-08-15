#include "acpi.h"
#include "../libc/string.h"
#include "../multiboot2.h"
#include "../terminal/shell.h"
#include "../terminal/terminal.h"


typedef struct __attribute__((packed)) {
    char signature[8]; // "RSD PTR " (note trailing space)
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision; // 0 = ACPI 1.0 (RSDT only), >=2 = ACPI 2.0+ (XSDT too)
    uint32_t rsdt_address;
} acpi_rsdp_v1_t;

typedef struct __attribute__((packed)) {
    acpi_rsdp_v1_t v1;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} acpi_rsdp_v2_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;
    // variable-length entries follow
} acpi_madt_t;

#define MADT_TYPE_LOCAL_APIC 0
#define MADT_LAPIC_FLAG_ENABLED (1u << 0)

typedef struct __attribute__((packed)) {
    uint8_t type; // 0
    uint8_t length; // 8
    uint8_t acpi_processor_id;
    uint8_t apic_id;
    uint32_t flags;
} acpi_madt_lapic_t;

static uint32_t g_lapic_ids[ACPI_MAX_CPUS];
static int g_cpu_count = 0;
static uint64_t g_lapic_base = 0;

static void print_hex64(uint64_t value) {
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

static int checksum_ok(const void *data, uint32_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint8_t sum = 0;

    for (uint32_t i = 0; i < length; i++)
        sum = (uint8_t)(sum + bytes[i]);

    return sum == 0;
}

static acpi_sdt_header_t *find_table(uint64_t table_base, int use_xsdt, const char *signature) {
    acpi_sdt_header_t *root = (acpi_sdt_header_t *)(uintptr_t)table_base;

    uint32_t entry_count;
    if (use_xsdt)
        entry_count = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 8;
    else
        entry_count = (root->length - (uint32_t)sizeof(acpi_sdt_header_t)) / 4;

    uint8_t *entries = (uint8_t *)root + sizeof(acpi_sdt_header_t);

    for (uint32_t i = 0; i < entry_count; i++) {
        uint64_t phys;

        if (use_xsdt) {
            uint64_t raw;
            memcpy(&raw, entries + i * 8, sizeof(raw));
            phys = raw;
        } else {
            uint32_t raw;
            memcpy(&raw, entries + i * 4, sizeof(raw));
            phys = raw;
        }

        acpi_sdt_header_t *table = (acpi_sdt_header_t *)(uintptr_t)phys;

        if (memcmp(table->signature, signature, 4) == 0)
            return table;
    }

    return 0;
}

// Returns 0 on failure (leaves g_cpu_count at 0 so the caller falls back).
static int parse_madt(acpi_madt_t *madt) {
    if (!checksum_ok(madt, madt->header.length)) {
        terminal_print("ACPI: MADT checksum invalid, ignoring\n");
        return 0;
    }

    g_lapic_base = madt->local_apic_address;

    uint8_t *p = (uint8_t *)madt + sizeof(acpi_madt_t);
    uint8_t *end = (uint8_t *)madt + madt->header.length;

    while (p + 2 <= end) {
        uint8_t type = p[0];
        uint8_t len = p[1];

        if (len < 2)
            break;

        if (type == MADT_TYPE_LOCAL_APIC && len >= (uint8_t)sizeof(acpi_madt_lapic_t)) {
            acpi_madt_lapic_t entry;
            memcpy(&entry, p, sizeof(entry));

            if ((entry.flags & MADT_LAPIC_FLAG_ENABLED) && g_cpu_count < ACPI_MAX_CPUS)
                g_lapic_ids[g_cpu_count++] = entry.apic_id;
        }

        p += len;
    }

    return g_cpu_count > 0;
}

void acpi_init(void *mb2_info) {
    g_cpu_count = 0;
    g_lapic_base = 0;

    mb2_tag_t *new_tag = mb2_find_tag((mb2_info_t *)mb2_info, MB2_TAG_ACPI_NEW);
    mb2_tag_t *old_tag = mb2_find_tag((mb2_info_t *)mb2_info, MB2_TAG_ACPI_OLD);

    acpi_rsdp_v1_t *v1 = 0;
    acpi_rsdp_v2_t *v2 = 0;

    if (new_tag) {
        v2 = (acpi_rsdp_v2_t *)((mb2_tag_acpi_t *)new_tag)->rsdp;
        v1 = &v2->v1;
    } else if (old_tag) {
        v1 = (acpi_rsdp_v1_t *)((mb2_tag_acpi_t *)old_tag)->rsdp;
    }

    if (!v1 || memcmp(v1->signature, "RSD PTR ", 8) != 0) {
        terminal_print("ACPI: no RSDP from bootloader, assuming single core\n");
        g_cpu_count = 1;
        g_lapic_ids[0] = 0;
        return;
    }

    int use_xsdt = 0;
    uint64_t table_base = v1->rsdt_address;

    if (v2 && v1->revision >= 2 && v2->xsdt_address != 0 && checksum_ok(v2, v2->length)) {
        table_base = v2->xsdt_address;
        use_xsdt = 1;
    } else if (!checksum_ok(v1, sizeof(*v1))) {
        terminal_print("ACPI: RSDP checksum invalid, assuming single core\n");
        g_cpu_count = 1;
        g_lapic_ids[0] = 0;
        return;
    }

    acpi_sdt_header_t *root = (acpi_sdt_header_t *)(uintptr_t)table_base;
    if (!checksum_ok(root, root->length)) {
        terminal_print("ACPI: RSDT/XSDT checksum invalid, assuming single core\n");
        g_cpu_count = 1;
        g_lapic_ids[0] = 0;
        return;
    }

    acpi_sdt_header_t *madt = find_table(table_base, use_xsdt, "APIC");

    if (!madt || !parse_madt((acpi_madt_t *)madt)) {
        terminal_print("ACPI: no usable MADT, assuming single core\n");
        g_cpu_count = 1;
        g_lapic_ids[0] = 0;
        return;
    }

    terminal_print("ACPI: found ");
    print_uint((uint64_t)g_cpu_count);
    terminal_print(" CPU core(s), Local APIC at ");
    print_hex64(g_lapic_base);
    terminal_print("\n");
}

int acpi_cpu_count(void) {
    return g_cpu_count > 0 ? g_cpu_count : 1;
}

uint32_t acpi_cpu_lapic_id(int index) {
    if (index < 0 || index >= g_cpu_count)
        return 0;

    return g_lapic_ids[index];
}

uint64_t acpi_lapic_base(void) {
    return g_lapic_base;
}