/* Settings, running in ring 3.
 *
 * This program cannot reach into the window manager and has no way to ask it
 * for anything. What it can do is write a file. The window manager re-reads
 * that file four times a second, so a choice made here shows up on the
 * desktop behind this window almost immediately, without either side knowing
 * anything about the other beyond the format.
 *
 * The file is plain "key value" text on purpose: everything this window does
 * can also be done with the shell's write command. */
#include "nyx.h"
#include "draw.h"

#define CFG "/nyx.cfg"

#define W 440
#define H 440

static surface scr;
static int win;

/* What is being edited. These are the same names the kernel's theme.c
   parses; nothing else is shared between the two. */
static int preset = 0;
static int wallpaper = 3;
static int corner = 8;
static int shadows = 1;
static int animate = 1;
static int quirks = 1;

static int dirty_frames;      /* shows a confirmation for a moment after saving */

/* The preset accents, duplicated from theme.c. Two copies of six colours is
   cheaper than a system call to fetch them, and if they drift the only cost
   is that the swatch is a shade off what the desktop shows. */
static const u32 ACCENTS[6] = {
    RGB(0x2C, 0xC7, 0xA0), RGB(0x6E, 0x8A, 0xE8), RGB(0xE0, 0xA0, 0x3C),
    RGB(0xE0, 0x6A, 0x8C), RGB(0x8A, 0x9B, 0xB0), RGB(0x9A, 0xD1, 0x4A),
};
static const char *PRESET_NAMES[6] = { "teal", "indigo", "amber", "rose", "slate", "lime" };
/* Seven, in the order theme.h numbers them. */
#define N_WALLPAPERS 7
static const char *WALLPAPERS[N_WALLPAPERS] = {
    "plain", "grid", "dots", "gradient", "stars", "waves", "weave"
};
static const int CORNERS[4] = { 0, 4, 8, 14 };

#define BG      RGB(0x15, 0x1B, 0x22)
#define PANEL   RGB(0x1E, 0x26, 0x2F)
#define FG      RGB(0xDA, 0xE3, 0xEA)
#define DIM     RGB(0x77, 0x86, 0x93)
#define EDGE    RGB(0x2C, 0x36, 0x41)

static u32 accent(void) { return ACCENTS[preset]; }

/* --- the config file ---------------------------------------------------- */

static int find_value(const char *text, const char *key, int fallback) {
    int klen = strlen(key);
    for (int i = 0; text[i]; i++) {
        if (i && text[i - 1] != '\n') continue;
        if (strncmp(text + i, key, klen) != 0) continue;
        if (text[i + klen] != ' ') continue;

        int j = i + klen + 1;
        int v = 0, any = 0;
        while (text[j] >= '0' && text[j] <= '9') { v = v * 10 + (text[j] - '0'); j++; any = 1; }
        return any ? v : fallback;
    }
    return fallback;
}

