#pragma once
#include "types.h"

bool ahci_init(void);
bool ahci_present(void);
u32  ahci_sectors(void);
const char *ahci_model(void);
bool ahci_read(u32 lba, u32 count, void *buf);
bool ahci_write(u32 lba, u32 count, const void *buf);
bool ahci_flush(void);
