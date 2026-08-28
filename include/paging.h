#pragma once
#include "types.h"

#define PTE_PRESENT 0x1
#define PTE_RW      0x2
#define PTE_USER    0x4

#define KERNEL_SPACE_MB 16u        /* identity mapped, shared by every space */

void paging_init(void);

/* Operate on whichever directory is currently loaded. */
bool map_page(u32 virt, u32 phys, u32 flags);
void unmap_page(u32 virt);
u32  virt_to_phys(u32 virt);

/* Operate on a named directory, which need not be the active one. */
bool map_page_in(u32 dir_phys, u32 virt, u32 phys, u32 flags);
u32  virt_to_phys_in(u32 dir_phys, u32 virt);

/* A fresh address space sharing the kernel's mappings. Returns the physical
   address of the directory, or 0. */
u32  paging_new_directory(void);
void paging_free_directory(u32 dir_phys);
void paging_switch(u32 dir_phys);
u32  paging_current_directory(void);
u32  paging_kernel_directory(void);
