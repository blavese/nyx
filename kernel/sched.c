/* Round-robin preemptive scheduler.
 *
 * Every task owns a kernel stack. A task that is not running has a complete
 * interrupt frame sitting on that stack, so switching tasks is just a matter
 * of telling the interrupt return path to unwind a different one. */
#include "sched.h"
#include "winsrv.h"
#include "vfs.h"
#include "syscall.h"
#include "heap.h"
#include "printf.h"
#include "string.h"
#include "timer.h"
#include "gdt.h"
#include "io.h"
#include "paging.h"

#define STACK_SIZE 16384u

static task_t *head;        /* circular list */
static task_t *current;
static u32 next_pid = 1;
static bool started = false;

void sched_init(void) { head = current = 0; next_pid = 1; started = false; }

/* A task starts in the directory its creator was in, which is what makes
   `cd` then `exec` behave the way anyone would expect. */
static void inherit_cwd(task_t *t) {
    task_t *parent = task_current();
    if (parent && parent->cwd[0]) strncpy(t->cwd, parent->cwd, TASK_CWD_MAX - 1);
    else                          strncpy(t->cwd, "/", TASK_CWD_MAX - 1);
    t->cwd[TASK_CWD_MAX - 1] = 0;
}

task_t *task_create(const char *name, void (*entry)(void)) {
    task_t *t = (task_t *)kcalloc(sizeof(task_t));
    if (!t) return 0;
    u8 *stack = (u8 *)kmalloc(STACK_SIZE);
    if (!stack) { kfree(t); return 0; }

    t->stack_base = (u64)stack;
    t->pid = next_pid++;
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->state = TASK_READY;
    inherit_cwd(t);

    /* Build the frame an interrupt return expects to find. Long mode always
       pops rsp and ss, even returning to the same privilege level, so unlike
       32-bit there is no shorter frame for a kernel task: both fields have to
       be filled in. */
    u64 top = ((u64)stack + STACK_SIZE) & ~0xFull;
    registers_t *f = (registers_t *)(top - sizeof(registers_t));
    memset(f, 0, sizeof(*f));
    f->rip    = (u64)entry;
    f->cs     = GDT_KERNEL_CODE;
    f->ss     = GDT_KERNEL_DATA;
    f->rsp    = top;
    f->rflags = 0x202;            /* IF set: the task runs interruptible */
    f->int_no = 32;
    t->rsp = (u64)f;

    if (!head) { head = t; t->next = t; }
    else {
        task_t *p = head;
        while (p->next != head) p = p->next;
        p->next = t;
        t->next = head;
    }
    return t;
}

task_t *task_create_user(const char *name, u64 dir, u64 entry, u64 stack_top) {
    task_t *t = (task_t *)kcalloc(sizeof(task_t));
    if (!t) return 0;
    u8 *stack = (u8 *)kmalloc(STACK_SIZE);
    if (!stack) { kfree(t); return 0; }

    t->stack_base = (u64)stack;
    t->pid = next_pid++;
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->state = TASK_READY;
    t->dir = dir;
    t->user = true;
    inherit_cwd(t);

    /* The privilege change is what makes this frame different: the selectors
       carry a requested privilege of 3, and the stack it returns to is the
       program's rather than this one. */
    u64 top = ((u64)stack + STACK_SIZE) & ~0xFull;
    registers_t *f = (registers_t *)(top - sizeof(registers_t));
    memset(f, 0, sizeof(*f));
    f->rip    = entry;
    f->cs     = USER_CODE_SEL;
    f->ss     = USER_DATA_SEL;
    f->rsp    = stack_top;
    f->rflags = 0x202;            /* interrupts stay on in user code */
    f->int_no = 32;
    t->rsp = (u64)f;

    if (!head) { head = t; t->next = t; }
    else {
        task_t *p = head;
        while (p->next != head) p = p->next;
        p->next = t;
        t->next = head;
    }
    return t;
}

task_t *task_current(void) { return current; }
task_t *task_list(void)    { return head; }

bool task_alive(u32 pid) {
    if (!head) return false;
    task_t *p = head;
    do {
        if (p->pid == pid) return p->state != TASK_DEAD;
        p = p->next;
    } while (p != head);
    return false;      /* already reaped */
}

/* Gives up the processor until the given task finishes. */
void task_wait(u32 pid) {
    while (task_alive(pid)) task_yield();
}

u32 task_count(void) {
    if (!head) return 0;
    u32 n = 0; task_t *p = head;
    do { n++; p = p->next; } while (p != head);
    return n;
}

static task_t *pick_next(task_t *from) {
    task_t *p = from ? from->next : head;
    u64 now = timer_ticks();
    for (u32 i = 0; i < 4096 && p; i++, p = p->next) {
        if (p->state == TASK_SLEEPING && now >= p->wake_at) p->state = TASK_READY;
        if (p->state == TASK_READY || p->state == TASK_RUNNING) return p;
    }
    return from;
}

/* Called from the interrupt dispatcher on every timer tick. Returns the
   stack pointer the interrupt return path should unwind. */
u64 scheduler_switch(u64 rsp) {
    if (!started || !head) return rsp;

    if (current) {
        current->rsp = rsp;
        if (current->state == TASK_RUNNING) current->state = TASK_READY;
    }

    task_t *next = pick_next(current);
    if (!next) return rsp;

    current = next;
    current->state = TASK_RUNNING;
    current->slices++;

    /* The next interrupt taken in this task has to land on a stack the CPU
       can find, and in user mode it finds it here. */
    tss_set_stack(current->stack_base + STACK_SIZE);

    u64 want = current->dir ? current->dir : paging_kernel_directory();
    if (want != paging_current_directory()) paging_switch(want);

    /* Reap anything that finished, but never the task we are about to run.
       Switch address spaces first: the task that just exited may still own
       the active page directory, and freeing the CR3 currently in use would
       tear the floor out from under this code. */
    if (head) {
        task_t *p = head;
        for (u32 i = 0; i < 4096; i++) {
            task_t *n = p->next;
            if (n != p && n->state == TASK_DEAD && n != current && n != head) {
                p->next = n->next;
                if (n->dir) paging_free_directory(n->dir);
                kfree((void *)n->stack_base);
                kfree(n);
            } else p = n;
            if (p == head) break;
        }
    }

    return current->rsp;
}

void sched_start(void) {
    if (!head) panic("sched_start with no tasks");
    started = true;
    sti();
    for (;;) hlt();          /* the first timer tick takes us into a task */
}

void task_yield(void) {
    /* Give up the rest of this slice by asking for a tick early. */
    __asm__ volatile ("int $32");
}

void task_sleep(u32 ms) {
    if (!current) { sleep_ms(ms); return; }
    current->wake_at = timer_ticks() + (ms * timer_hz()) / 1000u;
    current->state = TASK_SLEEPING;
    task_yield();
}

void task_exit(void) {
    /* Take back anything the task still holds. A graphical program that
       crashes must not leave its window on the desktop. */
    if (current) {
        winsrv_release(current->pid);
        vfs_release(current->pid);
        syscall_release(current->pid);
    }
    if (current) current->state = TASK_DEAD;
    for (;;) task_yield();
}
