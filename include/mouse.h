#pragma once
#include "types.h"

bool mouse_init(void);
bool mouse_present(void);
i32  mouse_x(void);
i32  mouse_y(void);
u8   mouse_buttons(void);
u32  mouse_moves(void);
void mouse_hide(void);
void mouse_set_autodraw(bool on);
void mouse_show(void);
