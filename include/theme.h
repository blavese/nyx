#pragma once
#include "types.h"

/* How the desktop looks.
 *
 * Everything the window manager draws takes its colours from here rather
 * than from constants, so a settings program in ring 3 can change the look
 * by writing a file. The file is plain "key value" lines, because it has to
 * be editable with the `write` command and readable by a person. */

#define THEME_FILE "/nyx.cfg"

typedef enum {
    WALLPAPER_PLAIN = 0,
    WALLPAPER_GRID,
    WALLPAPER_DOTS,
    WALLPAPER_GRADIENT,
    WALLPAPER_COUNT
} wallpaper_t;

typedef struct {
    u32 accent;          /* title bars, highlights, the pointer's own colour */
    u32 desktop;         /* the background behind everything */
    u32 surface;         /* window chrome and the taskbar */
    u32 text;
    u32 text_dim;

    wallpaper_t wallpaper;
    int  corner;         /* window corner radius, 0 for square */
    bool shadows;
    bool animate;        /* menus and highlights fade rather than snap */
} theme_t;

void theme_init(void);
const theme_t *theme(void);

/* Reads THEME_FILE if it exists. Returns true if anything changed, so the
   window manager knows to repaint. */
bool theme_reload(void);

/* Writes the current theme back out, which is how the settings program's
   choices survive a reboot. */
bool theme_save(void);

/* The named presets a settings program offers. */
#define THEME_PRESETS 6
const char *theme_preset_name(int i);
u32         theme_preset_accent(int i);
void        theme_apply_preset(int i);
int         theme_current_preset(void);
