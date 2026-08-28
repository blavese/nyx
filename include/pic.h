#pragma once
#include "types.h"
void pic_init(void);
void pic_eoi(u8 irq);
void pic_mask(u8 irq);
void pic_unmask(u8 irq);
