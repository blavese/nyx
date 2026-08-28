/* Drawing helpers that sit on top of the framebuffer: text, and blitting
   between an off-screen surface and the screen. */
#include "gfx.h"
#include "fb.h"
#include "font.h"
#include "string.h"

void gfx_char(int x, int y, char c, u32 fg) {
    if (c < FONT_FIRST || c > FONT_LAST) return;
    const u8 *g = font8x16[(u8)c - FONT_FIRST];
    for (int row = 0; row < FONT_H; row++) {
        u8 bits = g[row];
        for (int col = 0; col < FONT_W; col++)
            if (bits & (0x80 >> col)) fb_put((u32)(x + col), (u32)(y + row), fg);
    }
}

void gfx_text(int x, int y, const char *s, u32 fg) {
    for (int i = 0; s[i]; i++) gfx_char(x + i * FONT_W, y, s[i], fg);
}

void gfx_text_bg(int x, int y, const char *s, u32 fg, u32 bg) {
    int w = (int)strlen(s) * FONT_W;
    fb_rect((u32)x, (u32)y, (u32)w, FONT_H, bg);
    gfx_text(x, y, s, fg);
}

int gfx_text_width(const char *s) { return (int)strlen(s) * FONT_W; }

/* --- off-screen surfaces ------------------------------------------------ */

void surf_clear(u32 *px, int w, int h, u32 rgb) {
    for (int i = 0; i < w * h; i++) px[i] = rgb;
}

void surf_rect(u32 *px, int w, int h, int x, int y, int rw, int rh, u32 rgb) {
    if (x < 0) { rw += x; x = 0; }
    if (y < 0) { rh += y; y = 0; }
    if (x + rw > w) rw = w - x;
    if (y + rh > h) rh = h - y;
    if (rw <= 0 || rh <= 0) return;
    for (int j = 0; j < rh; j++) {
        u32 *row = px + (y + j) * w + x;
        for (int i = 0; i < rw; i++) row[i] = rgb;
    }
}

void surf_frame(u32 *px, int w, int h, int x, int y, int rw, int rh, u32 rgb) {
    surf_rect(px, w, h, x, y, rw, 1, rgb);
    surf_rect(px, w, h, x, y + rh - 1, rw, 1, rgb);
    surf_rect(px, w, h, x, y, 1, rh, rgb);
    surf_rect(px, w, h, x + rw - 1, y, 1, rh, rgb);
}

/* A filled circle, which is what a paint brush actually needs. */
void surf_disc(u32 *px, int w, int h, int cx, int cy, int r, u32 rgb) {
    if (r < 1) r = 1;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r) continue;
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            px[y * w + x] = rgb;
        }
    }
}

/* Bresenham, so a fast drag does not leave gaps between sampled positions. */
void surf_line(u32 *px, int w, int h, int x0, int y0, int x1, int y1, int r, u32 rgb) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;
    int err = adx - ady;

    for (;;) {
        surf_disc(px, w, h, x0, y0, r, rgb);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -ady) { err -= ady; x0 += sx; }
        if (e2 < adx)  { err += adx; y0 += sy; }
    }
}

void surf_char(u32 *px, int w, int h, int x, int y, char c, u32 fg) {
    if (c < FONT_FIRST || c > FONT_LAST) return;
    const u8 *g = font8x16[(u8)c - FONT_FIRST];
    for (int row = 0; row < FONT_H; row++) {
        u8 bits = g[row];
        for (int col = 0; col < FONT_W; col++) {
            if (!(bits & (0x80 >> col))) continue;
            int px_x = x + col, px_y = y + row;
            if (px_x < 0 || px_y < 0 || px_x >= w || px_y >= h) continue;
            px[px_y * w + px_x] = fg;
        }
    }
}

void surf_text(u32 *px, int w, int h, int x, int y, const char *s, u32 fg) {
    for (int i = 0; s[i]; i++) surf_char(px, w, h, x + i * FONT_W, y, s[i], fg);
}

void surf_blit(const u32 *px, int w, int h, int dx, int dy) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb_put((u32)(dx + i), (u32)(dy + j), px[j * w + i]);
}
