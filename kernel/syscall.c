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
#include "vfs.h"
#include "net.h"
#include "tcp.h"
#include "heap.h"
#include "smp.h"
#include "pmm.h"
#include "fb.h"
#include "fat.h"

/* User space is everything above the kernel's identity mapped region. */
#define USER_MIN (KERNEL_SPACE_MB * 1024u * 1024u)

/* Confirms a user buffer is one the caller could have reached on its own:
   above the kernel, not wrapped, and mapped user-accessible for its whole
   length. A missing check here is how a kernel gets talked into reading or
   writing wherever it is asked to.

   Merely being mapped is not enough. Every address space inherits the
   kernel's high mappings, so the framebuffer aperture and the register
   windows of the disk and network controllers are all present in a user
   directory; they are simply not reachable from ring 3 because their pages
   lack the user bit. Checking presence alone would let a program name one of
   those addresses and have the kernel write to it on the program's behalf. */
static bool user_range_ok(u32 addr, u32 len) {
    if (len == 0) return true;
    if (addr < USER_MIN) return false;
    if (addr + len < addr) return false;                 /* wrapped */

    u32 dir = paging_current_directory();
    for (u32 a = addr & ~0xFFFu; a < addr + len; a += PAGE_SIZE) {
        if (!virt_is_user_in(dir, a)) return false;
    }
    return true;
}

/* Whose call this is. Used everywhere a handle has to be checked against
   its owner, so one program cannot drive another's window or socket. */
static u32 caller_pid(void) {
    task_t *t = task_current();
    return t ? t->pid : 0;
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

    return vfs_read(name, (void *)buf, cap);
}

/* --- files ---------------------------------------------------------------

   Every path arrives as a user pointer, so it is copied into the kernel
   before anything looks at it. A path that is still in user memory can be
   changed by another thread between the check and the use. */

static bool copy_path(u32 addr, char *out, u32 cap) {
    for (u32 i = 0; i < cap; i++) {
        if (!user_range_ok(addr + i, 1)) return false;
        out[i] = ((const char *)addr)[i];
        if (!out[i]) return true;
    }
    return false;                       /* no terminator inside the limit */
}

static i32 sys_open(registers_t *r) {
    char path[VFS_PATH_MAX];
    if (!copy_path(r->ebx, path, sizeof(path))) return -1;
    return vfs_open(path, r->ecx);
}

static i32 sys_close(registers_t *r) {
    return vfs_close((int)r->ebx) ? 0 : -1;
}

static i32 sys_fread(registers_t *r) {
    u32 buf = r->ecx, len = r->edx;
    if (len > 1024 * 1024) return -1;
    if (!user_range_ok(buf, len)) return -1;
    return vfs_fd_read((int)r->ebx, (void *)buf, len);
}

static i32 sys_fwrite(registers_t *r) {
    u32 buf = r->ecx, len = r->edx;
    if (len > 1024 * 1024) return -1;
    if (!user_range_ok(buf, len)) return -1;
    return vfs_fd_write((int)r->ebx, (const void *)buf, len);
}

static i32 sys_seek(registers_t *r) {
    return vfs_fd_seek((int)r->ebx, (i32)r->ecx, r->edx);
}

static i32 sys_unlink(registers_t *r) {
    char path[VFS_PATH_MAX];
    if (!copy_path(r->ebx, path, sizeof(path))) return -1;
    return vfs_delete(path) ? 0 : -1;
}

static i32 sys_mkdir(registers_t *r) {
    char path[VFS_PATH_MAX];
    if (!copy_path(r->ebx, path, sizeof(path))) return -1;
    return vfs_mkdir(path) ? 0 : -1;
}

static i32 sys_rmdir(registers_t *r) {
    char path[VFS_PATH_MAX];
    if (!copy_path(r->ebx, path, sizeof(path))) return -1;
    return vfs_rmdir(path) ? 0 : -1;
}

static i32 sys_readdir(registers_t *r) {
    char path[VFS_PATH_MAX];
    if (!copy_path(r->ebx, path, sizeof(path))) return -1;
    if (!user_range_ok(r->edx, sizeof(nyx_stat_t))) return -1;

    nyx_stat_t st;
    memset(&st, 0, sizeof(st));
    bool is_dir = false;
    int rc = vfs_list(path, r->ecx, st.name, &st.size, &is_dir);
    if (rc != 1) return rc < 0 ? -1 : 0;
    st.is_dir = is_dir ? 1 : 0;
    memcpy((void *)r->edx, &st, sizeof(st));
    return 1;
}

static i32 sys_stat(registers_t *r) {
    char path[VFS_PATH_MAX];
    if (!copy_path(r->ebx, path, sizeof(path))) return -1;
    if (!user_range_ok(r->ecx, sizeof(nyx_stat_t))) return -1;

    nyx_stat_t st;
    memset(&st, 0, sizeof(st));
    bool is_dir = false;
    if (!vfs_stat(path, &st.size, &is_dir)) return -1;
    st.is_dir = is_dir ? 1 : 0;
    memcpy((void *)r->ecx, &st, sizeof(st));
    return 0;
}

static i32 sys_chdir(registers_t *r) {
    char path[VFS_PATH_MAX];
    if (!copy_path(r->ebx, path, sizeof(path))) return -1;
    return vfs_chdir(path) ? 0 : -1;
}

