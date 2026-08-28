/* A single-connection TCP client.
 *
 * Enough of TCP to open a connection, send a request and read the answer:
 * the three way handshake, sequence and acknowledgement tracking, and an
 * orderly close. There is no congestion control, no retransmission and only
 * one connection at a time. That is honest for what it is used for here,
 * which is fetching a page over a link that does not lose packets. */
#include "tcp.h"
#include "net.h"
#include "netpriv.h"
#include "timer.h"
#include "printf.h"
#include "string.h"
#include "heap.h"

#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PSH 0x08
#define TH_ACK 0x10

typedef struct {
    u16 sport, dport;
    u32 seq, ack;
    u8  offset, flags;
    u16 window, csum, urgent;
} __attribute__((packed)) tcp_t;

typedef enum { T_CLOSED, T_SYNSENT, T_OPEN, T_CLOSING, T_DONE } tstate_t;

static volatile tstate_t state = T_CLOSED;
static ipv4_t peer_ip;
static u16    peer_port, local_port;
static volatile u32 snd_nxt, rcv_nxt;
static volatile bool got_fin;

#define RXCAP 16384
static u8 *rxbuf;
static volatile u32 rxlen;

static u16 tcp_checksum(ipv4_t src, ipv4_t dst, const u8 *seg, u16 len) {
    /* The pseudo header covers the addresses so a misdelivered segment
       cannot pass the check. */
    u8 ph[12];
    ph[0] = (u8)(src >> 24); ph[1] = (u8)(src >> 16); ph[2] = (u8)(src >> 8); ph[3] = (u8)src;
    ph[4] = (u8)(dst >> 24); ph[5] = (u8)(dst >> 16); ph[6] = (u8)(dst >> 8); ph[7] = (u8)dst;
    ph[8] = 0; ph[9] = 6;
    ph[10] = (u8)(len >> 8); ph[11] = (u8)len;

    u32 sum = 0;
    for (int i = 0; i < 12; i += 2) sum += (u32)((ph[i] << 8) | ph[i + 1]);
    u16 i = 0;
    for (; i + 1 < len; i += 2) sum += (u32)((seg[i] << 8) | seg[i + 1]);
    if (i < len) sum += (u32)(seg[i] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)(~sum & 0xFFFF);
}

static bool send_seg(u8 flags, const void *data, u16 dlen) {
    u8 seg[1500];
    if (sizeof(tcp_t) + dlen > sizeof(seg)) return false;

    tcp_t *t = (tcp_t *)seg;
    t->sport = np_hs(local_port);
    t->dport = np_hs(peer_port);
    t->seq = np_hl(snd_nxt);
    t->ack = np_hl(rcv_nxt);
    t->offset = 5 << 4;                       /* 20 byte header, no options */
    t->flags = flags;
    t->window = np_hs(8192);
    t->csum = 0;
    t->urgent = 0;
    if (dlen) memcpy(seg + sizeof(tcp_t), data, dlen);

    u16 total = (u16)(sizeof(tcp_t) + dlen);
    t->csum = np_hs(tcp_checksum(net_ip(), peer_ip, seg, total));

    return np_ip_send(peer_ip, 6, seg, total);
}

/* Called by the IP layer for every arriving TCP segment. */
void tcp_input(ipv4_t src, const u8 *p, u16 len) {
    if (state == T_CLOSED || len < sizeof(tcp_t)) return;
    const tcp_t *t = (const tcp_t *)p;
    if (src != peer_ip) return;
    if (np_hs(t->dport) != local_port) return;

    u16 hlen = (u16)((t->offset >> 4) * 4);
    if (hlen < sizeof(tcp_t) || hlen > len) return;
    const u8 *data = p + hlen;
    u16 dlen = (u16)(len - hlen);

    if (t->flags & TH_RST) { state = T_DONE; return; }

    if (state == T_SYNSENT) {
        if ((t->flags & (TH_SYN | TH_ACK)) == (TH_SYN | TH_ACK)) {
            rcv_nxt = np_nl(t->seq) + 1;
            snd_nxt = np_nl(t->ack);
            state = T_OPEN;
            send_seg(TH_ACK, 0, 0);
        }
        return;
    }

    if (dlen) {
        /* Only accept the next in-order segment; anything else is dropped
           and the peer will resend. */
        if (np_nl(t->seq) == rcv_nxt) {
            u32 room = RXCAP - rxlen;
            u32 n = dlen < room ? dlen : room;
            if (n) { memcpy(rxbuf + rxlen, data, n); rxlen += n; }
            rcv_nxt += dlen;
            send_seg(TH_ACK, 0, 0);
        } else {
            send_seg(TH_ACK, 0, 0);           /* duplicate ack */
        }
    }

    if (t->flags & TH_FIN) {
        rcv_nxt++;
        got_fin = true;
        send_seg(TH_ACK, 0, 0);
        if (state == T_OPEN) {
            send_seg(TH_FIN | TH_ACK, 0, 0);
            snd_nxt++;
            state = T_CLOSING;
        } else {
            state = T_DONE;
        }
    }
}

bool tcp_connect(ipv4_t ip, u16 port, u32 timeout_ms) {
    if (!rxbuf) {
        rxbuf = (u8 *)kmalloc(RXCAP);
        if (!rxbuf) return false;
    }
    peer_ip = ip;
    peer_port = port;
    local_port = (u16)(45000 + (timer_ticks() & 0x0FFF));
    snd_nxt = 0x4E595800u ^ (u32)(timer_ticks() * 2654435761u);
    rcv_nxt = 0;
    rxlen = 0;
    got_fin = false;
    state = T_SYNSENT;

    if (!send_seg(TH_SYN, 0, 0)) { state = T_CLOSED; return false; }
    snd_nxt++;

    u64 deadline = timer_ticks() + (timeout_ms * timer_hz()) / 1000u;
    while (state == T_SYNSENT && timer_ticks() < deadline) net_poll();

    if (state != T_OPEN) { state = T_CLOSED; return false; }
    return true;
}

bool tcp_send(const void *data, u16 len) {
    if (state != T_OPEN) return false;
    if (!send_seg(TH_PSH | TH_ACK, data, len)) return false;
    snd_nxt += len;
    return true;
}

u32 tcp_recv(u8 *out, u32 cap, u32 timeout_ms) {
    u64 deadline = timer_ticks() + (timeout_ms * timer_hz()) / 1000u;
    u64 quiet = timer_ticks() + (timer_hz() * 2);

    while (timer_ticks() < deadline) {
        u32 before = rxlen;
        net_poll();
        if (rxlen > before) quiet = timer_ticks() + (timer_hz() * 2);
        if (got_fin) break;
        if (rxlen && timer_ticks() > quiet) break;   /* peer went quiet */
    }

    u32 n = rxlen < cap ? rxlen : cap;
    if (n) memcpy(out, rxbuf, n);
    return n;
}

void tcp_close(void) {
    if (state == T_OPEN) {
        send_seg(TH_FIN | TH_ACK, 0, 0);
        snd_nxt++;
        state = T_CLOSING;
        u64 deadline = timer_ticks() + timer_hz();
        while (state == T_CLOSING && timer_ticks() < deadline) net_poll();
    }
    state = T_CLOSED;
}

bool tcp_connected(void) { return state == T_OPEN; }
