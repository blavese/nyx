#pragma once
#include "types.h"

typedef struct {
    u8  bus, slot, func;
    u16 vendor, device;
    u32 bar0;
    u8  irq;
} pci_dev_t;

u32  pci_read32(u8 bus, u8 slot, u8 func, u8 offset);
u16  pci_read16(u8 bus, u8 slot, u8 func, u8 offset);
void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value);
void pci_write16(u8 bus, u8 slot, u8 func, u8 offset, u16 value);
bool pci_find(u16 vendor, u16 device, pci_dev_t *out);
bool pci_find_class(u8 class_code, u8 subclass, u8 prog_if, pci_dev_t *out);
void pci_enable_bus_master(const pci_dev_t *d);
