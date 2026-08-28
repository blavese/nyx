#pragma once
#include "types.h"

bool e1000_init(void);
bool e1000_up(void);
bool e1000_send(const void *data, u16 len);
void e1000_poll(void);
const u8 *e1000_mac(void);
u32  e1000_rx_count(void);
u32  e1000_tx_count(void);
