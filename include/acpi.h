#pragma once
#include "types.h"

/* Just enough ACPI to find the other processors.
 *
 * The firmware describes the machine in a chain of tables: a pointer in low
 * memory leads to a directory of tables, one of which lists every local
 * interrupt controller, which is to say every CPU. There is no other way to
 * find out how many processors exist. */

#define ACPI_MAX_CPUS 16

typedef struct {
    bool found;                 /* the tables were there and made sense */
    u32  lapic_base;            /* MMIO address of the local APIC */
    u32  ncpus;
    u8   apic_id[ACPI_MAX_CPUS];
    u8   usable[ACPI_MAX_CPUS]; /* the firmware says this one can be started */
    char oem[7];
} acpi_info_t;

bool acpi_init(void);
const acpi_info_t *acpi(void);
