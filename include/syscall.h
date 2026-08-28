#pragma once
#include "types.h"

#define SYS_EXIT       0
#define SYS_PUTC       1
#define SYS_WRITE      2
#define SYS_GETPID     3
#define SYS_TICKS      4
#define SYS_SLEEP      5
#define SYS_READ_FILE  6

/* The window server. A graphical program creates a window, asks where its
   pixels landed in its own address space, and then draws into them. */
#define SYS_WIN_CREATE   7
#define SYS_WIN_SURFACE  8
#define SYS_WIN_SIZE     9
#define SYS_WIN_POLL    10
#define SYS_WIN_COMMIT  11
#define SYS_WIN_CLOSE   12

void syscall_init(void);

/* How many system calls have been served since boot. */
u32 syscall_count(void);
