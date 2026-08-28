#pragma once
#include "types.h"

void fbcon_init(void);
void fbcon_putc(char c);
void fbcon_clear(void);
void fbcon_set_color(u8 fg, u8 bg);
u32  fbcon_cols(void);
u32  fbcon_rows(void);
