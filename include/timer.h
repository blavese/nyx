#pragma once
#include "types.h"
void timer_init(u32 hz);
u64  timer_ticks(void);
u32  timer_hz(void);
void sleep_ms(u32 ms);
