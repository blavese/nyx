#pragma once
#include "types.h"

#define ETH_ALEN 6

typedef u32 ipv4_t;      /* stored in host byte order */

void  net_init(void);
void  net_receive(const u8 *frame, u16 len);
void  net_poll(void);

bool  net_up(void);
ipv4_t net_ip(void);
ipv4_t net_gateway(void);
ipv4_t net_netmask(void);
ipv4_t net_dns(void);
const u8 *net_mac(void);

/* Runs the full DHCP handshake. Returns true once an address is bound. */
bool  net_dhcp(u32 timeout_ms);

/* Sends an echo request and waits for the reply. Returns round trip in
   milliseconds, or -1 on timeout. */
int   net_ping(ipv4_t dst, u32 timeout_ms);

bool  net_udp_send(ipv4_t dst, u16 sport, u16 dport, const void *data, u16 len);

/* Resolves a name through the DHCP-supplied resolver. */
bool  net_resolve(const char *host, ipv4_t *out, u32 timeout_ms);

ipv4_t net_parse_ip(const char *s);
void   net_format_ip(ipv4_t ip, char *out);

u32   net_rx_packets(void);
u32   net_tx_packets(void);
