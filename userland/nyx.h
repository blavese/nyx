/* The entire user-facing interface: seven system calls and a little sugar.
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
