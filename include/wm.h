#pragma once
#include "types.h"

#define WM_MAX_WINDOWS 8
#define WM_TITLE_H     24
#define WM_BORDER      1

typedef struct window window_t;

/* Buttons are the raw PS/2 bitmask: bit 0 left, bit 1 right. */
typedef void (*wm_mouse_fn)(window_t *w, int x, int y, u8 buttons, bool just_pressed);
typedef void (*wm_key_fn)(window_t *w, char c);

struct window {
    int   x, y;               /* outer top-left, including the title bar */
    int   cw, ch;             /* content size */
    char  title[32];
    u32  *canvas;             /* cw * ch pixels, owned by the window */
    bool  open;
    bool  dirty;              /* content changed since the last composite */
    wm_mouse_fn on_mouse;
    wm_key_fn   on_key;
    void (*on_close)(window_t *w);   /* lets an app drop its handle */
    void *data;
};

window_t *wm_create(const char *title, int x, int y, int cw, int ch);
void wm_close(window_t *w);
void wm_invalidate(window_t *w);
void wm_raise(window_t *w);

/* Runs the desktop until the user leaves it. */
void wm_run(void);
void wm_quit(void);
bool wm_active(void);

int  wm_outer_w(const window_t *w);
int  wm_outer_h(const window_t *w);
