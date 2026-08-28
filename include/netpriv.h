#pragma once
#include "types.h"
#include "net.h"

/* Shared between the IP layer and the protocols that sit on top of it. */
u16  np_hs(u16 v);          /* host to network, 16 bit */
u32  np_hl(u32 v);          /* host to network, 32 bit */
u32  np_nl(u32 v);          /* network to host, 32 bit */
bool np_ip_send(ipv4_t dst, u8 proto, const void *payload, u16 len);
