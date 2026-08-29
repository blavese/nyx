/* The entire user-facing interface: thirty-one system calls and a little
   sugar. There is no libc here, and nothing is linked in from the kernel;
   every call below crosses the ring boundary through int 0x80. */
#pragma once

typedef unsigned int   u32;
typedef unsigned short u16;
typedef unsigned char  u8;
typedef int            i32;

#define SYS_EXIT       0
#define SYS_PUTC       1
#define SYS_WRITE      2
#define SYS_GETPID     3
#define SYS_TICKS      4
#define SYS_SLEEP      5
#define SYS_READ_FILE  6

#define SYS_WIN_CREATE   7
#define SYS_WIN_SURFACE  8
#define SYS_WIN_SIZE     9
#define SYS_WIN_POLL    10
#define SYS_WIN_COMMIT  11
#define SYS_WIN_CLOSE   12

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

#define SYS_CONNECT     25
#define SYS_SEND        26
#define SYS_RECV        27
#define SYS_DISCONNECT  28
#define SYS_RESOLVE     29
#define SYS_NETINFO     30

static inline int syscall(int n, int a, int b, int c) {
    int r;
    __asm__ volatile ("int $0x80"
                      : "=a"(r)
                      : "a"(n), "b"(a), "c"(b), "d"(c)
                      : "memory");
    return r;
}

static inline void exit(int code)        { syscall(SYS_EXIT, code, 0, 0); }
static inline void putc(char ch)         { syscall(SYS_PUTC, ch, 0, 0); }
static inline int  getpid(void)          { return syscall(SYS_GETPID, 0, 0, 0); }
static inline int  ticks(void)           { return syscall(SYS_TICKS, 0, 0, 0); }
static inline void sleep_ms(int ms)      { syscall(SYS_SLEEP, ms, 0, 0); }

static inline int write(const char *s, int len) {
    return syscall(SYS_WRITE, 0, (int)s, len);
}

static inline int read_file(const char *name, char *buf, int cap) {
    return syscall(SYS_READ_FILE, (int)name, (int)buf, cap);
}

/* --- strings -------------------------------------------------------------

   Enough of a string library to write a program with. There is no libc to
   link against, so this is all of it. */

static inline int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static inline int strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

static inline void strcpy(char *d, const char *s) {
    while ((*d++ = *s++)) { }
}

static inline void strncpy(char *d, const char *s, int n) {
    int i = 0;
    for (; i < n - 1 && s[i]; i++) d[i] = s[i];
    if (n > 0) d[i] = 0;
}

static inline void *memset(void *p, int v, int n) {
    unsigned char *b = (unsigned char *)p;
    for (int i = 0; i < n; i++) b[i] = (unsigned char)v;
    return p;
}

static inline void *memcpy(void *d, const void *s, int n) {
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    for (int i = 0; i < n; i++) a[i] = b[i];
    return d;
}

static inline void puts(const char *s) { write(s, strlen(s)); }

static inline void putn(int v) {
    char buf[12];
    int n = 0;
    if (v < 0) { putc('-'); v = -v; }
    if (v == 0) { putc('0'); return; }
    while (v) { buf[n++] = (char)('0' + v % 10); v /= 10; }
    while (n--) putc(buf[n]);
}

/* Formats an unsigned value into `out`, returning its length. */
static inline int utoa(u32 v, char *out) {
    char tmp[12];
    int n = 0;
    if (!v) { out[0] = '0'; out[1] = 0; return 1; }
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
    return n;
}

/* --- files ---------------------------------------------------------------

   Paths may be relative; the kernel joins them to this program's working
   directory, which it inherited from whoever started it. */

#define O_READ   0x01
#define O_WRITE  0x02
#define O_CREATE 0x04
#define O_TRUNC  0x08
#define O_APPEND 0x10

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct {
    u32  size;
    u32  is_dir;
    char name[32];
} nyx_stat;

static inline int open(const char *path, u32 flags) {
    return syscall(SYS_OPEN, (int)path, (int)flags, 0);
}
static inline int close(int fd)  { return syscall(SYS_CLOSE, fd, 0, 0); }

static inline int fread(int fd, void *buf, int len) {
    return syscall(SYS_FREAD, fd, (int)buf, len);
}
static inline int fwrite(int fd, const void *buf, int len) {
    return syscall(SYS_FWRITE, fd, (int)buf, len);
}
static inline int seek(int fd, int off, int whence) {
    return syscall(SYS_SEEK, fd, off, whence);
}