static i32 sys_getcwd(registers_t *r) {
    u32 buf = r->ebx, cap = r->ecx;
    if (cap == 0 || cap > VFS_PATH_MAX) return -1;
    if (!user_range_ok(buf, cap)) return -1;
    const char *at = vfs_cwd();
    u32 n = (u32)strlen(at);
    if (n + 1 > cap) return -1;
    memcpy((void *)buf, at, n + 1);
    return (i32)n;
}

/* --- sockets -------------------------------------------------------------

   The TCP stack handles one connection at a time, so there is one socket and
   it belongs to whoever opened it. That is a real limit rather than a
   simplification of the interface: two programs cannot both be connected. */

static u32 sock_owner;
static bool sock_open;

static i32 sys_connect(registers_t *r) {
    char host[128];
    if (!copy_path(r->ebx, host, sizeof(host))) return -1;
    u16 port = (u16)r->ecx;
    if (!port) return -1;
    if (!net_up()) return -1;
    if (sock_open) return -1;              /* already in use */

    ipv4_t ip = net_parse_ip(host);
    if (!ip && !net_resolve(host, &ip, 6000)) return -1;
    if (!tcp_connect(ip, port, 6000)) return -1;

    sock_owner = caller_pid();
    sock_open = true;
    return 0;
}

static i32 sys_send(registers_t *r) {
    if (!sock_open || sock_owner != caller_pid()) return -1;
    u32 buf = r->ecx, len = r->edx;
    if (len == 0 || len > 1400) return -1;
    if (!user_range_ok(buf, len)) return -1;
    return tcp_send((const void *)buf, (u16)len) ? (i32)len : -1;
}

static i32 sys_recv(registers_t *r) {
    if (!sock_open || sock_owner != caller_pid()) return -1;
    u32 buf = r->ecx, len = r->edx;
    if (len == 0 || len > 65536) return -1;
    if (!user_range_ok(buf, len)) return -1;
    return (i32)tcp_recv((u8 *)buf, len, 4000);
}

static i32 sys_disconnect(registers_t *r) {
    (void)r;
    if (!sock_open || sock_owner != caller_pid()) return -1;
    tcp_close();
    sock_open = false;
    return 0;
}

/* Frees the socket when its owner dies, so a crashed program does not lock
   the only connection the machine has. */
void syscall_release(u32 pid) {
    if (sock_open && sock_owner == pid) { tcp_close(); sock_open = false; }
}

static i32 sys_resolve(registers_t *r) {
    char host[128];
    if (!copy_path(r->ebx, host, sizeof(host))) return -1;
    if (!user_range_ok(r->ecx, 4)) return -1;
    if (!net_up()) return -1;

    ipv4_t ip = net_parse_ip(host);
    if (!ip && !net_resolve(host, &ip, 6000)) return -1;
    *(u32 *)r->ecx = ip;
    return 0;
}

static i32 sys_netinfo(registers_t *r) {
    if (!user_range_ok(r->ebx, sizeof(nyx_netinfo_t))) return -1;
    nyx_netinfo_t info;
    memset(&info, 0, sizeof(info));
    info.up = net_up() ? 1 : 0;
    if (info.up) {
        info.ip = net_ip();
        info.gateway = net_gateway();
        info.netmask = net_netmask();
        info.dns = net_dns();
        memcpy(info.mac, net_mac(), 6);
    }
    memcpy((void *)r->ebx, &info, sizeof(info));
    return 0;
}

/* --- the window server ---------------------------------------------------

   Everything below is reached only through these calls. A program never sees
   a window_t, only a handle it was given, and the handle is checked against
   the caller's pid every time so one program cannot drive another's window. */

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

static i32 sys_sysinfo(registers_t *r) {
    if (!user_range_ok(r->ebx, sizeof(nyx_sysinfo_t))) return -1;

    nyx_sysinfo_t info;
    memset(&info, 0, sizeof(info));
    info.cpus_found = smp_cpu_count();
    info.cpus_started = smp_started();
    info.mem_total_kb = pmm_total_frames() * 4;
    info.mem_used_kb = pmm_used_frames() * 4;
    info.mem_free_kb = pmm_free_frames() * 4;
    info.heap_total_kb = heap_total() / 1024;
    info.uptime_seconds = (u32)(timer_ticks() / timer_hz());
    info.tasks = task_count();
    info.screen_w = fb_active() ? fb_width() : 0;
    info.screen_h = fb_active() ? fb_height() : 0;
    info.syscalls = syscall_count();
    info.disk_kb_free = fat_mounted() ? fat_free_bytes() / 1024 : 0;

    memcpy((void *)r->ebx, &info, sizeof(info));
    return 0;
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
    [SYS_OPEN]        = sys_open,
    [SYS_CLOSE]       = sys_close,
    [SYS_FREAD]       = sys_fread,
    [SYS_FWRITE]      = sys_fwrite,
    [SYS_SEEK]        = sys_seek,
    [SYS_UNLINK]      = sys_unlink,
    [SYS_MKDIR]       = sys_mkdir,
    [SYS_RMDIR]       = sys_rmdir,
    [SYS_READDIR]     = sys_readdir,
    [SYS_STAT]        = sys_stat,
    [SYS_CHDIR]       = sys_chdir,
    [SYS_GETCWD]      = sys_getcwd,
    [SYS_CONNECT]     = sys_connect,
    [SYS_SEND]        = sys_send,
    [SYS_RECV]        = sys_recv,
    [SYS_DISCONNECT]  = sys_disconnect,
    [SYS_RESOLVE]     = sys_resolve,
    [SYS_NETINFO]     = sys_netinfo,
    [SYS_SYSINFO]     = sys_sysinfo,
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
