#pragma once
#include "types.h"

#define PTE_PRESENT 0x1
#define PTE_RW      0x2
#define PTE_USER    0x4

void paging_init(void);
bool map_page(u32 virt, u32 phys, u32 flags);
void unmap_page(u32 virt);
u32  virt_to_phys(u32 virt);
u32  paging_directory_phys(void);
