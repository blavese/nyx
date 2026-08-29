/* Drawing into a window surface, from ring 3.
 *
 * The kernel has its own copy of all of this in gfx.c. That is not an
 * oversight: a program cannot call into the kernel to draw, and shipping a
 * shared library would mean a dynamic linker. Two hundred lines of clipping
 * arithmetic is the cheaper answer. */
#pragma once
#include "nyx.h"
#include "font.h"

typedef struct {
    u32 *px;
    int  w, h;
} surface;

static inline void fill(surface *s, u32 c) {
    for (int i = 0; i < s->w * s->h; i++) s->px[i] = c;
}

static inline void rect(surface *s, int x, int y, int w, int h, u32 c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        u32 *row = s->px + (u32)(y + j) * s->w + x;
        for (int i = 0; i < w; i++) row[i] = c;
    }
}

static inline void frame(surface *s, int x, int y, int w, int h, u32 c) {
    rect(s, x, y, w, 1, c);
    rect(s, x, y + h - 1, w, 1, c);
    rect(s, x, y, 1, h, c);
    rect(s, x + w - 1, y, 1, h, c);
}

/* A rectangle with the corners knocked off. Real rounding needs a curve per
   corner; at these radii a stack of shortening rows is indistinguishable and
   costs nothing. */
static inline void round_rect(surface *s, int x, int y, int w, int h, int r, u32 c) {
    if (r <= 0) { rect(s, x, y, w, h, c); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    rect(s, x, y + r, w, h - r * 2, c);
    for (int i = 0; i < r; i++) {
        /* How far in this row starts, following the circle. */
        int dy = r - i;
        int dx = r;
        while (dx > 0 && dx * dx + dy * dy > r * r) dx--;
        int inset = r - dx;
        rect(s, x + inset, y + i, w - inset * 2, 1, c);
        rect(s, x + inset, y + h - 1 - i, w - inset * 2, 1, c);
    }
}

static inline void disc(surface *s, int cx, int cy, int r, u32 c) {
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r) {
                int x = cx + dx, y = cy + dy;
                if (x >= 0 && x < s->w && y >= 0 && y < s->h)
                    s->px[(u32)y * s->w + x] = c;
            }
}

/* Bresenham, so a fast drag leaves a stroke instead of a dotted trail. */
static inline void line(surface *s, int x0, int y0, int x1, int y1, u32 c) {
    int dx = x1 - x0, dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        if (x0 >= 0 && x0 < s->w && y0 >= 0 && y0 < s->h)
            s->px[(u32)y0 * s->w + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

static inline void glyph(surface *s, int x, int y, char ch, u32 c) {
    if (ch < FONT_FIRST || ch > FONT_LAST) return;
    const unsigned char *g = font8x16[(int)ch - FONT_FIRST];
    for (int row = 0; row < FONT_H; row++) {
        int py = y + row;
        if (py < 0 || py >= s->h) continue;
        unsigned char bits = g[row];
        if (!bits) continue;
        for (int col = 0; col < FONT_W; col++) {
            if (!(bits & (0x80 >> col))) continue;
            int px = x + col;
            if (px < 0 || px >= s->w) continue;
            s->px[(u32)py * s->w + px] = c;
        }
    }
}

static inline void text(surface *s, int x, int y, const char *str, u32 c) {
    for (int i = 0; str[i]; i++) glyph(s, x + i * FONT_W, y, str[i], c);
}

/* Text centred in a box, which is what every button below wants. */
static inline void text_centred(surface *s, int x, int y, int w, int h,
                                const char *str, u32 c) {
    int tw = strlen(str) * FONT_W;
    text(s, x + (w - tw) / 2, y + (h - FONT_H) / 2, str, c);
}

/* Blends `over` into `under` by `alpha` out of 255. Used for hover states
   and shadows, where a second opaque colour would be one more thing to keep
   in step with the theme. */
static inline u32 mix(u32 under, u32 over, int alpha) {
    int ur = (under >> 16) & 0xFF, ug = (under >> 8) & 0xFF, ub = under & 0xFF;
    int orr = (over >> 16) & 0xFF, og = (over >> 8) & 0xFF, ob = over & 0xFF;
    int r = ur + (orr - ur) * alpha / 255;
    int g = ug + (og - ug) * alpha / 255;
    int b = ub + (ob - ub) * alpha / 255;
    return RGB(r, g, b);
}
