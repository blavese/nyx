#pragma once
#include "types.h"
#include "idt.h"

/* Matches VFS_PATH_MAX. Spelled out rather than included, because the
   scheduler has no other reason to know about the filesystem. */
#define TASK_CWD_MAX 128

typedef enum { TASK_READY, TASK_RUNNING, TASK_SLEEPING, TASK_DEAD } task_state_t;

typedef struct task {
    u32  esp;                 /* saved kernel stack pointer */
    u32  stack_base;
    u32  pid;
    char name[32];
    task_state_t state;
    u64  wake_at;             /* tick to wake on when sleeping */
    u32  slices;              /* how many times it has been scheduled */
    u32  dir;                 /* address space, 0 means the kernel's */
    bool user;                /* runs in ring 3 */
    char cwd[TASK_CWD_MAX];   /* working directory, inherited at creation */
    struct task *next;
} task_t;

void   sched_init(void);
task_t *task_create(const char *name, void (*entry)(void));

/* Builds a ring 3 task in its own address space. entry and stack_top are
   addresses in that space, not the kernel's. */
task_t *task_create_user(const char *name, u32 dir, u32 entry, u32 stack_top);
void   sched_start(void);
void   task_exit(void);
void   task_sleep(u32 ms);
void   task_yield(void);
task_t *task_current(void);
task_t *task_list(void);

/* True while a pid is still in the run queue and not finished. */
bool   task_alive(u32 pid);
void   task_wait(u32 pid);
u32    task_count(void);
