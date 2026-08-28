#pragma once
#include "types.h"
#include "idt.h"

typedef enum { TASK_READY, TASK_RUNNING, TASK_SLEEPING, TASK_DEAD } task_state_t;

typedef struct task {
    u32  esp;                 /* saved kernel stack pointer */
    u32  stack_base;
    u32  pid;
    char name[32];
    task_state_t state;
    u64  wake_at;             /* tick to wake on when sleeping */
    u32  slices;              /* how many times it has been scheduled */
    struct task *next;
} task_t;

void   sched_init(void);
task_t *task_create(const char *name, void (*entry)(void));
void   sched_start(void);
void   task_exit(void);
void   task_sleep(u32 ms);
void   task_yield(void);
task_t *task_current(void);
task_t *task_list(void);
u32    task_count(void);
