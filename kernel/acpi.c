/* Finding the other processors.
 *
 * A PC does not tell you how many CPUs it has; you have to go and look. The
 * firmware leaves a pointer in low memory, that leads to a directory of
 * tables, and one of those tables lists every local interrupt controller.
 * Each of those is a processor.
 *
 * Everything here is read-only and happens once, at boot. The tables can sit
 * anywhere in physical memory, including above the region the kernel
 * identity maps, so each one is mapped before it is read. */
#include "acpi.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"
#include "printf.h"

typedef struct {
    char sig[8];               /* "RSD PTR " */
    u8   checksum;
    char oem[6];
    u8   revision;
    u32  rsdt_address;
    /* revision 2 adds a length, an xsdt address and a second checksum */
    u32  length;
    u64  xsdt_address;
    u8   ext_checksum;
    u8   reserved[3];
} __attribute__((packed)) rsdp_t;

typedef struct {
    char sig[4];
    u32  length;
    u8   revision;
    u8   checksum;
    char oem[6];
    char oem_table[8];
    u32  oem_revision;
    u32  creator_id;
    u32  creator_revision;
} __attribute__((packed)) sdt_header_t;

/* Multiple APIC Description Table: the header, then a stream of entries of
   differing kinds, each carrying its own length. */
typedef struct {
    sdt_header_t header;
    u32 lapic_address;
    u32 flags;
} __attribute__((packed)) madt_t;

#define MADT_LAPIC          0
#define MADT_LAPIC_OVERRIDE 5

static acpi_info_t info;

const acpi_info_t *acpi(void) { return &info; }

/* Makes a physical range readable. Below the identity mapped region there is
   nothing to do; above it, the pages are mapped where they already are, so
   the pointer the caller gets is the physical address either way. */
static const void *map_phys(u64 phys, u64 len) {
    u64 limit = KERNEL_SPACE_MB * 1024ull * 1024ull;
    if (phys + len <= limit) return (const void *)phys;

    u64 first = phys & ~0xFFFull;
    u64 last = (phys + len + PAGE_SIZE - 1) & ~0xFFFull;
    for (u64 a = first; a < last; a += PAGE_SIZE)
        if (!virt_to_phys(a) && !map_page(a, a, PTE_PRESENT | PTE_RW)) return 0;
    return (const void *)phys;
}

static bool checksum_ok(const u8 *p, u32 len) {
    u8 sum = 0;
    for (u32 i = 0; i < len; i++) sum = (u8)(sum + p[i]);
    return sum == 0;
}

/* The pointer is either in the first kilobyte of the extended BIOS data area
   or somewhere in the last 128 KiB below a megabyte, on a 16 byte boundary
   in both cases. */
static const rsdp_t *find_rsdp(void) {
    u64 ebda = (u64)(*(volatile u16 *)0x40E) << 4;
    if (ebda >= 0x400 && ebda < 0xA0000) {
        for (u64 a = ebda; a < ebda + 1024; a += 16) {
            const rsdp_t *r = (const rsdp_t *)a;
            if (memcmp(r->sig, "RSD PTR ", 8) == 0 && checksum_ok((const u8 *)r, 20))
                return r;
        }
    }
    for (u64 a = 0xE0000; a < 0x100000; a += 16) {
        const rsdp_t *r = (const rsdp_t *)a;
        if (memcmp(r->sig, "RSD PTR ", 8) == 0 && checksum_ok((const u8 *)r, 20))
            return r;
    }
    return 0;
}

static void read_madt(const madt_t *madt) {
    info.lapic_base = madt->lapic_address;

    u32 len = madt->header.length;
    const u8 *p = (const u8 *)madt + sizeof(madt_t);
    const u8 *end = (const u8 *)madt + len;

    while (p + 2 <= end) {
        u8 type = p[0], entry_len = p[1];
        if (entry_len < 2) break;                 /* malformed; stop */
        if (p + entry_len > end) break;

        if (type == MADT_LAPIC && entry_len >= 8) {
            u8 apic_id = p[3];
            u32 flags = *(const u32 *)(p + 4);
            if (info.ncpus < ACPI_MAX_CPUS) {
                info.apic_id[info.ncpus] = apic_id;
                /* Bit 0 says it is enabled; bit 1 says it could be brought
                   online later. Either is worth trying. */
                info.usable[info.ncpus] = (flags & 0x3) ? 1 : 0;
                info.ncpus++;
            }
        } else if (type == MADT_LAPIC_OVERRIDE && entry_len >= 12) {
            /* A 64 bit address, but the low half is what a 32 bit kernel can
               reach, and no real machine puts it above 4 GiB. */
            info.lapic_base = *(const u32 *)(p + 4);
        }

        p += entry_len;
    }
}

bool acpi_init(void) {
    memset(&info, 0, sizeof(info));
    info.lapic_base = 0xFEE00000;                 /* the architectural default */

    const rsdp_t *rsdp = find_rsdp();
    if (!rsdp) return false;

    memcpy(info.oem, rsdp->oem, 6);
    info.oem[6] = 0;

    const sdt_header_t *rsdt = (const sdt_header_t *)map_phys(rsdp->rsdt_address,
                                                              sizeof(sdt_header_t));
    if (!rsdt || memcmp(rsdt->sig, "RSDT", 4) != 0) return false;

    u32 length = rsdt->length;
    if (length < sizeof(sdt_header_t) || length > 0x10000) return false;
    if (!map_phys(rsdp->rsdt_address, length)) return false;
    if (!checksum_ok((const u8 *)rsdt, length)) return false;

    u32 count = (length - sizeof(sdt_header_t)) / 4;
    const u32 *entries = (const u32 *)((const u8 *)rsdt + sizeof(sdt_header_t));

    for (u32 i = 0; i < count; i++) {
        const sdt_header_t *h = (const sdt_header_t *)map_phys(entries[i],
                                                               sizeof(sdt_header_t));
        if (!h) continue;
        if (memcmp(h->sig, "APIC", 4) != 0) continue;
        if (h->length < sizeof(madt_t) || h->length > 0x10000) continue;
        if (!map_phys(entries[i], h->length)) continue;
        if (!checksum_ok((const u8 *)h, h->length)) continue;

        read_madt((const madt_t *)h);
        info.found = info.ncpus > 0;
        return info.found;
    }

    return false;
}