static inline int unlink(const char *path) { return syscall(SYS_UNLINK, (int)path, 0, 0); }
static inline int mkdir(const char *path)  { return syscall(SYS_MKDIR, (int)path, 0, 0); }
static inline int rmdir(const char *path)  { return syscall(SYS_RMDIR, (int)path, 0, 0); }

/* Returns 1 when an entry was produced, 0 past the end, -1 on error. */
static inline int readdir(const char *path, int index, nyx_stat *out) {
    return syscall(SYS_READDIR, (int)path, index, (int)out);
}
static inline int stat(const char *path, nyx_stat *out) {
    return syscall(SYS_STAT, (int)path, (int)out, 0);
}
static inline int chdir(const char *path) { return syscall(SYS_CHDIR, (int)path, 0, 0); }
static inline int getcwd(char *buf, int cap) {
    return syscall(SYS_GETCWD, (int)buf, cap, 0);
}

/* Reads a whole file into a caller-supplied buffer. Returns the length. */
static inline int slurp(const char *path, char *buf, int cap) {
    int fd = open(path, O_READ);
    if (fd < 0) return -1;
    int total = 0;
    for (;;) {
        int n = fread(fd, buf + total, cap - total);
        if (n <= 0) break;
        total += n;
        if (total >= cap) break;
    }
    close(fd);
    return total;
}

static inline int spit(const char *path, const void *buf, int len) {
    int fd = open(path, O_WRITE | O_CREATE | O_TRUNC);
    if (fd < 0) return -1;
    int n = fwrite(fd, buf, len);
    close(fd);
    return n;
}

/* --- the network ---------------------------------------------------------

   One connection at a time, because that is what the kernel's TCP supports.
   `host` may be a name or a dotted address. */

typedef struct {
    u32 up;
    u32 ip, gateway, netmask, dns;
    u8  mac[6];
    u16 pad;
} nyx_netinfo;

static inline int connect(const char *host, int port) {
    return syscall(SYS_CONNECT, (int)host, port, 0);
}
static inline int send(const void *buf, int len) {
    return syscall(SYS_SEND, 0, (int)buf, len);
}
static inline int recv(void *buf, int len) {
    return syscall(SYS_RECV, 0, (int)buf, len);
}
static inline int disconnect(void) { return syscall(SYS_DISCONNECT, 0, 0, 0); }

static inline int resolve(const char *host, u32 *out) {
    return syscall(SYS_RESOLVE, (int)host, (int)out, 0);
}
static inline int netinfo(nyx_netinfo *out) {
    return syscall(SYS_NETINFO, (int)out, 0, 0);
}

/* --- windows -------------------------------------------------------------

   A window is a handle and a block of pixels the kernel maps into this
   program's address space. Draw into the pixels, call win_commit, and the
   window manager puts them on the screen. */

#define WIN_EV_NONE  0
#define WIN_EV_MOUSE 1
#define WIN_EV_KEY   2
#define WIN_EV_CLOSE 3

/* Bit 7 of buttons is set on the event that started a press, so a program
   can tell a new stroke from the middle of one. */
#define WIN_BTN_LEFT  0x01
#define WIN_BTN_RIGHT 0x02
#define WIN_BTN_DOWN  0x80

typedef struct {
    u32 type;
    i32 x, y;
    u32 buttons;
    u32 key;
} win_event;

static inline int win_create(const char *title, int w, int h) {
    return syscall(SYS_WIN_CREATE, (int)title, w, h);
}

/* The address of this window's pixels, row major, one u32 per pixel. */
static inline u32 *win_surface(int handle) {
    return (u32 *)syscall(SYS_WIN_SURFACE, handle, 0, 0);
}

static inline int win_width(int handle) {
    int v = syscall(SYS_WIN_SIZE, handle, 0, 0);
    return v < 0 ? -1 : ((v >> 16) & 0xFFFF);
}

static inline int win_height(int handle) {
    int v = syscall(SYS_WIN_SIZE, handle, 0, 0);
    return v < 0 ? -1 : (v & 0xFFFF);
}

static inline int win_poll(int handle, win_event *ev) {
    return syscall(SYS_WIN_POLL, handle, (int)ev, 0);
}

static inline int win_commit(int handle) {
    return syscall(SYS_WIN_COMMIT, handle, 0, 0);
}

static inline int win_close(int handle) {
    return syscall(SYS_WIN_CLOSE, handle, 0, 0);
}

#define RGB(r, g, b) (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
