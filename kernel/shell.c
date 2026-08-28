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
#include "blockdev.h"
#include "diskfs.h"
#include "fat.h"
#include "net.h"
#include "netdev.h"
#include "http.h"
#include "mouse.h"
#include "user.h"
#include "wm.h"
#include "apps.h"
#include "fb.h"
#include "elf.h"
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
            "  desktop         windows, a mouse and a paint program\n"
            "  help            this text\n"
            "  ls              list files\n"
            "  cat NAME        print a file\n"
            "  write NAME TEXT create or overwrite a file\n"
            "  append NAME TXT add a line to a file\n"
            "  rm NAME         delete a file\n"
            "  disk            show the attached disk\n"
            "  sync            force a write to disk\n"
            "  format          erase the disk and start clean\n"
            "  net             network status\n"
            "  dhcp            ask the network for an address\n"
            "  ping ADDRESS    ping a host by ip or name\n"
            "  resolve HOST    look up a hostname\n"
            "  fetch HOST [PATH] [FILE]   download a page over http\n"
            "  exec PROGRAM    run an elf program and wait for it\n"
            "  bg PROGRAM      run one in the background\n"
            "  ring3           run the built-in ring 3 test\n"
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
    else if (!strcmp(c, "desktop")) {
        if (!fb_active()) { kprintf("the desktop needs a framebuffer" "\n"); return; }

        /* paint is an ordinary ring 3 program. It is started here and then
           draws on its own, through the window server, while this task runs
           the compositor. */
        file_t *pf = fs_find("paint.elf");
        if (pf) {
            int rc = user_spawn_elf("paint", pf->data, pf->size);
            if (rc < 0) kprintf("desktop: paint: %s" "\n", elf_error(rc));
        }
        app_about();
        wm_run();
        /* the desktop owned the screen; give the console its own back */
        vga_clear();
        kprintf("back at the shell" "\n");
    }
    else if (!strcmp(c, "bg")) {
        if (argc < 2) { kprintf("usage: bg PROGRAM" "\n"); return; }
        file_t *f = fs_find(argv[1]);
        if (!f) { kprintf("bg: %s: no such file" "\n", argv[1]); return; }
        int rc = user_spawn_elf(argv[1], f->data, f->size);
        if (rc > 0) kprintf("[%d] %s running in the background" "\n", rc, argv[1]);
        else kprintf("bg: %s: %s" "\n", argv[1], elf_error(rc));
    } else if (!strcmp(c, "exec")) {
        if (argc < 2) { kprintf("usage: exec PROGRAM" "\n" "e.g. exec hello.elf" "\n"); return; }
        file_t *f = fs_find(argv[1]);
        if (!f) { kprintf("exec: %s: no such file" "\n", argv[1]); return; }
        int rc = user_spawn_elf(argv[1], f->data, f->size);
        if (rc > 0) {
            /* Wait for it, the way a shell does, so its output is not
               interleaved with the next prompt. */
            task_wait((u32)rc);
        } else {
            kprintf("exec: %s: %s" "\n", argv[1], elf_error(rc));
        }
    } else if (!strcmp(c, "ring3")) {
        int pid = user_spawn_stub("ring3");
        if (pid > 0) kprintf("started pid %d in ring 3\n", pid);
        else kprintf("could not start it (%d)\n", pid);
    }
    else if (!strcmp(c, "mouse")) {
        if (!mouse_present()) { kprintf("no mouse\n"); return; }
        kprintf("pointer  %d,%d\n", mouse_x(), mouse_y());
        kprintf("buttons  %s%s%s\n",
                (mouse_buttons() & 1) ? "left " : "",
                (mouse_buttons() & 2) ? "right " : "",
                (mouse_buttons() & 4) ? "middle" : "");
        kprintf("moves    %d\n", mouse_moves());
    }
    else if (!strcmp(c, "sync")) {
        kprintf(diskfs_sync() ? "written to disk\n" : "sync: no disk\n");
    } else if (!strcmp(c, "format")) {
        if (!diskfs_available()) { kprintf("format: no disk attached\n"); return; }
        kprintf(diskfs_format() && diskfs_sync() ? "disk formatted\n" : "format failed\n");
    } else if (!strcmp(c, "disk")) {
        if (!blk_present()) { kprintf("no disk attached\n"); return; }
        kprintf("model    %s\n", blk_model());
        kprintf("size     %d sectors (%d MiB)\n", blk_sectors(), blk_sectors() / 2048);
        kprintf("state    %s\n", diskfs_mounted() ? "mounted" : "not mounted");
        if (fat_mounted()) {
            kprintf("format   FAT16, %d clusters of %d bytes\n",
                    fat_total_clusters(), fat_cluster_bytes());
            kprintf("free     %d KiB\n", fat_free_bytes() / 1024);
        }
    }    else if (!strcmp(c, "net")) {
        if (!net_up()) { kprintf("no network card\n"); return; }
        const u8 *m = net_mac();
        char b[20];
        kprintf("mac      %02x:%02x:%02x:%02x:%02x:%02x\n", m[0],m[1],m[2],m[3],m[4],m[5]);
        if (net_ip()) {
            net_format_ip(net_ip(), b);      kprintf("address  %s\n", b);
            net_format_ip(net_netmask(), b); kprintf("netmask  %s\n", b);
            net_format_ip(net_gateway(), b); kprintf("gateway  %s\n", b);
            net_format_ip(net_dns(), b);     kprintf("dns      %s\n", b);
        } else {
            kprintf("address  none, run: dhcp\n");
        }
        kprintf("packets  %d in, %d out\n", net_rx_packets(), net_tx_packets());
    } else if (!strcmp(c, "dhcp")) {
        if (!net_up()) { kprintf("no network card\n"); return; }
        kprintf("asking for an address...\n");
        if (net_dhcp(6000)) {
            char b[20]; net_format_ip(net_ip(), b);
            kprintf("got %s\n", b);
        } else kprintf("no answer\n");
    } else if (!strcmp(c, "ping")) {
        if (argc < 2) { kprintf("usage: ping ADDRESS\n"); return; }
        if (!net_ip()) { kprintf("no address yet, run: dhcp\n"); return; }
        ipv4_t target = net_parse_ip(argv[1]);
        if (!target) {
            if (!net_resolve(argv[1], &target, 4000)) { kprintf("cannot resolve %s\n", argv[1]); return; }
        }
        char b[20]; net_format_ip(target, b);
        for (int i = 0; i < 4; i++) {
            int ms = net_ping(target, 2000);
            if (ms >= 0) kprintf("reply from %s: seq=%d time=%dms\n", b, i + 1, ms);
            else         kprintf("no reply from %s: seq=%d\n", b, i + 1);
        }
    } else if (!strcmp(c, "fetch")) {
        if (argc < 2) { kprintf("usage: fetch HOST [PATH] [SAVEAS]\n"); return; }
        if (!net_ip()) { kprintf("no address yet, run: dhcp\n"); return; }
        const char *path = argc > 2 ? argv[2] : "/";
        const char *save = argc > 3 ? argv[3] : 0;
        int rc = http_get(argv[1], path, save);
        if (rc == HTTP_ERR_RESOLVE) kprintf("cannot resolve %s\n", argv[1]);
        else if (rc == HTTP_ERR_CONNECT) kprintf("could not connect\n");
        else if (rc < 0) kprintf("fetch failed\n");
    } else if (!strcmp(c, "resolve")) {
        if (argc < 2) { kprintf("usage: resolve HOSTNAME\n"); return; }
        ipv4_t ip;
        if (net_resolve(argv[1], &ip, 4000)) {
            char b[20]; net_format_ip(ip, b);
            kprintf("%s is %s\n", argv[1], b);
        } else kprintf("cannot resolve %s\n", argv[1]);
    }
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
