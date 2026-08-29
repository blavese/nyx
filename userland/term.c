/* A terminal, running in ring 3.
 *
 * This is a shell that is not part of the kernel. It has a window, a
 * scrollback buffer and a command set, and every single thing it does goes
 * through int 0x80: listing a directory, reading a file, writing one, and
 * fetching a page over TCP. The kernel's own shell still exists on the
 * serial console, but nothing here calls into it.
 *
 * It is deliberately not a terminal emulator. There is no pty and no job
 * control; commands run inline and print into the buffer. */
#include "nyx.h"
#include "draw.h"

#define COLS      160
#define MAX_LINES 240
#define PAD       8

/* Roughly one terminal's worth of file. Anything larger is printed in full
   but never held in one piece. */
#define IO_BUF 16384

static const u32 BG      = RGB(0x10, 0x14, 0x1A);
static const u32 FG      = RGB(0xC8, 0xD2, 0xDA);
static const u32 DIM     = RGB(0x6B, 0x7A, 0x87);
static const u32 ACCENT  = RGB(0x5E, 0xD1, 0xA0);
static const u32 WARN    = RGB(0xE0, 0x7A, 0x6A);
static const u32 CURSOR  = RGB(0x5E, 0xD1, 0xA0);

/* The scrollback. Each line carries its own colour so errors read as errors
   without an escape sequence parser. */
static char lines[MAX_LINES][COLS + 1];
static u32  colours[MAX_LINES];
static int  n_lines;            /* how many are in use, up to MAX_LINES */
static int  first;              /* index of the oldest line */
static int  view;               /* how many lines scrolled back from the end */

static surface scr;
static int win;
static int rows, cols;

static char input[COLS + 1];
static int  in_len;
static int  blink;

/* --- the buffer --------------------------------------------------------- */

static char *line_at(int i) { return lines[(first + i) % MAX_LINES]; }
static u32  *colour_at(int i) { return &colours[(first + i) % MAX_LINES]; }

static void push_line(const char *s, u32 colour) {
    int idx;
    if (n_lines < MAX_LINES) {
        idx = (first + n_lines) % MAX_LINES;
        n_lines++;
    } else {
        idx = first;
        first = (first + 1) % MAX_LINES;
    }
    strncpy(lines[idx], s, COLS + 1);
    colours[idx] = colour;
}

/* Prints text, breaking it at newlines and wrapping anything too wide. */
static void print(const char *s, u32 colour) {
    char buf[COLS + 1];
    int n = 0;
    for (int i = 0; ; i++) {
        char c = s[i];
        if (c == '\n' || c == 0 || n == cols) {
            buf[n] = 0;
            push_line(buf, colour);
            n = 0;
            if (c == 0) return;
            if (c == '\n') continue;
        }
        if (c == '\t') {
            while (n < cols && (n % 4)) buf[n++] = ' ';
            if (n < cols) buf[n++] = ' ';
            continue;
        }
        if (c < 32 || c > 126) continue;
        buf[n++] = c;
    }
}

static void say(const char *s) { print(s, FG); }
static void dim(const char *s) { print(s, DIM); }
static void err(const char *s) { print(s, WARN); }

/* Building a line out of pieces, which is most of what the commands do. */
static char work[COLS + 1];
static int  work_n;

static void w_reset(void) { work_n = 0; work[0] = 0; }
static void w_str(const char *s) {
    for (int i = 0; s[i] && work_n < COLS; i++) work[work_n++] = s[i];
    work[work_n] = 0;
}
static void w_num(u32 v) {
    char tmp[12];
    utoa(v, tmp);
    w_str(tmp);
}
static void w_pad(int to) {
    while (work_n < to && work_n < COLS) work[work_n++] = ' ';
    work[work_n] = 0;
}

/* --- rendering ---------------------------------------------------------- */

