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

/* Files. A program can now create, read, write and delete them, and walk
   directories, which is what lets the shell move out of the kernel. */
#define SYS_OPEN        13
#define SYS_CLOSE       14
#define SYS_FREAD       15
#define SYS_FWRITE      16
#define SYS_SEEK        17
#define SYS_UNLINK      18
#define SYS_MKDIR       19
#define SYS_RMDIR       20
#define SYS_READDIR     21
#define SYS_STAT        22
#define SYS_CHDIR       23
#define SYS_GETCWD      24

/* Sockets. One TCP connection at a time, which is what the stack supports. */
#define SYS_CONNECT     25
#define SYS_SEND        26
#define SYS_RECV        27
#define SYS_DISCONNECT  28
#define SYS_RESOLVE     29
#define SYS_NETINFO     30

/* What SYS_STAT and SYS_READDIR fill in. Fixed layout: ring 3 reads this
   straight out of a buffer the kernel wrote. */
typedef struct {
    u32  size;
    u32  is_dir;
    char name[32];
} nyx_stat_t;

/* What SYS_NETINFO fills in. */
typedef struct {
    u32 up;
    u32 ip, gateway, netmask, dns;
    u8  mac[6];
    u16 pad;
} nyx_netinfo_t;

void syscall_init(void);

/* Drops anything a dead task was holding, such as the one TCP socket. */
void syscall_release(u32 pid);

/* How many system calls have been served since boot. */
u32 syscall_count(void);
