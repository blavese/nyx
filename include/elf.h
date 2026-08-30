#pragma once
#include "types.h"

#define ELF_OK            0
#define ELF_ERR_SHORT    -1
#define ELF_ERR_MAGIC    -2
#define ELF_ERR_CLASS    -3
#define ELF_ERR_TYPE     -4
#define ELF_ERR_RANGE    -5
#define ELF_ERR_OVERFLOW -6
#define ELF_ERR_MEMORY   -7

/* Loads every PT_LOAD segment into the given address space. */
int elf_load(u64 dir, const u8 *image, u64 size, u64 *entry_out);
const char *elf_error(int code);
