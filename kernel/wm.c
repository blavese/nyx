/* A window manager.
 *
 * Windows are off-screen surfaces; the manager owns the chrome around them,
 * the stacking order and the pointer. Everything is composited into the
 * framebuffer's back buffer and pushed once per frame, so a window moving
 * over another never leaves a trail.
 *
 * Compositing writes rows with memcpy rather than going through fb_put per
 * pixel. At 1024x768 that is the difference between a desktop that drags
 * smoothly and one that does not. The chrome is the exception: rounded
 * corners and shadows read the framebuffer back, so they are drawn per pixel
 * and only around the edges of a window rather than across it.
 *
 * Nothing here picks its own colours. They all come from theme.c, which
 * reads a file a ring 3 program writes, which is how the settings window
 * changes the look of a desktop it cannot otherwise reach. */
#include "wm.h"
#include "fb.h"
#include "gfx.h"
#include "font.h"
#include "theme.h"
#include "mouse.h"
#include "keyboard.h"
#include "heap.h"
#include "string.h"
#include "printf.h"
#include "timer.h"
#include "vfs.h"
#include "user.h"
#include "elf.h"
#include "apps.h"

#define TASKBAR_H  34
#define MENU_W     210
#define MENU_ITEM  30
#define SHADOW     5

static window_t *stack[WM_MAX_WINDOWS];   /* index 0 is the bottom */
static int  nwin;
static bool running;

static window_t *dragging;
static int drag_off_x, drag_off_y;
static window_t *mouse_capture;           /* gets moves until the button lifts */

static u8  last_buttons;
static int last_mx, last_my;
static bool needs_composite = true;

/* The launcher. Open when someone clicks the desktop or the taskbar badge. */
static bool menu_open;
static int  menu_x, menu_y;
static int  menu_hover = -1;

static u64 last_theme_check;

/* What the launcher offers. A null program means the kernel handles it. */
static const struct {
    const char *label;
    const char *program;
} MENU[] = {
    { "Terminal",     "/term.elf" },
    { "Paint",        "/paint.elf" },
    { "Settings",     "/settings.elf" },
    { "System info",  0 },
    { "Close all",    0 },
    { "Leave desktop", 0 },
};

#define MENU_N ((int)(sizeof(MENU) / sizeof(MENU[0])))

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
    surf_clear(w->canvas, cw, ch, theme()->surface);

    stack[nwin++] = w;                    /* new windows open on top */
    needs_composite = true;
    return w;
}

