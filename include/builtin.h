#pragma once
#include "types.h"

/* Programs linked into the kernel image. They are put into the filesystem at
   boot, so `ls` shows them and `exec` runs them like anything else. */
void builtin_install(void);
u32  builtin_count_programs(void);
