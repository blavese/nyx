#pragma once
#include "types.h"
void keyboard_init(void);
bool kbd_has_char(void);
char kbd_getchar(void);          /* blocking */
int  kbd_trygetchar(void);       /* -1 when empty */
