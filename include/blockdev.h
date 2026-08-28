#pragma once
#include "types.h"

#define SECTOR_SIZE 512

/* Whichever disk controller was found, behind one interface. */
bool blk_init(void);
bool blk_present(void);
u32  blk_sectors(void);
const char *blk_model(void);
const char *blk_driver(void);
bool blk_read(u32 lba, u32 count, void *buf);
bool blk_write(u32 lba, u32 count, const void *buf);
bool blk_flush(void);