void wm_close(window_t *w) {
    if (!w) return;
    if (w->owned_by_user) {
        /* Tell the owner rather than pulling the surface out from under it. */
        wm_event_t ev = { WM_EV_CLOSE, 0, 0, 0, 0 };
        wm_push_event(w, &ev);
    }
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

/* --- events for programs outside the kernel ----------------------------- */

void wm_push_event(window_t *w, const wm_event_t *ev) {
    if (!w) return;
    u32 next = (w->q_head + 1) % WM_EVENT_QUEUE;
    if (next == w->q_tail) return;          /* full: drop the oldest news */
    w->queue[w->q_head] = *ev;
    w->q_head = next;
}

bool wm_pop_event(window_t *w, wm_event_t *out) {
    if (!w || w->q_head == w->q_tail) return false;
    *out = w->queue[w->q_tail];
    w->q_tail = (w->q_tail + 1) % WM_EVENT_QUEUE;
    return true;
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

/* Lighter and darker versions of a colour, for edges and hovers. */
static u32 lighten(u32 c, int amount) { return gfx_mix(c, RGB(0xFF, 0xFF, 0xFF), amount); }
static u32 darken(u32 c, int amount)  { return gfx_mix(c, 0, amount); }

static void draw_wallpaper(void) {
    const theme_t *t = theme();
    int h = (int)fb_height() - TASKBAR_H;

    switch (t->wallpaper) {
    case WALLPAPER_GRADIENT:
        /* Lit from the top left, which is where a desktop usually is. */
        fb_vgradient(0, 0, (int)fb_width(), h, lighten(t->desktop, 22), t->desktop);
        break;

    case WALLPAPER_GRID: {
        fb_rect(0, 0, fb_width(), (u32)h, t->desktop);
        u32 linec = lighten(t->desktop, 14);
        for (int y = 0; y < h; y += 32) fb_rect(0, (u32)y, fb_width(), 1, linec);
        for (u32 x = 0; x < fb_width(); x += 32) fb_rect(x, 0, 1, (u32)h, linec);
        break;
    }

    case WALLPAPER_DOTS: {
        fb_rect(0, 0, fb_width(), (u32)h, t->desktop);
        u32 dot = lighten(t->desktop, 26);
        for (int y = 16; y < h; y += 28)
            for (u32 x = 16; x < fb_width(); x += 28) {
                fb_put(x, (u32)y, dot);
                fb_put(x + 1, (u32)y, dot);
                fb_put(x, (u32)y + 1, dot);
                fb_put(x + 1, (u32)y + 1, dot);
            }
        break;
    }

    default:
        fb_rect(0, 0, fb_width(), (u32)h, t->desktop);
        break;
    }
}

static int close_box(const window_t *w, int *bx, int *by) {
    *bx = w->x + wm_outer_w(w) - 26;
    *by = w->y + (WM_TITLE_H - 14) / 2;
    return 14;
}

static void draw_chrome(window_t *w, bool focused) {
    const theme_t *t = theme();
    int ow = wm_outer_w(w), oh = wm_outer_h(w);
    int r = t->corner;

    if (t->shadows) fb_shadow(w->x, w->y, ow, oh, r, SHADOW);

    /* The body, so the rounded bottom corners have something under them. */
    fb_round_rect(w->x, w->y, ow, oh, r, t->surface);

    /* The title bar is the top of that same rounded shape, which is why it
       is drawn as its own rounded rect and then squared off at the bottom. */
    u32 bar = focused ? t->accent : lighten(t->surface, 10);
    fb_round_rect(w->x, w->y, ow, WM_TITLE_H + r, r, bar);
    fb_rect((u32)w->x, (u32)(w->y + WM_TITLE_H - 1), (u32)ow, 1,
            focused ? darken(t->accent, 40) : darken(t->surface, 20));

    u32 title_fg = focused ? darken(t->accent, 170) : t->text_dim;
    gfx_text(w->x + 12, w->y + (WM_TITLE_H - FONT_H) / 2, w->title, title_fg);

    /* A dot rather than a cross: at 14 pixels a cross is mostly noise, and
       the colour already says what it does. */
    int bx, by, bs = close_box(w, &bx, &by);
    u32 dot = focused ? RGB(0xE0, 0x6A, 0x5A) : darken(t->surface, 30);
    fb_round_rect(bx, by, bs, bs, bs / 2, dot);
    if (focused) {
        fb_rect((u32)(bx + 4), (u32)(by + 6), 6, 2, darken(dot, 120));
    }

    fb_round_frame(w->x, w->y, ow, oh, r, darken(t->surface, 55));
}

static void draw_menu(void) {
    if (!menu_open) return;
    const theme_t *t = theme();
    int h = MENU_N * MENU_ITEM + 12;

    if (t->shadows) fb_shadow(menu_x, menu_y, MENU_W, h, 8, SHADOW);
    fb_round_rect(menu_x, menu_y, MENU_W, h, 8, lighten(t->surface, 6));
    fb_round_frame(menu_x, menu_y, MENU_W, h, 8, darken(t->surface, 40));

    for (int i = 0; i < MENU_N; i++) {
        int iy = menu_y + 6 + i * MENU_ITEM;
        if (i == menu_hover)
            fb_round_rect(menu_x + 5, iy, MENU_W - 10, MENU_ITEM, 6,
                          gfx_mix(lighten(t->surface, 6), t->accent, 60));

        u32 fg = (i == menu_hover) ? t->text : gfx_mix(t->text, t->text_dim, 120);
        gfx_text(menu_x + 38, iy + (MENU_ITEM - FONT_H) / 2, MENU[i].label, fg);

        /* A rounded square stands in for an icon. Accent for the things that
           launch a program, grey for the ones the desktop handles itself. */
        fb_round_rect(menu_x + 16, iy + MENU_ITEM / 2 - 6, 12, 12, 3,
                      MENU[i].program ? t->accent : darken(t->text_dim, 60));
    }
}

static void draw_taskbar(void) {
    const theme_t *t = theme();
    int y = (int)fb_height() - TASKBAR_H;

    fb_rect(0, (u32)y, fb_width(), TASKBAR_H, darken(t->surface, 40));
    fb_rect(0, (u32)y, fb_width(), 1, lighten(t->surface, 12));

    /* The launcher badge, which is also what the desktop menu opens from. */
    bool badge_hot = menu_open;
    fb_round_rect(8, y + 5, 76, TASKBAR_H - 10, 6,
                  badge_hot ? t->accent : lighten(t->surface, 4));
    gfx_text(20, y + (TASKBAR_H - FONT_H) / 2, "nyx",
             badge_hot ? darken(t->accent, 170) : t->accent);

    int x = 96;
    for (int i = 0; i < nwin; i++) {
        bool focused = (i == nwin - 1);
        int tw = gfx_text_width(stack[i]->title) + 24;
        if (x + tw > (int)fb_width() - 120) break;

        fb_round_rect(x, y + 5, tw, TASKBAR_H - 10, 6,
                      focused ? gfx_mix(lighten(t->surface, 4), t->accent, 70)
                              : lighten(t->surface, 4));
        gfx_text(x + 12, y + (TASKBAR_H - FONT_H) / 2, stack[i]->title,
                 focused ? t->text : t->text_dim);
        if (focused) fb_rect((u32)(x + 8), (u32)(y + TASKBAR_H - 6), (u32)(tw - 16), 2, t->accent);
        x += tw + 6;
    }

    /* Uptime on the right, which is the only clock this machine has. */
    char clock[24];
    u32 secs = (u32)(timer_ticks() / timer_hz());
    kformat(clock, sizeof(clock), "up %d:%02d", secs / 60, secs % 60);
    gfx_text((int)fb_width() - gfx_text_width(clock) - 16,
             y + (TASKBAR_H - FONT_H) / 2, clock, t->text_dim);
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
    draw_wallpaper();

    for (int i = 0; i < nwin; i++) {
        window_t *w = stack[i];
        draw_chrome(w, i == nwin - 1);
        blit_surface(w->canvas, w->cw, w->ch,
                     w->x + WM_BORDER, w->y + WM_TITLE_H);
        w->dirty = false;
    }

    draw_taskbar();
    draw_menu();
    draw_cursor(last_mx, last_my);
    fb_flush();
}

/* --- launching ---------------------------------------------------------- */

static void launch(const char *path) {
    u32 size = 0;
    u8 *img = vfs_slurp(path, &size);
    if (!img) return;
    user_spawn_elf(path, img, size);
    kfree(img);
}

static void menu_choose(int i) {
    menu_open = false;
    needs_composite = true;
    if (i < 0 || i >= MENU_N) return;

    if (MENU[i].program) { launch(MENU[i].program); return; }

    if (!strcmp(MENU[i].label, "System info")) app_about();
    else if (!strcmp(MENU[i].label, "Close all")) { while (nwin > 0) wm_close(stack[nwin - 1]); }
    else if (!strcmp(MENU[i].label, "Leave desktop")) running = false;
}

static int menu_item_at(int mx, int my) {
    if (!menu_open) return -1;
    int h = MENU_N * MENU_ITEM + 12;
    if (mx < menu_x || mx >= menu_x + MENU_W) return -1;
    if (my < menu_y + 6 || my >= menu_y + h - 6) return -1;
    int i = (my - menu_y - 6) / MENU_ITEM;
    return (i >= 0 && i < MENU_N) ? i : -1;
}

static void open_menu_at(int x, int y) {
    int h = MENU_N * MENU_ITEM + 12;
    if (x + MENU_W > (int)fb_width()) x = (int)fb_width() - MENU_W - 4;
    if (y + h > (int)fb_height() - TASKBAR_H) y = (int)fb_height() - TASKBAR_H - h - 4;
    if (x < 4) x = 4;
    if (y < 4) y = 4;
    menu_x = x; menu_y = y;
    menu_open = true;
    menu_hover = -1;
    needs_composite = true;
}

/* --- input -------------------------------------------------------------- */

static window_t *window_at(int mx, int my, bool *on_title, bool *on_close) {
    for (int i = nwin - 1; i >= 0; i--) {
        window_t *w = stack[i];
        int ow = wm_outer_w(w), oh = wm_outer_h(w);
        if (mx < w->x || my < w->y || mx >= w->x + ow || my >= w->y + oh) continue;

        int bx, by, bs = close_box(w, &bx, &by);
        *on_close = (mx >= bx && mx < bx + bs && my >= by && my < by + bs);
        *on_title = (my < w->y + WM_TITLE_H);
        return w;
    }
    *on_title = *on_close = false;
    return 0;
}

static bool on_taskbar_badge(int mx, int my) {
    int y = (int)fb_height() - TASKBAR_H;
    return my >= y + 5 && my < y + TASKBAR_H - 5 && mx >= 8 && mx < 84;
}

static void handle_mouse(int mx, int my, u8 buttons) {
    bool pressed_now = (buttons & 1) && !(last_buttons & 1);
    bool right_now   = (buttons & 2) && !(last_buttons & 2);
    bool released    = !(buttons & 1) && (last_buttons & 1);

    if (menu_open) {
        int over = menu_item_at(mx, my);
        if (over != menu_hover) { menu_hover = over; needs_composite = true; }
        if (pressed_now) {
            if (over >= 0) { menu_choose(over); return; }
            /* A click anywhere else dismisses it, and does nothing more. */
            menu_open = false;
            needs_composite = true;
            return;
        }
    }

    if (released) {
        /* Hand the release to whoever was being drawn in, before dropping
           the capture: a program needs to know a stroke ended. */
        if (mouse_capture && mouse_capture->owned_by_user) {
            window_t *w = mouse_capture;
            wm_event_t ev = { WM_EV_MOUSE,
                              mx - (w->x + WM_BORDER),
                              my - (w->y + WM_TITLE_H), 0, 0 };
            wm_push_event(w, &ev);
        }
        dragging = 0;
        mouse_capture = 0;
    }

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
        int lx = mx - (w->x + WM_BORDER), ly = my - (w->y + WM_TITLE_H);
        if (w->owned_by_user) {
            wm_event_t ev = { WM_EV_MOUSE, lx, ly, buttons, 0 };
            wm_push_event(w, &ev);
        } else if (w->on_mouse) {
            w->on_mouse(w, lx, ly, buttons, false);
        }
        return;
    }

    if (!pressed_now && !right_now) return;

    bool on_title = false, on_close = false;
    window_t *w = window_at(mx, my, &on_title, &on_close);

    if (!w) {
        /* Nothing under the pointer: the taskbar badge, or the desktop
           itself, both of which open the launcher. */
        if (on_taskbar_badge(mx, my)) {
            if (menu_open) { menu_open = false; needs_composite = true; }
            else open_menu_at(8, (int)fb_height() - TASKBAR_H - (MENU_N * MENU_ITEM + 12) - 6);
        } else if (my < (int)fb_height() - TASKBAR_H) {
            open_menu_at(mx, my);
        }
        return;
    }

    wm_raise(w);

    if (on_close) { wm_close(w); return; }

    if (on_title) {
        dragging = w;
        drag_off_x = mx - w->x;
        drag_off_y = my - w->y;
        return;
    }

    mouse_capture = w;
    {
        int lx = mx - (w->x + WM_BORDER), ly = my - (w->y + WM_TITLE_H);
        if (w->owned_by_user) {
            wm_event_t ev = { WM_EV_MOUSE, lx, ly, buttons | 0x80, 0 };
            wm_push_event(w, &ev);          /* 0x80 marks the initial press */
        } else if (w->on_mouse) {
            w->on_mouse(w, lx, ly, buttons, true);
        }
    }
}

void wm_quit(void) { running = false; }

void wm_run(void) {
    if (!fb_active()) { kprintf("the desktop needs a framebuffer\n"); return; }

    theme_init();

    running = true;
    menu_open = false;
    mouse_set_autodraw(false);          /* the manager draws the pointer */
    last_mx = mouse_x();
    last_my = mouse_y();
    last_buttons = mouse_buttons();
    needs_composite = true;
    last_theme_check = timer_ticks();

    while (running) {
        int mx = mouse_x(), my = mouse_y();
        u8 buttons = mouse_buttons();

        if (mx != last_mx || my != last_my || buttons != last_buttons) {
            last_mx = mx; last_my = my;
            handle_mouse(mx, my, buttons);
            last_buttons = buttons;
            needs_composite = true;
        }

        int c = kbd_trygetchar();
        if (c == 27) {                             /* escape */
            if (menu_open) { menu_open = false; needs_composite = true; }
            else break;
        } else if (c >= 0 && nwin > 0) {
            window_t *top = stack[nwin - 1];
            if (top->owned_by_user) {
                wm_event_t ev = { WM_EV_KEY, 0, 0, 0, (u32)c };
                wm_push_event(top, &ev);
            } else if (top->on_key) {
                top->on_key(top, (char)c);
            }
        }

        /* The settings window writes a file; this is what notices. Four
           times a second is well under what a person can perceive as lag
           and is a 512 byte read. */
        if (timer_ticks() - last_theme_check > timer_hz() / 4) {
            last_theme_check = timer_ticks();
            if (theme_reload()) needs_composite = true;
        }

        for (int i = 0; i < nwin; i++)
            if (stack[i]->dirty) needs_composite = true;

        /* The taskbar clock ticks, so repaint at least once a second even
           when nothing else changed. */
        static u64 last_tick;
        if (timer_ticks() - last_tick >= timer_hz()) {
            last_tick = timer_ticks();
            needs_composite = true;
        }

        if (needs_composite) {
            composite();
            needs_composite = false;
        }
    }

    while (nwin > 0) wm_close(stack[nwin - 1]);
    running = false;
    menu_open = false;
    mouse_set_autodraw(true);
}
