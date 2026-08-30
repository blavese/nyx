/* PCI configuration space through the legacy 0xCF8/0xCFC port pair.
   Enough to walk the bus and find a device by vendor and id. */
#include "pci.h"
#include "io.h"
#include "printf.h"

#define CONFIG_ADDR 0xCF8
#define CONFIG_DATA 0xCFC

static u32 address(u8 bus, u8 slot, u8 func, u8 offset) {
    return 0x80000000u
         | ((u32)bus  << 16)
         | ((u32)slot << 11)
         | ((u32)func << 8)
         | ((u32)offset & 0xFC);
}

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset) {
    outl(CONFIG_ADDR, address(bus, slot, func, offset));
    return inl(CONFIG_DATA);
}

u16 pci_read16(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 v = pci_read32(bus, slot, func, offset);
    return (u16)((v >> ((offset & 2) * 8)) & 0xFFFF);
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    outl(CONFIG_ADDR, address(bus, slot, func, offset));
    outl(CONFIG_DATA, value);
}

void pci_write16(u8 bus, u8 slot, u8 func, u8 offset, u16 value) {
    u32 old = pci_read32(bus, slot, func, offset);
    u32 shift = (offset & 2) * 8;
    u32 v = (old & ~(0xFFFFu << shift)) | ((u32)value << shift);
    pci_write32(bus, slot, func, offset, v);
}

bool pci_find(u16 vendor, u16 device, pci_dev_t *out) {
    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            for (u8 func = 0; func < 8; func++) {
                u32 id = pci_read32((u8)bus, slot, func, 0x00);
                u16 v = (u16)(id & 0xFFFF);
                u16 d = (u16)(id >> 16);
                if (v == 0xFFFF) continue;
                if (v == vendor && d == device) {
                    out->bus = (u8)bus;
                    out->slot = slot;
                    out->func = func;
                    out->vendor = v;
                    out->device = d;
                    out->bar0 = pci_read32((u8)bus, slot, func, 0x10);
                    out->irq  = (u8)(pci_read32((u8)bus, slot, func, 0x3C) & 0xFF);
                    return true;
                }
                if (func == 0) {
                    u32 hdr = pci_read32((u8)bus, slot, 0, 0x0C);
                    if (!((hdr >> 16) & 0x80)) break;   /* not multifunction */
                }
            }
        }
    }
    return false;
}

bool pci_find_class(u8 class_code, u8 subclass, u8 prog_if, pci_dev_t *out) {
    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            for (u8 func = 0; func < 8; func++) {
                u32 id = pci_read32((u8)bus, slot, func, 0x00);
                if ((u16)(id & 0xFFFF) == 0xFFFF) continue;

                u32 cls = pci_read32((u8)bus, slot, func, 0x08);
                if ((u8)(cls >> 24) != class_code) continue;
                if ((u8)(cls >> 16) != subclass) continue;
                if ((u8)(cls >> 8) != prog_if) continue;

                out->bus = (u8)bus;
                out->slot = slot;
                out->func = func;
                out->vendor = (u16)(id & 0xFFFF);
                out->device = (u16)(id >> 16);
                out->bar0 = pci_read32((u8)bus, slot, func, 0x10);
                out->irq  = (u8)(pci_read32((u8)bus, slot, func, 0x3C) & 0xFF);
                return true;
            }
        }
    }
    return false;
}

void pci_enable_bus_master(const pci_dev_t *d) {
    u16 cmd = pci_read16(d->bus, d->slot, d->func, 0x04);
    cmd |= (1 << 0)    /* respond to I/O space */
         | (1 << 1)    /* respond to memory space */
         | (1 << 2);   /* allow it to drive the bus */
    pci_write16(d->bus, d->slot, d->func, 0x04, cmd);
}
