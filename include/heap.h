#pragma once
#include "types.h"
void  heap_init(u32 start, u32 size);
void *kmalloc(size_t n);
void *kcalloc(size_t n);
void  kfree(void *p);
u32   heap_used(void);
u32   heap_total(void);
