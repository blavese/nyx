#pragma once
#include "types.h"

/* Linear framebuffer, set up through the Bochs VBE dispatch interface that
   QEMU's std VGA implements. */

bool fb_init(u32 width, u32 height);
bool fb_active(void);

u32  fb_width(void);
u32  fb_height(void);
u32  fb_pitch(void);       /* bytes per scanline */
u8  *fb_pixels(void);

void fb_clear(u32 rgb);
void fb_put(u32 x, u32 y, u32 rgb);
u32  fb_get(u32 x, u32 y);
void fb_rect(u32 x, u32 y, u32 w, u32 h, u32 rgb);
void fb_frame(u32 x, u32 y, u32 w, u32 h, u32 rgb);

/* Everything is drawn into a back buffer; this pushes it to the card. */
void fb_flush(void);
void fb_flush_rect(u32 x, u32 y, u32 w, u32 h);

#define RGB(r, g, b) (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
