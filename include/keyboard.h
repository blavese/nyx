#pragma once
#include "types.h"

/* Keys that are not characters.
 *
 * A PS/2 keyboard reports these as a 0xE0 prefix followed by an ordinary
 * scancode, which is how the same byte means "P" and "page down" depending
 * on what came before it. They are given values above anything a byte can
 * hold so that one integer can carry either a character or a key. */
#define KEY_UP        0x100
#define KEY_DOWN      0x101
#define KEY_LEFT      0x102
#define KEY_RIGHT     0x103
#define KEY_HOME      0x104
#define KEY_END       0x105
#define KEY_PAGE_UP   0x106
#define KEY_PAGE_DOWN 0x107
#define KEY_DELETE    0x108
#define KEY_INSERT    0x109
#define KEY_F1        0x110      /* F1..F12 run consecutively */

#define KEY_IS_SPECIAL(k) ((k) >= 0x100)

void keyboard_init(void);
bool kbd_has_char(void);
char kbd_getchar(void);          /* blocking */
int  kbd_trygetchar(void);       /* -1 when empty */
