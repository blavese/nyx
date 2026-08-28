/* The programs that live on the desktop.
 *
 * These are kernel-side windows rather than ring 3 processes: the window
 * manager has no way yet to hand a surface across the privilege boundary, so
 * a graphical program still runs inside the kernel. Text programs run in
 * user space; graphical ones do not, and that gap is worth naming. */
#include "apps.h"
#include "wm.h"
#include "gfx.h"
#include "font.h"
#include "fb.h"
#include "pmm.h"
#include "heap.h"
#include "timer.h"
#include "sched.h"
#include "fs.h"
#include "string.h"
#include "printf.h"

/* --- paint -------------------------------------------------------------- */

#define TOOLBAR_H  58
#define SWATCH     24
#define SWATCH_GAP 4

static const u32 PALETTE[16] = {
    RGB(0x18, 0x1D, 0x23), RGB(0xF2, 0xF5, 0xF7), RGB(0xC7, 0x4A, 0x3C),
    RGB(0xE0, 0x8A, 0x3C), RGB(0xE8, 0xC8, 0x62), RGB(0x5E, 0xD1, 0x8A),
    RGB(0x2E, 0x9E, 0x5B), RGB(0x4F, 0xD6, 0xD6), RGB(0x3C, 0x8F, 0xD1),
    RGB(0x2A, 0x54, 0xA8), RGB(0x8E, 0x6B, 0xE0), RGB(0xB0, 0x6A, 0xD6),
    RGB(0xE0, 0x74, 0xB8), RGB(0x8A, 0x5A, 0x3A), RGB(0x7A, 0x86, 0x92),
    RGB(0x3A, 0x44, 0x50),
};

static const int BRUSHES[4] = { 1, 3, 6, 12 };

typedef struct {
    int  color;
    int  brush;
    bool drawing;
    int  last_x, last_y;
} paint_state_t;

static paint_state_t paint;
static window_t *paint_win;

static int canvas_h(window_t *w) { return w->ch - TOOLBAR_H; }

static void paint_draw_toolbar(window_t *w) {
    surf_rect(w->canvas, w->cw, w->ch, 0, 0, w->cw, TOOLBAR_H, RGB(0x22, 0x29, 0x31));
    surf_rect(w->canvas, w->cw, w->ch, 0, TOOLBAR_H - 1, w->cw, 1, RGB(0x39, 0x44, 0x4F));

    for (int i = 0; i < 16; i++) {
        int x = 8 + i * (SWATCH + SWATCH_GAP);
        surf_rect(w->canvas, w->cw, w->ch, x, 6, SWATCH, SWATCH, PALETTE[i]);
        /* the selected colour gets a ring, so it reads without hovering */
        if (i == paint.color) {
            surf_frame(w->canvas, w->cw, w->ch, x - 2, 4, SWATCH + 4, SWATCH + 4,
                       RGB(0xFF, 0xFF, 0xFF));
            surf_frame(w->canvas, w->cw, w->ch, x - 3, 3, SWATCH + 6, SWATCH + 6,
                       RGB(0x10, 0x14, 0x18));
        } else {
            surf_frame(w->canvas, w->cw, w->ch, x, 6, SWATCH, SWATCH,
                       RGB(0x12, 0x16, 0x1B));
        }
    }

    int by = 36;
    surf_text(w->canvas, w->cw, w->ch, 8, by + 2, "brush", RGB(0x93, 0xA1, 0xAC));
    for (int i = 0; i < 4; i++) {
        int x = 56 + i * 34;
        bool on = (i == paint.brush);
        surf_rect(w->canvas, w->cw, w->ch, x, by, 28, 18,
                  on ? RGB(0x2C, 0x7A, 0x6B) : RGB(0x2E, 0x37, 0x41));
        surf_disc(w->canvas, w->cw, w->ch, x + 14, by + 9, BRUSHES[i] / 2 + 1,
                  on ? RGB(0xFF, 0xFF, 0xFF) : RGB(0xA8, 0xB4, 0xBE));
    }

    int cx = 210;
    surf_rect(w->canvas, w->cw, w->ch, cx, by, 54, 18, RGB(0x5A, 0x2E, 0x2A));
    surf_text(w->canvas, w->cw, w->ch, cx + 9, by + 2, "clear", RGB(0xF0, 0xD8, 0xD4));

    int sx = 278;
    surf_rect(w->canvas, w->cw, w->ch, sx, by, 46, 18, RGB(0x2E, 0x37, 0x41));
    surf_text(w->canvas, w->cw, w->ch, sx + 6, by + 2, "fill", RGB(0xD4, 0xDD, 0xE4));
}

static void paint_clear_canvas(window_t *w) {
    surf_rect(w->canvas, w->cw, w->ch, 0, TOOLBAR_H, w->cw, canvas_h(w),
              RGB(0xF7, 0xF8, 0xFA));
}

