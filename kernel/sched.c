/* Round-robin preemptive scheduler.
 *
 * Every task owns a kernel stack. A task that is not running has a complete
 * interrupt frame sitting on that stack, so switching tasks is just a matter
 * of telling the interrupt return path to unwind a different one. */
#include "sched.h"
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

task_t *task_create(const char *name, void (*entry)(void)) {
    task_t *t = (task_t *)kcalloc(sizeof(task_t));
    if (!t) return 0;
    u8 *stack = (u8 *)kmalloc(STACK_SIZE);
    if (!stack) { kfree(t); return 0; }

    t->stack_base = (u32)stack;
    t->pid = next_pid++;
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->state = TASK_READY;

    /* Build the frame an interrupt return expects to find. Because this is a
       ring 0 task, iret will not pop useresp/ss, so those two are left out of
       the arithmetic below. */
    u32 top = (u32)stack + STACK_SIZE;
    top &= ~0xFu;
    registers_t *f = (registers_t *)(top - sizeof(registers_t));
    memset(f, 0, sizeof(*f));
    f->ds     = 0x10;
    f->eip    = (u32)entry;
    f->cs     = 0x08;
    f->eflags = 0x202;            /* IF set: the task runs interruptible */
    f->int_no = 32;
    t->esp = (u32)f;

    if (!head) { head = t; t->next = t; }
    else {
        task_t *p = head;
        while (p->next != head) p = p->next;
        p->next = t;
        t->next = head;
    }
    return t;
}

task_t *task_create_user(const char *name, u32 dir, u32 entry, u32 stack_top) {
    task_t *t = (task_t *)kcalloc(sizeof(task_t));
    if (!t) return 0;
    u8 *stack = (u8 *)kmalloc(STACK_SIZE);
    if (!stack) { kfree(t); return 0; }

    t->stack_base = (u32)stack;
    t->pid = next_pid++;
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->state = TASK_READY;
    t->dir = dir;
    t->user = true;

    /* A frame taken at a privilege change carries the user stack and its
       selector as well, and iret pops both on the way back out. */
    u32 top = ((u32)stack + STACK_SIZE) & ~0xFu;
    registers_t *f = (registers_t *)(top - sizeof(registers_t));
    memset(f, 0, sizeof(*f));
    f->ds      = 0x23;            /* user data, rpl 3 */
    f->eip     = entry;
    f->cs      = 0x1B;            /* user code, rpl 3 */
    f->eflags  = 0x202;           /* interrupts stay on in user code */
    f->useresp = stack_top;
    f->ss      = 0x23;
    f->int_no  = 32;
    t->esp = (u32)f;

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
u32 scheduler_switch(u32 esp) {
    if (!started || !head) return esp;

    if (current) {
        current->esp = esp;
        if (current->state == TASK_RUNNING) current->state = TASK_READY;
    }

    task_t *next = pick_next(current);
    if (!next) return esp;

    /* Reap anything that finished, but never the task we are about to run. */
    if (head) {
        task_t *p = head;
        for (u32 i = 0; i < 4096; i++) {
            task_t *n = p->next;
            if (n != p && n->state == TASK_DEAD && n != next && n != head) {
                p->next = n->next;
                kfree((void *)n->stack_base);
                kfree(n);
            } else p = n;
            if (p == head) break;
        }
    }

    current = next;
    current->state = TASK_RUNNING;
    current->slices++;

    /* The next interrupt taken in this task has to land on a stack the CPU
       can find, and in user mode it finds it here. */
    tss_set_stack(current->stack_base + STACK_SIZE);

    u32 want = current->dir ? current->dir : paging_kernel_directory();
    if (want != paging_current_directory()) paging_switch(want);

    return current->esp;
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
    if (current) current->state = TASK_DEAD;
    for (;;) task_yield();
}
