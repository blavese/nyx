/* A window manager.
 *
 * Windows are off-screen surfaces; the manager owns the chrome around them,
 * the stacking order and the pointer. Everything is composited into the
 * framebuffer's back buffer and pushed once per frame, so a window moving
 * over another never leaves a trail.
 *
 * Compositing writes rows with memcpy rather than going through fb_put per
 * pixel. At 1024x768 that is the difference between a desktop that drags
 * smoothly and one that does not. */
#include "wm.h"
#include "fb.h"
#include "gfx.h"
#include "font.h"
#include "mouse.h"
#include "keyboard.h"
#include "heap.h"
#include "string.h"
#include "printf.h"
#include "timer.h"

#define DESKTOP_BG    RGB(0x1B, 0x22, 0x2A)
#define DESKTOP_GRID  RGB(0x20, 0x28, 0x31)
#define BAR_BG        RGB(0x11, 0x16, 0x1C)
#define BAR_TEXT      RGB(0x8A, 0x9B, 0xA6)
#define TITLE_ACTIVE  RGB(0x2C, 0x7A, 0x6B)
#define TITLE_IDLE    RGB(0x2A, 0x33, 0x3D)
#define TITLE_TEXT    RGB(0xF0, 0xF4, 0xF6)
#define TITLE_DIM     RGB(0x9A, 0xA6, 0xB0)
#define FRAME_COLOR   RGB(0x0D, 0x11, 0x15)
#define CLOSE_HOT     RGB(0xC7, 0x4A, 0x3C)

#define TASKBAR_H 28

static window_t *stack[WM_MAX_WINDOWS];   /* index 0 is the bottom */
static int  nwin;
static bool running;

static window_t *dragging;
static int drag_off_x, drag_off_y;
static window_t *mouse_capture;           /* gets moves until the button lifts */

static u8  last_buttons;
static int last_mx, last_my;
static bool needs_composite = true;

bool wm_active(void) { return running; }

int wm_outer_w(const window_t *w) { return w->cw + WM_BORDER * 2; }
int wm_outer_h(const window_t *w) { return w->ch + WM_TITLE_H + WM_BORDER; }

void wm_invalidate(window_t *w) { if (w) w->dirty = true; needs_composite = true; }

window_t *wm_create(const char *title, int x, int y, int cw, int ch) {
    if (nwin >= WM_MAX_WINDOWS) return 0;

    window_t *w = (window_t *)kcalloc(sizeof(window_t));
    if (!w) return 0;
    w->canvas = (u32 *)kmalloc((u32)(cw * ch) * 4);
    if (!w->canvas) { kfree(w); return 0; }

    w->x = x; w->y = y; w->cw = cw; w->ch = ch;
    strncpy(w->title, title, sizeof(w->title) - 1);
    w->open = true;
    w->dirty = true;
    surf_clear(w->canvas, cw, ch, RGB(0x16, 0x1B, 0x21));

    stack[nwin++] = w;                    /* new windows open on top */
    needs_composite = true;
    return w;
}

void wm_close(window_t *w) {
    if (!w) return;
    if (w->on_close) w->on_close(w);
    for (int i = 0; i < nwin; i++) {
        if (stack[i] != w) continue;
        for (int j = i; j < nwin - 1; j++) stack[j] = stack[j + 1];
        nwin--;
        break;
    }
    if (dragging == w) dragging = 0;
    if (mouse_capture == w) mouse_capture = 0;
    if (w->canvas) kfree(w->canvas);
    kfree(w);
    needs_composite = true;
}

void wm_raise(window_t *w) {
    if (nwin == 0 || stack[nwin - 1] == w) return;
    for (int i = 0; i < nwin; i++) {
        if (stack[i] != w) continue;
        for (int j = i; j < nwin - 1; j++) stack[j] = stack[j + 1];
        stack[nwin - 1] = w;
        needs_composite = true;
        return;
    }
}

/* --- compositing -------------------------------------------------------- */

