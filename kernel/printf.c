/* Everything printed goes to the screen and out the serial port, so a
   headless test run sees exactly what a user at the console would. */
#include "printf.h"
#include "vga.h"
#include "serial.h"
#include "string.h"
#include "io.h"

void kputc(char c) {
    vga_putc(c);
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
}
void kputs(const char *s) { while (*s) kputc(*s++); }

static void put_uint(u32 v, u32 base, int upper, int width, char pad) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char buf[36];
    int n = 0;
    if (v == 0) buf[n++] = '0';
    while (v) { buf[n++] = digits[v % base]; v /= base; }
    for (int i = n; i < width; i++) kputc(pad);
    while (n--) kputc(buf[n]);
}

static void put_str(const char *s, int width, bool left) {
    int n = 0;
    for (const char *p = s; *p; p++) n++;
    if (!left) for (int i = n; i < width; i++) kputc(' ');
    while (*s) kputc(*s++);
    if (left) for (int i = n; i < width; i++) kputc(' ');
}

static void put_int(i32 v, int width, char pad) {
    if (v < 0) { kputc('-'); put_uint((u32)(-v), 10, 0, width ? width - 1 : 0, pad); }
    else put_uint((u32)v, 10, 0, width, pad);
}

void kvprintf(const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { kputc(*fmt); continue; }
        fmt++;
        char pad = ' ';
        int width = 0;
        bool left = false;
        if (*fmt == '-') { left = true; fmt++; }
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

        switch (*fmt) {
            case 'd': case 'i': put_int(va_arg(ap, i32), width, pad); break;
            case 'u': put_uint(va_arg(ap, u32), 10, 0, width, pad); break;
            case 'x': put_uint(va_arg(ap, u32), 16, 0, width, pad); break;
            case 'X': put_uint(va_arg(ap, u32), 16, 1, width, pad); break;
            case 'b': put_uint(va_arg(ap, u32), 2, 0, width, pad); break;
            case 'p': kputs("0x"); put_uint((u32)(uintptr_t)va_arg(ap, void *), 16, 0, 8, '0'); break;
            case 'c': kputc((char)va_arg(ap, int)); break;
            case 's': { const char *s = va_arg(ap, const char *); put_str(s ? s : "(null)", width, left); break; }
            case '%': kputc('%'); break;
            default: kputc('%'); kputc(*fmt); break;
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); kvprintf(fmt, ap); va_end(ap);
}

void panic(const char *fmt, ...) {
    cli();
    vga_set_color(VGA_WHITE, VGA_RED);
    kprintf("\n*** KERNEL PANIC ***\n");
    va_list ap; va_start(ap, fmt); kvprintf(fmt, ap); va_end(ap);
    kprintf("\nSystem halted.\n");
    for (;;) hlt();
}
