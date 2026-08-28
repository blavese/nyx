/* The entire user-facing interface: thirteen system calls and a little sugar.
   There is no libc here, and nothing is linked in from the kernel; every
   call below crosses the ring boundary through int 0x80. */
#pragma once

typedef unsigned int   u32;
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

static inline int strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
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
