#pragma once
#include "types.h"
#include "net.h"

bool tcp_connect(ipv4_t ip, u16 port, u32 timeout_ms);
bool tcp_send(const void *data, u16 len);
u32  tcp_recv(u8 *out, u32 cap, u32 timeout_ms);
void tcp_close(void);
bool tcp_connected(void);
void tcp_input(ipv4_t src, const u8 *p, u16 len);