static void blit_surface(const u32 *px, int sw, int sh, int dx, int dy) {
    int sx = 0, sy = 0;
    int w = sw, h = sh;
    if (dx < 0) { sx = -dx; w += dx; dx = 0; }
    if (dy < 0) { sy = -dy; h += dy; dy = 0; }
    if (dx + w > (int)fb_width())  w = (int)fb_width() - dx;
    if (dy + h > (int)fb_height()) h = (int)fb_height() - dy;
    if (w <= 0 || h <= 0) return;

    u8 *dst = fb_pixels();
    u32 pitch = fb_pitch();
    for (int j = 0; j < h; j++) {
        const u32 *srow = px + (sy + j) * sw + sx;
        u8 *drow = dst + (u32)(dy + j) * pitch + (u32)dx * 4;
        memcpy(drow, srow, (u32)w * 4);
    }
}

static void draw_chrome(window_t *w, bool focused) {
    int ow = wm_outer_w(w), oh = wm_outer_h(w);

    fb_rect((u32)w->x, (u32)w->y, (u32)ow, WM_TITLE_H,
            focused ? TITLE_ACTIVE : TITLE_IDLE);
    fb_frame((u32)w->x, (u32)w->y, (u32)ow, (u32)oh, FRAME_COLOR);

    gfx_text(w->x + 8, w->y + (WM_TITLE_H - FONT_H) / 2, w->title,
             focused ? TITLE_TEXT : TITLE_DIM);

    /* close button */
    int bx = w->x + ow - 20, by = w->y + 6;
    fb_rect((u32)bx, (u32)by, 12, 12, focused ? CLOSE_HOT : TITLE_DIM);
    gfx_char(bx + 2, by - 2, 'x', RGB(0xFF, 0xFF, 0xFF));
}

static void draw_taskbar(void) {
    u32 y = fb_height() - TASKBAR_H;
    fb_rect(0, y, fb_width(), TASKBAR_H, BAR_BG);
    fb_rect(0, y, fb_width(), 1, RGB(0x2A, 0x33, 0x3D));

    gfx_text(12, (int)y + (TASKBAR_H - FONT_H) / 2, "nyx desktop", RGB(0x4F, 0xD6, 0xA0));

    int x = 130;
    for (int i = 0; i < nwin; i++) {
        bool focused = (i == nwin - 1);
        int tw = gfx_text_width(stack[i]->title) + 16;
        fb_rect((u32)x, y + 5, (u32)tw, TASKBAR_H - 10,
                focused ? RGB(0x24, 0x38, 0x3A) : RGB(0x1A, 0x21, 0x28));
        gfx_text(x + 8, (int)y + (TASKBAR_H - FONT_H) / 2, stack[i]->title,
                 focused ? RGB(0xD8, 0xE4, 0xE8) : BAR_TEXT);
        x += tw + 6;
    }

    const char *hint = "Esc  leave the desktop";
    gfx_text((int)fb_width() - gfx_text_width(hint) - 12,
             (int)y + (TASKBAR_H - FONT_H) / 2, hint, BAR_TEXT);
}

static const u8 CURSOR[19][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0}, {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0}, {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0}, {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0}, {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0}, {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0}, {1,2,2,2,2,2,2,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0}, {1,2,2,1,1,2,2,1,0,0,0,0},
    {1,2,1,0,0,1,2,2,1,0,0,0}, {1,1,0,0,0,1,2,2,1,0,0,0},
    {1,0,0,0,0,0,1,2,2,1,0,0}, {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0},
};

static void draw_cursor(int mx, int my) {
    for (int y = 0; y < 19; y++)
        for (int x = 0; x < 12; x++) {
            u8 v = CURSOR[y][x];
            if (!v) continue;
            fb_put((u32)(mx + x), (u32)(my + y),
                   v == 1 ? RGB(0x08, 0x0C, 0x10) : RGB(0xF4, 0xF7, 0xF9));
        }
}

static void composite(void) {
    fb_clear(DESKTOP_BG);

    /* a faint grid, so a window moving over the desktop is obvious */
    for (u32 y = 0; y < fb_height() - TASKBAR_H; y += 32)
        fb_rect(0, y, fb_width(), 1, DESKTOP_GRID);
    for (u32 x = 0; x < fb_width(); x += 32)
        fb_rect(x, 0, 1, fb_height() - TASKBAR_H, DESKTOP_GRID);

    for (int i = 0; i < nwin; i++) {
        window_t *w = stack[i];
        draw_chrome(w, i == nwin - 1);
        blit_surface(w->canvas, w->cw, w->ch,
                     w->x + WM_BORDER, w->y + WM_TITLE_H);
        w->dirty = false;
    }

    draw_taskbar();
    draw_cursor(last_mx, last_my);
    fb_flush();
}

