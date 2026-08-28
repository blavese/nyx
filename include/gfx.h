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
