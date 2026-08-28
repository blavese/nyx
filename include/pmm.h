#pragma once
#include "types.h"
#include "multiboot.h"

#define PAGE_SIZE 4096u

void pmm_init(multiboot_info_t *mbi);
u32  pmm_alloc_frame(void);        /* returns physical addr, 0 on failure */
void pmm_free_frame(u32 addr);
u32  pmm_total_frames(void);
u32  pmm_used_frames(void);
u32  pmm_free_frames(void);