static void draw_all(void) {
    fill(&scr, BG);

    /* The last row belongs to the prompt, so the scrollback gets the rest. */
    int text_rows = rows - 1;
    int end = n_lines - view;
    if (end < 0) end = 0;
    int start = end - text_rows;
    if (start < 0) start = 0;

    int y = PAD;
    for (int i = start; i < end; i++) {
        text(&scr, PAD, y, line_at(i), *colour_at(i));
        y += FONT_H;
    }

    /* The prompt sits on the bottom row, always visible. */
    int py = PAD + text_rows * FONT_H;
    rect(&scr, 0, py - 3, scr.w, 1, RGB(0x1E, 0x26, 0x2F));

    char cwd[128];
    if (getcwd(cwd, sizeof(cwd)) < 0) strcpy(cwd, "/");
    text(&scr, PAD, py, cwd, ACCENT);
    int x = PAD + (strlen(cwd) + 1) * FONT_W;
    glyph(&scr, x - FONT_W, py, ' ', FG);
    text(&scr, x, py, "> ", DIM);
    x += 2 * FONT_W;
    text(&scr, x, py, input, FG);

    if (view == 0 && (blink / 12) % 2 == 0)
        rect(&scr, x + in_len * FONT_W, py, 2, FONT_H, CURSOR);

    if (view > 0) {
        const char *note = " scrolled back, End returns ";
        int tw = strlen(note) * FONT_W;
        rect(&scr, scr.w - tw - PAD, PAD - 2, tw, FONT_H + 2, RGB(0x23, 0x2D, 0x38));
        text(&scr, scr.w - tw - PAD, PAD, note, DIM);
    }
}

/* --- commands ----------------------------------------------------------- */

static int split(char *s, char **argv, int max) {
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ') *s++ = 0;
        if (!*s) break;
        argv[n++] = s;
        while (*s && *s != ' ') s++;
    }
    return n;
}

static void join_from(char **argv, int argc, int from, char *out, int cap) {
    int o = 0;
    for (int i = from; i < argc && o < cap - 1; i++) {
        if (i > from && o < cap - 1) out[o++] = ' ';
        for (const char *p = argv[i]; *p && o < cap - 1; p++) out[o++] = *p;
    }
    out[o] = 0;
}

static void cmd_help(void) {
    dim("commands");
    say("  ls [PATH]        list a directory");
    say("  cd [PATH]        change directory");
    say("  pwd              where you are");
    say("  cat FILE         print a file");
    say("  write FILE TEXT  create or overwrite");
    say("  append FILE TEXT add a line");
    say("  rm FILE          delete a file");
    say("  mkdir NAME       make a directory");
    say("  rmdir NAME       remove an empty one");
    say("  stat PATH        size and kind");
    say("  net              network status");
    say("  get HOST [PATH] [FILE]   download over http");
    say("  echo TEXT        print it back");
    say("  clear            empty the scrollback");
    say("  about            what this program is");
    dim("PageUp and PageDown scroll. Escape leaves the desktop.");
}

static void cmd_ls(const char *path) {
    const char *where = path ? path : ".";
    nyx_stat st;
    int files = 0, dirs = 0;
    u32 bytes = 0;

    for (int i = 0; ; i++) {
        if (readdir(where, i, &st) != 1) break;
        w_reset();
        if (st.is_dir) {
            w_str("      <dir>  ");
            w_str(st.name);
            w_str("/");
            print(work, ACCENT);
            dirs++;
        } else {
            char num[12];
            int n = utoa(st.size, num);
            for (int k = n; k < 10; k++) w_str(" ");
            w_str(num);
            w_str("  ");
            w_str(st.name);
            say(work);
            files++;
            bytes += st.size;
        }
    }

    if (!files && !dirs) { dim("(empty)"); return; }
    w_reset();
    w_num((u32)files); w_str(" file(s) in "); w_num(bytes); w_str(" bytes");
    if (dirs) { w_str(", "); w_num((u32)dirs); w_str(" director"); w_str(dirs == 1 ? "y" : "ies"); }
    dim(work);
}