/* --- input -------------------------------------------------------------- */

static window_t *window_at(int mx, int my, bool *on_title, bool *on_close) {
    for (int i = nwin - 1; i >= 0; i--) {
        window_t *w = stack[i];
        int ow = wm_outer_w(w), oh = wm_outer_h(w);
        if (mx < w->x || my < w->y || mx >= w->x + ow || my >= w->y + oh) continue;

        int bx = w->x + ow - 20, by = w->y + 6;
        *on_close = (mx >= bx && mx < bx + 12 && my >= by && my < by + 12);
        *on_title = (my < w->y + WM_TITLE_H);
        return w;
    }
    *on_title = *on_close = false;
    return 0;
}

static void handle_mouse(int mx, int my, u8 buttons) {
    bool pressed_now = (buttons & 1) && !(last_buttons & 1);
    bool released    = !(buttons & 1) && (last_buttons & 1);

    if (released) { dragging = 0; mouse_capture = 0; }

    if (dragging) {
        dragging->x = mx - drag_off_x;
        dragging->y = my - drag_off_y;
        if (dragging->y < 0) dragging->y = 0;
        if (dragging->x < -(dragging->cw - 60)) dragging->x = -(dragging->cw - 60);
        if (dragging->x > (int)fb_width() - 60) dragging->x = (int)fb_width() - 60;
        if (dragging->y > (int)fb_height() - TASKBAR_H - WM_TITLE_H)
            dragging->y = (int)fb_height() - TASKBAR_H - WM_TITLE_H;
        needs_composite = true;
        return;
    }

    /* Once a drag starts inside a window's content it keeps receiving
       movement, even if the pointer strays outside. */
    if (mouse_capture) {
        window_t *w = mouse_capture;
        if (w->on_mouse)
            w->on_mouse(w, mx - (w->x + WM_BORDER), my - (w->y + WM_TITLE_H), buttons, false);
        return;
    }

    if (!pressed_now) return;

    bool on_title = false, on_close = false;
    window_t *w = window_at(mx, my, &on_title, &on_close);
    if (!w) return;

    wm_raise(w);

    if (on_close) { wm_close(w); return; }

    if (on_title) {
        dragging = w;
        drag_off_x = mx - w->x;
        drag_off_y = my - w->y;
        return;
    }

    mouse_capture = w;
    if (w->on_mouse)
        w->on_mouse(w, mx - (w->x + WM_BORDER), my - (w->y + WM_TITLE_H), buttons, true);
}

void wm_quit(void) { running = false; }

void wm_run(void) {
    if (!fb_active()) { kprintf("the desktop needs a framebuffer\n"); return; }

    running = true;
    mouse_set_autodraw(false);          /* the manager draws the pointer */
    last_mx = mouse_x();
    last_my = mouse_y();
    last_buttons = mouse_buttons();
    needs_composite = true;

    while (running) {
        int mx = mouse_x(), my = mouse_y();
        u8 buttons = mouse_buttons();

        if (mx != last_mx || my != last_my || buttons != last_buttons) {
            int prev_buttons = last_buttons;
            last_mx = mx; last_my = my;
            handle_mouse(mx, my, buttons);
            last_buttons = buttons;
            (void)prev_buttons;
            needs_composite = true;
        }

        int c = kbd_trygetchar();
        if (c == 27) break;                        /* escape leaves */
        if (c >= 0 && nwin > 0 && stack[nwin - 1]->on_key)
            stack[nwin - 1]->on_key(stack[nwin - 1], (char)c);

        for (int i = 0; i < nwin; i++)
            if (stack[i]->dirty) needs_composite = true;

        if (needs_composite) {
            composite();
            needs_composite = false;
        }
    }

    while (nwin > 0) wm_close(stack[nwin - 1]);
    running = false;
    mouse_set_autodraw(true);
}
