/* System calls.
 *
 * int 0x80, arguments in registers, result in eax. The gate is the only door
 * from ring 3 into the kernel, so every pointer that arrives through it comes
 * from code that is not trusted and has to be checked against the caller's
 * own address space before it is touched. */
#include "syscall.h"
#include "idt.h"
#include "printf.h"
#include "paging.h"
#include "pmm.h"
#include "sched.h"
#include "fs.h"
#include "string.h"
#include "timer.h"
#include "winsrv.h"

/* User space is everything above the kernel's identity mapped region. */
#define USER_MIN (KERNEL_SPACE_MB * 1024u * 1024u)

/* Confirms a user buffer is really mapped, really belongs to user space, and
   does not run off the end of what was mapped. A missing check here is how a
   kernel gets talked into reading or writing wherever it is asked to. */
static bool user_range_ok(u32 addr, u32 len) {
    if (len == 0) return true;
    if (addr < USER_MIN) return false;
    if (addr + len < addr) return false;                 /* wrapped */

    u32 dir = paging_current_directory();
    for (u32 a = addr & ~0xFFFu; a < addr + len; a += PAGE_SIZE) {
        u32 phys = virt_to_phys_in(dir, a);
        if (!phys) return false;
    }
    return true;
}

static i32 sys_exit(registers_t *r) {
    (void)r;
    task_exit();
    return 0;
}

static i32 sys_putc(registers_t *r) {
    kputc((char)(r->ebx & 0xFF));
    return 1;
}

static i32 sys_write(registers_t *r) {
    u32 buf = r->ecx;
    u32 len = r->edx;
    if (len > 65536) return -1;
    if (!user_range_ok(buf, len)) return -1;
    const char *p = (const char *)buf;
    for (u32 i = 0; i < len; i++) kputc(p[i]);
    return (i32)len;
}

static i32 sys_getpid(registers_t *r) {
    (void)r;
    task_t *t = task_current();
    return t ? (i32)t->pid : -1;
}

static i32 sys_ticks(registers_t *r) {
    (void)r;
    return (i32)timer_ticks();
}

static i32 sys_sleep(registers_t *r) {
    task_sleep(r->ebx);
    return 0;
}

/* Reads a file into a user buffer. Returns the byte count, or -1. */
static i32 sys_read_file(registers_t *r) {
    u32 name_addr = r->ebx;
    u32 buf = r->ecx;
    u32 cap = r->edx;

    if (!user_range_ok(name_addr, 1) || !user_range_ok(buf, cap)) return -1;

    char name[FS_NAME_MAX];
    const char *src = (const char *)name_addr;
    u32 i = 0;
    for (; i < FS_NAME_MAX - 1; i++) {
        if (!user_range_ok(name_addr + i, 1)) return -1;
        name[i] = src[i];
        if (!name[i]) break;
    }
    name[FS_NAME_MAX - 1] = 0;

    file_t *f = fs_find(name);
    if (!f) return -1;
    u32 n = f->size < cap ? f->size : cap;
    memcpy((void *)buf, f->data, n);
    return (i32)n;
}

/* --- the window server ---------------------------------------------------

   Everything below is reached only through these calls. A program never sees
   a window_t, only a handle it was given, and the handle is checked against
   the caller's pid every time so one program cannot drive another's window. */

static u32 caller_pid(void) {
    task_t *t = task_current();
    return t ? t->pid : 0;
}

static i32 sys_win_create(registers_t *r) {
    u32 name_addr = r->ebx;
    int cw = (int)r->ecx, ch = (int)r->edx;

    char title[32];
    const char *src = (const char *)name_addr;
    u32 i = 0;
    for (; i < sizeof(title) - 1; i++) {
        if (!user_range_ok(name_addr + i, 1)) return -1;
        title[i] = src[i];
        if (!title[i]) break;
    }
    title[sizeof(title) - 1] = 0;

    return winsrv_create(caller_pid(), title, cw, ch);
}

static i32 sys_win_surface(registers_t *r) {
    u32 addr = winsrv_surface(caller_pid(), (int)r->ebx,
                              paging_current_directory());
    return (i32)addr;
}

static i32 sys_win_size(registers_t *r) {
    return winsrv_size(caller_pid(), (int)r->ebx);
}

static i32 sys_win_poll(registers_t *r) {
    u32 out = r->ecx;
    if (!user_range_ok(out, sizeof(wm_event_t))) return -1;

    wm_event_t ev;
    if (!winsrv_poll(caller_pid(), (int)r->ebx, &ev)) return 0;
    memcpy((void *)out, &ev, sizeof(ev));
    return 1;
}

static i32 sys_win_commit(registers_t *r) {
    return winsrv_commit(caller_pid(), (int)r->ebx) ? 0 : -1;
}

static i32 sys_win_close(registers_t *r) {
    return winsrv_close(caller_pid(), (int)r->ebx) ? 0 : -1;
}

static u32 served;
u32 syscall_count(void) { return served; }

typedef i32 (*syscall_fn)(registers_t *);

static const syscall_fn TABLE[] = {
    [SYS_EXIT]      = sys_exit,
    [SYS_PUTC]      = sys_putc,
    [SYS_WRITE]     = sys_write,
    [SYS_GETPID]    = sys_getpid,
    [SYS_TICKS]     = sys_ticks,
    [SYS_SLEEP]     = sys_sleep,
    [SYS_READ_FILE] = sys_read_file,
    [SYS_WIN_CREATE]  = sys_win_create,
    [SYS_WIN_SURFACE] = sys_win_surface,
    [SYS_WIN_SIZE]    = sys_win_size,
    [SYS_WIN_POLL]    = sys_win_poll,
    [SYS_WIN_COMMIT]  = sys_win_commit,
    [SYS_WIN_CLOSE]   = sys_win_close,
};

#define N_SYSCALLS (sizeof(TABLE) / sizeof(TABLE[0]))

static void syscall_handler(registers_t *r) {
    served++;
    u32 n = r->eax;
    if (n >= N_SYSCALLS || !TABLE[n]) {
        r->eax = (u32)-1;
        return;
    }
    r->eax = (u32)TABLE[n](r);
}

void syscall_init(void) {
    register_interrupt_handler(0x80, syscall_handler);
}
