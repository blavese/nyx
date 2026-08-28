#pragma once
#include "types.h"

/* Whichever network card was found, behind one interface. */
bool netdev_init(void);
bool netdev_up(void);
bool netdev_send(const void *data, u16 len);
void netdev_poll(void);
const u8 *netdev_mac(void);
u32  netdev_rx_count(void);
u32  netdev_tx_count(void);
const char *netdev_name(void);
