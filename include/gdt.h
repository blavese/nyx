#pragma once
#include "types.h"
void gdt_init(void);
void tss_set_stack(u32 esp0);
