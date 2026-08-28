#pragma once
#include "types.h"

enum vga_color {
    VGA_BLACK, VGA_BLUE, VGA_GREEN, VGA_CYAN, VGA_RED, VGA_MAGENTA,
    VGA_BROWN, VGA_LGREY, VGA_DGREY, VGA_LBLUE, VGA_LGREEN, VGA_LCYAN,
    VGA_LRED, VGA_LMAGENTA, VGA_YELLOW, VGA_WHITE
};

void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_write(const char *s);
void vga_set_color(u8 fg, u8 bg);
u8   vga_get_color(void);
void vga_cursor(int row, int col);
void vga_get_cursor(int *row, int *col);
