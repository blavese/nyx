#pragma once
#include "types.h"

/* Straight to the screen. */
void gfx_char(int x, int y, char c, u32 fg);
void gfx_text(int x, int y, const char *s, u32 fg);
void gfx_text_bg(int x, int y, const char *s, u32 fg, u32 bg);
int  gfx_text_width(const char *s);

/* Into an off-screen surface of w by h pixels. Everything clips. */
void surf_clear(u32 *px, int w, int h, u32 rgb);
void surf_rect(u32 *px, int w, int h, int x, int y, int rw, int rh, u32 rgb);
void surf_frame(u32 *px, int w, int h, int x, int y, int rw, int rh, u32 rgb);
void surf_disc(u32 *px, int w, int h, int cx, int cy, int r, u32 rgb);
void surf_line(u32 *px, int w, int h, int x0, int y0, int x1, int y1, int r, u32 rgb);
void surf_char(u32 *px, int w, int h, int x, int y, char c, u32 fg);
void surf_text(u32 *px, int w, int h, int x, int y, const char *s, u32 fg);
void surf_blit(const u32 *px, int w, int h, int dx, int dy);

/* Chrome drawing. These go straight to the screen, because the window
   manager composites there rather than into a surface of its own. */
void fb_round_rect(int x, int y, int w, int h, int r, u32 rgb);
void fb_round_frame(int x, int y, int w, int h, int r, u32 rgb);
void fb_shadow(int x, int y, int w, int h, int r, int spread);
void fb_vgradient(int x, int y, int w, int h, u32 top, u32 bottom);
u32  gfx_mix(u32 under, u32 over, int alpha);
