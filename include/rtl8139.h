#pragma once
#include "types.h"

bool rtl_init(void);
bool rtl_up(void);
bool rtl_send(const void *data, u16 len);
void rtl_poll(void);
const u8 *rtl_mac(void);
u32  rtl_rx_count(void);
u32  rtl_tx_count(void);
