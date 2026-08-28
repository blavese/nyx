/* Just enough HTTP to ask a server for a page and keep the answer. */
#include "http.h"
#include "tcp.h"
#include "net.h"
#include "fs.h"
#include "heap.h"
#include "printf.h"
#include "string.h"

#define BODY_CAP 16384

int http_get(const char *host, const char *path, const char *save_as) {
    ipv4_t ip = net_parse_ip(host);
    if (!ip && !net_resolve(host, &ip, 5000)) return HTTP_ERR_RESOLVE;

    char addr[20];
    net_format_ip(ip, addr);
    kprintf("connecting to %s (%s) port 80\n", host, addr);

    if (!tcp_connect(ip, 80, 6000)) return HTTP_ERR_CONNECT;

    /* HTTP/1.0 with an explicit close, so the server ends the body by
       closing the connection and we do not have to parse chunked encoding. */
    char req[512];
    u32 n = 0;
    const char *p;
    for (p = "GET "; *p; p++) req[n++] = *p;
    for (p = path; *p && n < sizeof(req) - 64; p++) req[n++] = *p;
    for (p = " HTTP/1.0"; *p; p++) req[n++] = *p;
    req[n++] = 13; req[n++] = 10;
    for (p = "Host: "; *p; p++) req[n++] = *p;
    for (p = host; *p && n < sizeof(req) - 32; p++) req[n++] = *p;
    req[n++] = 13; req[n++] = 10;
    for (p = "User-Agent: nyx/0.1"; *p; p++) req[n++] = *p;
    req[n++] = 13; req[n++] = 10;
    for (p = "Connection: close"; *p; p++) req[n++] = *p;
    req[n++] = 13; req[n++] = 10;
    req[n++] = 13; req[n++] = 10;

    if (!tcp_send(req, (u16)n)) { tcp_close(); return HTTP_ERR_SEND; }

    u8 *buf = (u8 *)kmalloc(BODY_CAP);
    if (!buf) { tcp_close(); return HTTP_ERR_MEMORY; }

    u32 got = tcp_recv(buf, BODY_CAP, 10000);
    tcp_close();

    if (got == 0) { kfree(buf); return HTTP_ERR_EMPTY; }

    /* Split the headers from the body at the blank line. */
    u32 body = 0;
    for (u32 i = 0; i + 3 < got; i++) {
        if (buf[i] == 13 && buf[i + 1] == 10 && buf[i + 2] == 13 && buf[i + 3] == 10) {
            body = i + 4;
            break;
        }
    }

    int status = 0;
    if (got > 12 && buf[0] == 'H') {
        status = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
    }

    u32 blen = got - body;
    if (save_as && blen) fs_write(save_as, buf + body, blen);

    kprintf("status %d, %d bytes of headers, %d bytes of body\n", status, body, blen);
    if (save_as && blen) kprintf("saved to %s\n", save_as);
    else if (blen) {
        u32 show = blen < 400 ? blen : 400;
        kprintf("---\n");
        for (u32 i = 0; i < show; i++) kputc((char)buf[body + i]);
        if (blen > show) kprintf("\n... %d more bytes\n", blen - show);
        kprintf("\n---\n");
    }

    kfree(buf);
    return status ? status : HTTP_ERR_EMPTY;
}
