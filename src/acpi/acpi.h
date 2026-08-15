#pragma once

#include <stdint.h>
#define ACPI_MAX_CPUS 4

void acpi_init(void *mb2_info);
int acpi_cpu_count(void);

uint32_t acpi_cpu_lapic_id(int index);
uint64_t acpi_lapic_base(void);