static void cmd_cat(const char *path) {
    nyx_stat st;
    if (stat(path, &st) != 0) { err("no such file"); return; }
    if (st.is_dir) { err("that is a directory"); return; }

    static char buf[IO_BUF];
    int fd = open(path, O_READ);
    if (fd < 0) { err("cannot open it"); return; }

    for (;;) {
        int n = fread(fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = 0;
        print(buf, FG);
    }
    close(fd);
}

static void cmd_stat(const char *path) {
    nyx_stat st;
    if (stat(path, &st) != 0) { err("no such path"); return; }
    w_reset();
    w_str(path);
    w_pad(24);
    w_str(st.is_dir ? "directory" : "file, ");
    if (!st.is_dir) { w_num(st.size); w_str(" bytes"); }
    say(work);
}

static void cmd_net(void) {
    nyx_netinfo info;
    if (netinfo(&info) != 0 || !info.up) { err("no network"); return; }

    static const char *labels[4] = { "address  ", "gateway  ", "netmask  ", "resolver " };
    u32 values[4] = { info.ip, info.gateway, info.netmask, info.dns };
    for (int i = 0; i < 4; i++) {
        w_reset();
        w_str(labels[i]);
        for (int b = 3; b >= 0; b--) {
            w_num((values[i] >> (b * 8)) & 0xFF);
            if (b) w_str(".");
        }
        say(work);
    }

    w_reset();
    w_str("mac      ");
    for (int i = 0; i < 6; i++) {
        const char *hex = "0123456789abcdef";
        char pair[3] = { hex[info.mac[i] >> 4], hex[info.mac[i] & 15], 0 };
        w_str(pair);
        if (i < 5) w_str(":");
    }
    say(work);
}

/* An HTTP GET, done entirely from user space through the socket calls. */
static void cmd_get(int argc, char **argv) {
    if (argc < 2) { err("usage: get HOST [PATH] [FILE]"); return; }
    const char *host = argv[1];
    const char *path = argc > 2 ? argv[2] : "/";
    const char *save = argc > 3 ? argv[3] : 0;

    w_reset(); w_str("connecting to "); w_str(host); dim(work);
    if (connect(host, 80) != 0) { err("could not connect"); return; }

    static char req[512];
    int n = 0;
    const char *p1 = "GET ";
    for (const char *p = p1; *p; p++) req[n++] = *p;
    for (const char *p = path; *p; p++) req[n++] = *p;
    const char *p2 = " HTTP/1.0\r\nHost: ";
    for (const char *p = p2; *p; p++) req[n++] = *p;
    for (const char *p = host; *p; p++) req[n++] = *p;
    const char *p3 = "\r\nConnection: close\r\nUser-Agent: nyx-term\r\n\r\n";
    for (const char *p = p3; *p; p++) req[n++] = *p;

    if (send(req, n) < 0) { err("send failed"); disconnect(); return; }

    static char body[IO_BUF];
    int total = 0;
    for (;;) {
        int got = recv(body + total, (int)sizeof(body) - 1 - total);
        if (got <= 0) break;
        total += got;
        if (total >= (int)sizeof(body) - 1) break;
    }
    disconnect();
    body[total] = 0;

    if (!total) { err("nothing came back"); return; }

    w_reset(); w_num((u32)total); w_str(" bytes received"); dim(work);

    /* Split the headers off, so what gets saved is the page itself. */
    int start = 0;
    for (int i = 0; i + 3 < total; i++) {
        if (body[i] == '\r' && body[i + 1] == '\n' &&
            body[i + 2] == '\r' && body[i + 3] == '\n') { start = i + 4; break; }
    }

    /* The status line is worth seeing either way. */
    char status[64];
    int sn = 0;
    while (sn < 63 && body[sn] && body[sn] != '\r' && body[sn] != '\n') { status[sn] = body[sn]; sn++; }
    status[sn] = 0;
    say(status);

    if (save) {
        if (spit(save, body + start, total - start) < 0) err("could not save it");
        else {
            w_reset(); w_str("saved "); w_num((u32)(total - start));
            w_str(" bytes to "); w_str(save);
            print(work, ACCENT);
        }
    } else {
        print(body + start, FG);
    }
}

static void cmd_about(void) {
    print("nyx terminal", ACCENT);
    say("A shell that is not part of the kernel.");
    say("");
    w_reset(); w_str("pid      "); w_num((u32)getpid()); say(work);
    say("ring     3");
    say("access   system calls only");
    say("");
    dim("Listing a directory, reading a file and fetching a page");
    dim("over TCP all cross the ring boundary through int 0x80.");
}

static void run(char *cmdline) {
    char *argv[16];
    int argc = split(cmdline, argv, 16);
    if (!argc) return;
    const char *c = argv[0];

    if (!strcmp(c, "help")) cmd_help();
    else if (!strcmp(c, "ls")) cmd_ls(argc > 1 ? argv[1] : 0);
    else if (!strcmp(c, "cd")) {
        if (chdir(argc > 1 ? argv[1] : "/") != 0) err("cd: not a directory");
    }
    else if (!strcmp(c, "pwd")) {
        char cwd[128];
        if (getcwd(cwd, sizeof(cwd)) >= 0) say(cwd);
    }
    else if (!strcmp(c, "cat")) {
        if (argc < 2) err("usage: cat FILE"); else cmd_cat(argv[1]);
    }
    else if (!strcmp(c, "write") || !strcmp(c, "append")) {
        if (argc < 3) { err("usage: write FILE TEXT"); return; }
        char text_buf[COLS + 2];
        join_from(argv, argc, 2, text_buf, COLS);
        int len = strlen(text_buf);
        text_buf[len++] = '\n';

        int flags = (c[0] == 'w') ? (O_WRITE | O_CREATE | O_TRUNC)
                                  : (O_WRITE | O_CREATE | O_APPEND);
        int fd = open(argv[1], flags);
        if (fd < 0) { err("cannot open it"); return; }
        int n = fwrite(fd, text_buf, len);
        close(fd);
        if (n == len) dim("ok"); else err("write failed");
    }
    else if (!strcmp(c, "rm")) {
        if (argc < 2) err("usage: rm FILE");
        else if (unlink(argv[1]) != 0) err("rm: no such file");
        else dim("ok");
    }
    else if (!strcmp(c, "mkdir")) {
        if (argc < 2) err("usage: mkdir NAME");
        else if (mkdir(argv[1]) != 0) err("mkdir: failed");
        else dim("ok");
    }
    else if (!strcmp(c, "rmdir")) {
        if (argc < 2) err("usage: rmdir NAME");
        else if (rmdir(argv[1]) != 0) err("rmdir: not empty, or not a directory");
        else dim("ok");
    }
    else if (!strcmp(c, "stat")) {
        if (argc < 2) err("usage: stat PATH"); else cmd_stat(argv[1]);
    }
    else if (!strcmp(c, "net")) cmd_net();
    else if (!strcmp(c, "get")) cmd_get(argc, argv);
    else if (!strcmp(c, "echo")) {
        char text_buf[COLS + 1];
        join_from(argv, argc, 1, text_buf, COLS);
        say(text_buf);
    }
    else if (!strcmp(c, "clear")) { n_lines = 0; first = 0; view = 0; }
    else if (!strcmp(c, "about")) cmd_about();
    else {
        w_reset(); w_str(c); w_str(": not a command, try help");
        err(work);
    }
}

/* --- input -------------------------------------------------------------- */

static void submit(void) {
    w_reset();
    char cwd[128];
    if (getcwd(cwd, sizeof(cwd)) < 0) strcpy(cwd, "/");
    w_str(cwd); w_str(" > "); w_str(input);
    print(work, DIM);

    char copy[COLS + 1];
    strncpy(copy, input, COLS + 1);
    in_len = 0;
    input[0] = 0;
    view = 0;
    run(copy);
}

static void on_key(u32 key) {
    if (key == '\n') { submit(); return; }
    if (key == '\b') { if (in_len > 0) input[--in_len] = 0; return; }
    if (key < 32 || key > 126) return;
    if (in_len < cols - 20) { input[in_len++] = (char)key; input[in_len] = 0; }
}

int main(void);

__attribute__((section(".text._start"))) void _start(void) {
    exit(main());
}

int main(void) {
    win = win_create("terminal", 760, 480);
    if (win < 0) { puts("term: no window\n"); return 1; }

    scr.px = win_surface(win);
    if (!scr.px) { puts("term: no surface\n"); return 1; }
    scr.w = win_width(win);
    scr.h = win_height(win);
    if (scr.w <= 0 || scr.h <= 0) return 1;

    cols = (scr.w - PAD * 2) / FONT_W;
    rows = (scr.h - PAD * 2) / FONT_H;
    if (cols > COLS) cols = COLS;

    print("nyx terminal", ACCENT);
    dim("A shell running in ring 3. Type help.");
    say("");

    draw_all();
    win_commit(win);

    for (;;) {
        win_event ev;
        int changed = 0;

        while (win_poll(win, &ev) == 1) {
            if (ev.type == WIN_EV_CLOSE) { win_close(win); return 0; }
            if (ev.type == WIN_EV_KEY) { on_key(ev.key); changed = 1; }
            if (ev.type == WIN_EV_MOUSE && (ev.buttons & WIN_BTN_DOWN)) {
                /* A click scrolls back to the bottom, which is what anyone
                   expects after reading up through output. */
                if (view) { view = 0; changed = 1; }
            }
        }

        blink++;
        if (blink % 12 == 0) changed = 1;      /* the cursor needs a repaint */

        if (changed) {
            draw_all();
            win_commit(win);
        }
        sleep_ms(20);
    }
}
