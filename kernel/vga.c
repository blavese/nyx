/* 80x25 VGA text mode at the classic 0xB8000 mapping. */
#include "vga.h"
#include "io.h"
#include "string.h"
#include "fb.h"
#include "fbcon.h"

#define W 80
#define H 25
static volatile u16 *const BUF = (volatile u16 *)0xB8000;

static int row = 0, col = 0;
static u8 color = 0x07;

static inline u16 cell(char c, u8 attr) { return (u16)c | ((u16)attr << 8); }

void vga_set_color(u8 fg, u8 bg) {
    color = (u8)(fg | (bg << 4));
    if (fb_active()) fbcon_set_color(fg, bg);
}
u8   vga_get_color(void) { return color; }

static void move_hw_cursor(void) {
    u16 pos = (u16)(row * W + col);
    outb(0x3D4, 14); outb(0x3D5, (u8)(pos >> 8));
    outb(0x3D4, 15); outb(0x3D5, (u8)(pos & 0xFF));
}

void vga_clear(void) {
    if (fb_active()) { fbcon_clear(); return; }
    for (int i = 0; i < W * H; i++) BUF[i] = cell(' ', color);
    row = col = 0;
    move_hw_cursor();
}

void vga_init(void) { vga_set_color(VGA_LGREY, VGA_BLACK); vga_clear(); }

static void scroll(void) {
    for (int y = 1; y < H; y++)
        for (int x = 0; x < W; x++)
            BUF[(y - 1) * W + x] = BUF[y * W + x];
    for (int x = 0; x < W; x++) BUF[(H - 1) * W + x] = cell(' ', color);
    row = H - 1;
}

void vga_putc(char c) {
    switch (c) {
        case '\n': col = 0; row++; break;
        case '\r': col = 0; break;
        case '\t': col = (col + 4) & ~3; break;
        case '\b':
            if (col > 0) col--;
            else if (row > 0) { row--; col = W - 1; }
            BUF[row * W + col] = cell(' ', color);
            break;
        default:
            BUF[row * W + col] = cell(c, color);
            col++;
    }
    if (col >= W) { col = 0; row++; }
    if (row >= H) scroll();
    move_hw_cursor();
}

void vga_write(const char *s) { while (*s) vga_putc(*s++); }

void vga_cursor(int r, int c) {
    if (r >= 0 && r < H) row = r;
    if (c >= 0 && c < W) col = c;
    move_hw_cursor();
}
void vga_get_cursor(int *r, int *c) { if (r) *r = row; if (c) *c = col; }
