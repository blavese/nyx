#pragma once
#include "types.h"

#define SECTOR_SIZE 512

bool ata_init(void);
bool ata_present(void);
u32  ata_sectors(void);                 /* total addressable sectors */
const char *ata_model(void);
bool ata_read(u32 lba, u32 count, void *buf);
bool ata_write(u32 lba, u32 count, const void *buf);
bool ata_flush(void);