static void load(void) {
    static char buf[1024];
    int n = slurp(CFG, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = 0;

    /* The first line is a comment, so a key at offset 0 cannot be matched by
       the loop above; prefixing a newline makes every key look the same. */
    static char padded[1030];
    padded[0] = '\n';
    memcpy(padded + 1, buf, n + 1);

    preset    = find_value(padded, "preset", preset);
    wallpaper = find_value(padded, "wallpaper", wallpaper);
    corner    = find_value(padded, "corner", corner);
    shadows   = find_value(padded, "shadows", shadows);
    animate   = find_value(padded, "animate", animate);
    quirks    = find_value(padded, "quirks", quirks);

    if (preset < 0 || preset > 5) preset = 0;
    if (wallpaper < 0 || wallpaper >= N_WALLPAPERS) wallpaper = 3;
    if (corner < 0 || corner > 20) corner = 8;
}

static void save(void) {
    char out[256];
    int n = 0;
    const char *head = "# written by settings\n";
    for (const char *p = head; *p; p++) out[n++] = *p;

    struct { const char *key; int value; } fields[6] = {
        { "preset", preset }, { "wallpaper", wallpaper }, { "corner", corner },
        { "shadows", shadows }, { "animate", animate }, { "quirks", quirks },
    };
    for (int i = 0; i < 6; i++) {
        for (const char *p = fields[i].key; *p; p++) out[n++] = *p;
        out[n++] = ' ';
        n += utoa((u32)fields[i].value, out + n);
        out[n++] = '\n';
    }

    spit(CFG, out, n);
    dirty_frames = 60;
}

/* --- layout ------------------------------------------------------------- */

/* Every control is a rectangle with an index, so hit testing and drawing
   agree by construction rather than by two lists being kept in step. */
typedef struct { int x, y, w, h; } box;

static box swatch_box(int i)    { return (box){ 24 + i * 46, 74, 36, 36 }; }
/* Four across, wrapping, because seven no longer fit on one line. */
static box wallpaper_box(int i) {
    return (box){ 24 + (i % 4) * 100, 168 + (i / 4) * 40, 92, 32 };
}
static box corner_box(int i)    { return (box){ 24 + i * 62, 298, 54, 32 }; }
static box shadow_box(void)     { return (box){ 24, 360, 122, 32 }; }
static box animate_box(void)    { return (box){ 154, 360, 122, 32 }; }
static box quirks_box(void)     { return (box){ 284, 360, 132, 32 }; }

static int inside(box b, int x, int y) {
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static void section(int y, const char *label) {
    text(&scr, 24, y, label, DIM);
}

static void toggle(box b, const char *label, int on) {
    round_rect(&scr, b.x, b.y, b.w, b.h, 6, on ? accent() : PANEL);
    frame(&scr, b.x, b.y, b.w, b.h, EDGE);

    /* A small switch, so the state reads without depending on colour. */
    int kw = 20, kh = 12;
    int kx = b.x + 10, ky = b.y + (b.h - kh) / 2;
    round_rect(&scr, kx, ky, kw, kh, kh / 2, on ? RGB(0x14, 0x1A, 0x20) : EDGE);
    disc(&scr, on ? kx + kw - 5 : kx + 5, ky + kh / 2, 4,
         on ? accent() : DIM);

    text(&scr, kx + kw + 10, b.y + (b.h - FONT_H) / 2, label,
         on ? RGB(0x10, 0x16, 0x1C) : FG);
}

static void draw_all(void) {
    fill(&scr, BG);

    /* header */
    round_rect(&scr, 0, 0, W, 52, 0, PANEL);
    text(&scr, 24, 18, "Appearance", FG);
    text(&scr, 24 + 11 * FONT_W, 18, "  desktop settings", DIM);
    rect(&scr, 0, 51, W, 1, EDGE);

    section(58, "accent");
    for (int i = 0; i < 6; i++) {
        box b = swatch_box(i);
        round_rect(&scr, b.x, b.y, b.w, b.h, 8, ACCENTS[i]);
        if (i == preset) {
            round_rect(&scr, b.x - 3, b.y - 3, b.w + 6, b.h + 6, 10,
                       mix(BG, ACCENTS[i], 90));
            round_rect(&scr, b.x, b.y, b.w, b.h, 8, ACCENTS[i]);
            disc(&scr, b.x + b.w / 2, b.y + b.h / 2, 5, RGB(0x12, 0x17, 0x1C));
        }
    }
    text(&scr, 24, 120, PRESET_NAMES[preset], FG);

    section(152, "wallpaper");
    for (int i = 0; i < N_WALLPAPERS; i++) {
        box b = wallpaper_box(i);
        int on = (i == wallpaper);
        round_rect(&scr, b.x, b.y, b.w, b.h, 6, on ? accent() : PANEL);
        frame(&scr, b.x, b.y, b.w, b.h, EDGE);
        text_centred(&scr, b.x, b.y, b.w, b.h, WALLPAPERS[i],
                     on ? RGB(0x10, 0x16, 0x1C) : FG);
    }

    section(282, "corners");
    for (int i = 0; i < 4; i++) {
        box b = corner_box(i);
        int on = (CORNERS[i] == corner);
        round_rect(&scr, b.x, b.y, b.w, b.h, CORNERS[i] ? CORNERS[i] / 2 + 2 : 0,
                   on ? accent() : PANEL);
        frame(&scr, b.x, b.y, b.w, b.h, EDGE);
        char label[8];
        utoa((u32)CORNERS[i], label);
        text_centred(&scr, b.x, b.y, b.w, b.h, label,
                     on ? RGB(0x10, 0x16, 0x1C) : FG);
    }

    section(344, "effects");
    toggle(shadow_box(), "shadows", shadows);
    toggle(animate_box(), "smooth", animate);
    toggle(quirks_box(), "quirks", quirks);

    if (dirty_frames > 0) {
        const char *msg = "saved to /nyx.cfg";
        int tw = strlen(msg) * FONT_W;
        text(&scr, W - tw - 20, 20, msg, accent());
    }
}

/* --- input -------------------------------------------------------------- */

static void on_click(int x, int y) {
    for (int i = 0; i < 6; i++)
        if (inside(swatch_box(i), x, y)) { preset = i; save(); return; }
    for (int i = 0; i < N_WALLPAPERS; i++)
        if (inside(wallpaper_box(i), x, y)) { wallpaper = i; save(); return; }
    for (int i = 0; i < 4; i++)
        if (inside(corner_box(i), x, y)) { corner = CORNERS[i]; save(); return; }
    if (inside(shadow_box(), x, y))  { shadows = !shadows; save(); return; }
    if (inside(animate_box(), x, y)) { animate = !animate; save(); return; }
    if (inside(quirks_box(), x, y))  { quirks = !quirks; save(); return; }
}

int main(void);

__attribute__((section(".text._start"))) void _start(void) {
    exit(main());
}

int main(void) {
    win = win_create("settings", W, H);
    if (win < 0) { puts("settings: no window\n"); return 1; }

    scr.px = win_surface(win);
    if (!scr.px) { puts("settings: no surface\n"); return 1; }
    scr.w = win_width(win);
    scr.h = win_height(win);
    if (scr.w <= 0 || scr.h <= 0) return 1;

    load();
    draw_all();
    win_commit(win);

    for (;;) {
        win_event ev;
        int changed = 0;

        while (win_poll(win, &ev) == 1) {
            if (ev.type == WIN_EV_CLOSE) { win_close(win); return 0; }
            if (ev.type == WIN_EV_MOUSE && (ev.buttons & WIN_BTN_DOWN)) {
                on_click(ev.x, ev.y);
                changed = 1;
            }
            if (ev.type == WIN_EV_KEY) {
                if (ev.key == 'r') { load(); changed = 1; }
                if (ev.key == 's') { save(); changed = 1; }
            }
        }

        if (dirty_frames > 0) { dirty_frames--; if (dirty_frames == 0) changed = 1; }

        if (changed) { draw_all(); win_commit(win); }
        sleep_ms(20);
    }
}
