#pragma once
#include "types.h"

#define FONT_W 8
#define FONT_H 16
#define FONT_FIRST 32
#define FONT_LAST  126

extern const u8 font8x16[FONT_LAST - FONT_FIRST + 1][FONT_H];
