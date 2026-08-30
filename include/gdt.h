#pragma once
#include "types.h"

/* Selectors, which are byte offsets into the descriptor table rather than
   indices. The low two bits carry the requested privilege level, so a user
   selector is the offset with 3 added. */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20
#define GDT_TSS         0x28

#define USER_CODE_SEL (GDT_USER_CODE | 3)
#define USER_DATA_SEL (GDT_USER_DATA | 3)

void gdt_init(void);

/* The stack an interrupt taken in ring 3 lands on. */
void tss_set_stack(u64 rsp0);
