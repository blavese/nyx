/* Interactive shell. Reads from the PS/2 keyboard or the serial line,
   whichever produces a character first, so it can be driven by a person at
   the console or by a script piping into QEMU. */
#include "shell.h"
#include "printf.h"
#include "string.h"
#include "keyboard.h"
#include "vga.h"
#include "heap.h"
#include "pmm.h"
#include "timer.h"
#include "sched.h"
#include "fs.h"
#include "io.h"
#include "welcome.h"
#include "serial.h"

#define LINE_MAX 256
#define ARG_MAX  16

static char line[LINE_MAX];

static u32 split(char *s, char **argv, u32 max) {
    u32 n = 0;
    while (*s && n < max) {
        while (*s == ' ') *s++ = 0;
        if (!*s) break;
        argv[n++] = s;
        while (*s && *s != ' ') s++;
    }
    return n;
}

static void cmd_help(void) {
    kprintf("commands:\n"
            "  guide           a short tour, start here\n"
            "  help            this text\n"
            "  ls              list files\n"
            "  cat NAME        print a file\n"
            "  write NAME TEXT create or overwrite a file\n"
            "  append NAME TXT add a line to a file\n"
            "  rm NAME         delete a file\n"
            "  ps              list tasks\n"
            "  mem             memory usage\n"
            "  uptime          time since boot\n"
            "  uname           kernel identity\n"
            "  echo TEXT       print text\n"
            "  clear           clear the screen\n"
            "  spawn           start a background counter task\n"
            "  fault           deliberately divide by zero\n"
            "  reboot          reset the machine\n");
}

static void cmd_ls(void) {
    u32 n = fs_count();
    if (!n) { kprintf("(no files)\n"); return; }
    for (u32 i = 0; i < n; i++) {
        file_t *f = fs_at(i);
        if (f) kprintf("  %6d  %s\n", f->size, f->name);
    }
    kprintf("%d file(s), %d bytes\n", n, fs_bytes_used());
}

static void cmd_cat(const char *name) {
    file_t *f = fs_find(name);
    if (!f) { kprintf("cat: %s: no such file\n", name); return; }
    for (u32 i = 0; i < f->size; i++) kputc((char)f->data[i]);
    if (f->size && f->data[f->size - 1] != '\n') kputc('\n');
}

static void join_from(char **argv, u32 argc, u32 start, char *out, u32 cap) {
    u32 o = 0;
    for (u32 i = start; i < argc && o < cap - 1; i++) {
        if (i > start && o < cap - 1) out[o++] = ' ';
        for (const char *p = argv[i]; *p && o < cap - 1; p++) out[o++] = *p;
    }
    out[o] = 0;
}

static void cmd_ps(void) {
    task_t *t = task_list();
    if (!t) { kprintf("(no tasks)\n"); return; }
    kprintf("  PID  STATE     SLICES  NAME\n");
    const char *st[] = { "ready", "running", "sleeping", "dead" };
    task_t *p = t;
    do {
        kprintf("  %3d  %-8s  %6d  %s\n", p->pid, st[p->state], p->slices, p->name);
        p = p->next;
    } while (p != t);
}

static void cmd_mem(void) {
    kprintf("physical: %d KiB total, %d KiB used, %d KiB free\n",
            pmm_total_frames() * 4, pmm_used_frames() * 4, pmm_free_frames() * 4);
    kprintf("heap:     %d KiB total, %d bytes used\n", heap_total() / 1024, heap_used());
    kprintf("files:    %d using %d bytes\n", fs_count(), fs_bytes_used());
    kprintf("serial:   irqs=%d got=%d read=%d dropped=%d\n",
            serial_isr_calls(), serial_isr_bytes(), serial_read_bytes(), serial_overruns());
}

static void cmd_uptime(void) {
    u64 t = timer_ticks();
    u32 secs = (u32)(t / timer_hz());
    kprintf("up %d.%02d seconds (%d ticks at %d Hz)\n",
            secs, (u32)((t % timer_hz()) * 100 / timer_hz()), (u32)t, timer_hz());
}

static volatile u32 spawn_n = 0;
static void counter_task(void) {
    for (int i = 0; i < 3; i++) { spawn_n++; task_sleep(400); }
    task_exit();
}

static void execute(char *buf) {
    char *argv[ARG_MAX];
    u32 argc = split(buf, argv, ARG_MAX);
    if (!argc) return;
    const char *c = argv[0];

    if (!strcmp(c, "help")) cmd_help();
    else if (!strcmp(c, "guide")) guide_print();
    else if (!strcmp(c, "ls")) cmd_ls();
    else if (!strcmp(c, "cat")) {
        if (argc < 2) kprintf("usage: cat NAME\n"); else cmd_cat(argv[1]);
    } else if (!strcmp(c, "write") || !strcmp(c, "append")) {
        if (argc < 3) { kprintf("usage: %s NAME TEXT\n", c); return; }
        char text[LINE_MAX];
        join_from(argv, argc, 2, text, sizeof(text));
        u32 len = (u32)strlen(text);
        text[len++] = '\n';
        bool ok = (c[0] == 'w') ? fs_write(argv[1], text, len)
                                : fs_append(argv[1], text, len);
        kprintf(ok ? "ok\n" : "failed\n");
    } else if (!strcmp(c, "rm")) {
        if (argc < 2) kprintf("usage: rm NAME\n");
        else kprintf(fs_delete(argv[1]) ? "ok\n" : "rm: no such file\n");
    } else if (!strcmp(c, "ps")) cmd_ps();
    else if (!strcmp(c, "mem")) cmd_mem();
    else if (!strcmp(c, "uptime")) cmd_uptime();
    else if (!strcmp(c, "uname")) kprintf("%s %s i686\n", KERNEL_NAME, KERNEL_VERSION);
    else if (!strcmp(c, "clear")) vga_clear();
    else if (!strcmp(c, "echo")) {
        char text[LINE_MAX];
        join_from(argv, argc, 1, text, sizeof(text));
        kprintf("%s\n", text);
    } else if (!strcmp(c, "spawn")) {
        task_t *t = task_create("counter", counter_task);
        kprintf(t ? "spawned pid %d\n" : "spawn failed\n", t ? t->pid : 0);
    } else if (!strcmp(c, "fault")) {
        kprintf("dividing by zero...\n");
        volatile int z = 0;
        volatile int x = 1 / z;
        (void)x;
    } else if (!strcmp(c, "reboot")) {
        kprintf("rebooting\n");
        u8 t = 0x02;
        while (t & 0x02) t = inb(0x64);
        outb(0x64, 0xFE);
    } else {
        kprintf("%s: not found (try help)\n", c);
    }
}

void shell_task(void) {
    welcome_print();
    u32 len = 0;
    kprintf("nyx> ");
    for (;;) {
        int ch = kbd_trygetchar();
        if (ch < 0) { hlt(); continue; }   /* woken by the timer or a key */
        char c = (char)ch;

        if (c == '\n') {
            kputc('\n');
            line[len] = 0;
            execute(line);
            len = 0;
            kprintf("nyx> ");
        } else if (c == '\b') {
            if (len) { len--; kputc('\b'); }
        } else if (c >= ' ' && c < 127 && len < LINE_MAX - 2) {
            line[len++] = c;
            kputc(c);
        }
    }
}
