/* A text console drawn into the framebuffer.
 *
 * Mirrors the VGA text console's interface so the shell does not know or care
 * which one it is talking to. Colours use the same 16 entry palette, so code
 * written for text mode keeps working. */
#include "fbcon.h"
#include "fb.h"
#include "font.h"
#include "string.h"
#include "mouse.h"

static const u32 PALETTE[16] = {
    RGB(0x14, 0x18, 0x1D),   /* black, lifted slightly so it is not a void */
    RGB(0x2A, 0x54, 0xA8),   /* blue */
    RGB(0x2E, 0x9E, 0x5B),   /* green */
    RGB(0x1F, 0x8D, 0x96),   /* cyan */
    RGB(0xB2, 0x3A, 0x30),   /* red */
    RGB(0x8E, 0x3C, 0x9E),   /* magenta */
    RGB(0x9A, 0x6B, 0x2A),   /* brown */
    RGB(0xC2, 0xC8, 0xCC),   /* light grey */
    RGB(0x55, 0x5F, 0x66),   /* dark grey */
    RGB(0x5A, 0x8F, 0xE8), /* light blue */
    RGB(0x5E, 0xD1, 0x8A),   /* light green */
    RGB(0x4F, 0xD6, 0xD6),   /* light cyan */
    RGB(0xE8, 0x6A, 0x5E),   /* light red */
    RGB(0xD1, 0x77, 0xE0),   /* light magenta */
    RGB(0xE8, 0xC8, 0x62),   /* yellow */
    RGB(0xF2, 0xF5, 0xF7),   /* white */
};

static u32 cols, rows;
static u32 cx, cy;
static u8  fg = 7, bg = 0;
static bool cursor_shown;

void fbcon_init(void) {
    cols = fb_width() / FONT_W;
    rows = fb_height() / FONT_H;
    cx = cy = 0;
    fg = 7; bg = 0;
    cursor_shown = false;
    fb_clear(PALETTE[bg]);
    fb_flush();
}

u32 fbcon_cols(void) { return cols; }
u32 fbcon_rows(void) { return rows; }

void fbcon_set_color(u8 f, u8 b) { fg = f & 0x0F; bg = b & 0x0F; }

static void draw_cell(u32 col, u32 row, char ch, u8 f, u8 b) {
    u32 px = col * FONT_W, py = row * FONT_H;
    u32 fgc = PALETTE[f], bgc = PALETTE[b];

    if (ch < FONT_FIRST || ch > FONT_LAST) {
        fb_rect(px, py, FONT_W, FONT_H, bgc);
        return;
    }
    const u8 *g = font8x16[(u8)ch - FONT_FIRST];
    for (u32 y = 0; y < FONT_H; y++) {
        u8 bits = g[y];
        for (u32 x = 0; x < FONT_W; x++)
            fb_put(px + x, py + y, (bits & (0x80 >> x)) ? fgc : bgc);
    }
}

static void flush_cell(u32 col, u32 row) {
    fb_flush_rect(col * FONT_W, row * FONT_H, FONT_W, FONT_H);
}

static void hide_cursor(void) {
    if (!cursor_shown) return;
    fb_rect(cx * FONT_W, cy * FONT_H + FONT_H - 2, FONT_W, 2, PALETTE[bg]);
    flush_cell(cx, cy);
    cursor_shown = false;
}

static void show_cursor(void) {
    fb_rect(cx * FONT_W, cy * FONT_H + FONT_H - 2, FONT_W, 2, PALETTE[fg]);
    flush_cell(cx, cy);
    cursor_shown = true;
}

static void scroll(void) {
    u8 *p = fb_pixels();
    u32 pitch = fb_pitch();
    u32 line = FONT_H * pitch;
    u32 keep = (rows - 1) * line;

    memmove(p, p + line, keep);
    fb_rect(0, (rows - 1) * FONT_H, fb_width(), FONT_H, PALETTE[bg]);
    cy = rows - 1;
    fb_flush();
}

void fbcon_putc(char c) {
    /* The pointer saves the pixels underneath it, so it has to come
       off before anything writes there and go back on afterwards. */
    mouse_hide();
    hide_cursor();

    switch (c) {
        case '\n': cx = 0; cy++; break;
        case '\r': cx = 0; break;
        case '\t': cx = (cx + 4) & ~3u; break;
        case '\b':
            if (cx > 0) cx--;
            else if (cy > 0) { cy--; cx = cols - 1; }
            draw_cell(cx, cy, ' ', fg, bg);
            flush_cell(cx, cy);
            break;
        default:
            draw_cell(cx, cy, c, fg, bg);
            flush_cell(cx, cy);
            cx++;
    }

    if (cx >= cols) { cx = 0; cy++; }
    if (cy >= rows) scroll();
    show_cursor();
    mouse_show();
}

void fbcon_clear(void) {
    mouse_hide();
    cursor_shown = false;
    fb_clear(PALETTE[bg]);
    cx = cy = 0;
    fb_flush();
    show_cursor();
    mouse_show();
}
