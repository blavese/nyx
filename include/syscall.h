#pragma once
#include "types.h"

#define SYS_EXIT       0
#define SYS_PUTC       1
#define SYS_WRITE      2
#define SYS_GETPID     3
#define SYS_TICKS      4
#define SYS_SLEEP      5
#define SYS_READ_FILE  6

void syscall_init(void);

/* How many system calls have been served since boot. */
u32 syscall_count(void);