static void paint_on_mouse(window_t *w, int x, int y, u8 buttons, bool just_pressed) {
    bool down = (buttons & 1) != 0;

    if (y < TOOLBAR_H) {
        if (!just_pressed) return;

        for (int i = 0; i < 16; i++) {
            int sx = 8 + i * (SWATCH + SWATCH_GAP);
            if (x >= sx && x < sx + SWATCH && y >= 6 && y < 6 + SWATCH) {
                paint.color = i;
                paint_draw_toolbar(w);
                wm_invalidate(w);
                return;
            }
        }
        for (int i = 0; i < 4; i++) {
            int bx = 56 + i * 34;
            if (x >= bx && x < bx + 28 && y >= 36 && y < 54) {
                paint.brush = i;
                paint_draw_toolbar(w);
                wm_invalidate(w);
                return;
            }
        }
        if (x >= 210 && x < 264 && y >= 36 && y < 54) {
            paint_clear_canvas(w);
            wm_invalidate(w);
            return;
        }
        if (x >= 278 && x < 324 && y >= 36 && y < 54) {
            surf_rect(w->canvas, w->cw, w->ch, 0, TOOLBAR_H, w->cw, canvas_h(w),
                      PALETTE[paint.color]);
            wm_invalidate(w);
            return;
        }
        return;
    }

    if (!down) { paint.drawing = false; return; }

    int cy = y - TOOLBAR_H;
    if (cy < 0 || cy >= canvas_h(w) || x < 0 || x >= w->cw) return;

    /* Draw into a sub-surface offset by the toolbar, so the brush cannot
       paint over the controls. */
    u32 *canvas = w->canvas + TOOLBAR_H * w->cw;
    int ch = canvas_h(w);
    int r = BRUSHES[paint.brush];

    if (paint.drawing && !just_pressed) {
        /* Join to the previous position: the mouse reports in jumps, and
           without this a quick stroke is a row of dots. */
        surf_line(canvas, w->cw, ch, paint.last_x, paint.last_y, x, cy, r,
                  PALETTE[paint.color]);
    } else {
        surf_disc(canvas, w->cw, ch, x, cy, r, PALETTE[paint.color]);
    }

    paint.drawing = true;
    paint.last_x = x;
    paint.last_y = cy;
    wm_invalidate(w);
}

void app_paint(void) {
    if (paint_win) { wm_raise(paint_win); return; }

    window_t *w = wm_create("paint", 90, 70, 640, 430);
    if (!w) return;

    paint.color = 2;
    paint.brush = 1;
    paint.drawing = false;

    paint_clear_canvas(w);
    paint_draw_toolbar(w);
    w->on_mouse = paint_on_mouse;
    w->on_close = app_paint_forget;
    paint_win = w;
    wm_invalidate(w);
}

void app_paint_forget(window_t *w) { if (paint_win == w) paint_win = 0; }

/* --- about -------------------------------------------------------------- */

static void about_render(window_t *w) {
    surf_clear(w->canvas, w->cw, w->ch, RGB(0x16, 0x1B, 0x21));
    surf_rect(w->canvas, w->cw, w->ch, 0, 0, w->cw, 34, RGB(0x1E, 0x25, 0x2D));
    surf_text(w->canvas, w->cw, w->ch, 14, 9, "nyx " KERNEL_VERSION, RGB(0x4F, 0xD6, 0xA0));

    char line[80];
    int y = 48;
    const int step = 20;

    surf_text(w->canvas, w->cw, w->ch, 14, y, "an operating system written from scratch",
              RGB(0xC4, 0xCE, 0xD6)); y += step + 6;

    u32 total = pmm_total_frames() * 4;
    u32 used  = pmm_used_frames() * 4;

    kformat(line, sizeof(line), "memory    %d of %d KiB used", used, total);
    surf_text(w->canvas, w->cw, w->ch, 14, y, line, RGB(0x93, 0xA1, 0xAC)); y += step;

    kformat(line, sizeof(line), "heap      %d of %d KiB", heap_used() / 1024, heap_total() / 1024);
    surf_text(w->canvas, w->cw, w->ch, 14, y, line, RGB(0x93, 0xA1, 0xAC)); y += step;

    kformat(line, sizeof(line), "display   %d x %d, 32 bpp", (u32)fb_width(), (u32)fb_height());
    surf_text(w->canvas, w->cw, w->ch, 14, y, line, RGB(0x93, 0xA1, 0xAC)); y += step;

    kformat(line, sizeof(line), "uptime    %d seconds", (u32)(timer_ticks() / timer_hz()));
    surf_text(w->canvas, w->cw, w->ch, 14, y, line, RGB(0x93, 0xA1, 0xAC)); y += step;

    kformat(line, sizeof(line), "tasks     %d", task_count());
    surf_text(w->canvas, w->cw, w->ch, 14, y, line, RGB(0x93, 0xA1, 0xAC)); y += step;

    kformat(line, sizeof(line), "files     %d", fs_count());
    surf_text(w->canvas, w->cw, w->ch, 14, y, line, RGB(0x93, 0xA1, 0xAC)); y += step + 8;

    surf_text(w->canvas, w->cw, w->ch, 14, y, "drag a title bar to move a window",
              RGB(0x6E, 0x7C, 0x88)); y += step;
    surf_text(w->canvas, w->cw, w->ch, 14, y, "click one to bring it to the front",
              RGB(0x6E, 0x7C, 0x88));
}

static window_t *about_win;

static void about_on_mouse(window_t *w, int x, int y, u8 buttons, bool just_pressed) {
    (void)x; (void)y; (void)buttons;
    if (just_pressed) { about_render(w); wm_invalidate(w); }   /* refresh the figures */
}

void app_about(void) {
    if (about_win) { wm_raise(about_win); return; }
    window_t *w = wm_create("about", 300, 300, 380, 300);
    if (!w) return;
    about_render(w);
    w->on_mouse = about_on_mouse;
    w->on_close = app_about_forget;
    about_win = w;
    wm_invalidate(w);
}

void app_about_forget(window_t *w) { if (about_win == w) about_win = 0; }
