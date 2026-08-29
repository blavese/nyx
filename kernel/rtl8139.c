/* Realtek RTL8139 driver.
 *
 * The card DMAs received frames into one circular buffer that the driver
 * walks, and transmits out of four descriptors used round robin. It is the
 * simplest real NIC to bring up, which is why every hobby kernel has one. */
#include "rtl8139.h"
#include "pci.h"
#include "io.h"
#include "idt.h"
#include "pic.h"
#include "heap.h"
#include "printf.h"
#include "string.h"
#include "net.h"

#define VENDOR 0x10EC
#define DEVICE 0x8139

/* register offsets from the I/O base */
#define REG_MAC      0x00
#define REG_MAR      0x08
#define REG_TSD0     0x10
#define REG_TSAD0    0x20
#define REG_RBSTART  0x30
#define REG_CMD      0x37
#define REG_CAPR     0x38
#define REG_CBR      0x3A
#define REG_IMR      0x3C
#define REG_ISR      0x3E
#define REG_TCR      0x40
#define REG_RCR      0x44
#define REG_CONFIG1  0x52

#define CMD_RESET    0x10
#define CMD_RX_EN    0x08
#define CMD_TX_EN    0x04
#define CMD_BUFE     0x01

#define ISR_ROK      0x0001
#define ISR_TOK      0x0004

#define RX_BUF_SIZE  8192
#define RX_PAD       (16 + 1500)

static pci_dev_t dev;
static u16 io_base;
static u8 *rx_buf;
static u16 rx_offset;
static u8 *tx_buf[4];
static int tx_slot;
static u8 mac[6];
static bool up = false;
static u32 rx_count, tx_count;

bool rtl_up(void) { return up; }
const u8 *rtl_mac(void) { return mac; }
u32 rtl_rx_count(void) { return rx_count; }
u32 rtl_tx_count(void) { return tx_count; }

static void handle_rx(void) {
    /* Walk every frame the card has left in the ring since last time. */
    while (!(inb(io_base + REG_CMD) & CMD_BUFE)) {
        u16 off = rx_offset % RX_BUF_SIZE;
        u16 status = *(u16 *)(rx_buf + off);
        u16 len    = *(u16 *)(rx_buf + off + 2);

        if (len < 4 || len > 1518 + 4) {
            /* Ring is out of step; resetting the receiver is the only
               reliable way back. */
            rx_offset = 0;
            outw(io_base + REG_CAPR, (u16)(0 - 16));
            return;
        }

        if (status & 0x01) {                 /* ROK on this frame */
            u8 *frame = rx_buf + off + 4;
            u16 flen = (u16)(len - 4);       /* drop the trailing CRC */
            rx_count++;
            net_receive(frame, flen);
        }

        rx_offset = (u16)((rx_offset + len + 4 + 3) & ~3u);
        rx_offset %= RX_BUF_SIZE;
        outw(io_base + REG_CAPR, (u16)(rx_offset - 16));
    }
}

static void rtl_isr(registers_t *r) {
    (void)r;
    u16 isr = inw(io_base + REG_ISR);
    outw(io_base + REG_ISR, isr);            /* acknowledge by writing back */
    if (isr & ISR_ROK) handle_rx();
}

bool rtl_send(const void *data, u16 len) {
    if (!up || len == 0 || len > 1792) return false;

    /* Pad to the ethernet minimum with the zeros already in the buffer,
       rather than by copying past the end of the caller's frame. */
    u16 total = len < 60 ? 60 : len;

    u8 *buf = tx_buf[tx_slot];
    memset(buf, 0, 1792);
    memcpy(buf, data, len);

    outl(io_base + REG_TSAD0 + tx_slot * 4, (u32)buf);
    outl(io_base + REG_TSD0  + tx_slot * 4, total);

    /* Wait for the card to take it. Bit 15 (OWN) goes high when the buffer
       is free again, bit 13 (TOK) when it actually went out. */
    for (u32 i = 0; i < 10000000u; i++) {
        u32 st = inl(io_base + REG_TSD0 + tx_slot * 4);
        if (st & 0x8000) break;
    }

    tx_slot = (tx_slot + 1) & 3;
    tx_count++;
    return true;
}

bool rtl_init(void) {
    up = false;
    if (!pci_find(VENDOR, DEVICE, &dev)) return false;

    pci_enable_bus_master(&dev);
    io_base = (u16)(dev.bar0 & ~0x3u);

    outb(io_base + REG_CONFIG1, 0x00);       /* out of low power */

    outb(io_base + REG_CMD, CMD_RESET);
    for (u32 i = 0; i < 10000000u; i++)
        if (!(inb(io_base + REG_CMD) & CMD_RESET)) break;

    rx_buf = (u8 *)kmalloc(RX_BUF_SIZE + RX_PAD);
    if (!rx_buf) return false;
    memset(rx_buf, 0, RX_BUF_SIZE + RX_PAD);
    /* The heap sits inside the identity mapped region, so the address the
       kernel sees is the address the card needs. */
    outl(io_base + REG_RBSTART, (u32)rx_buf);

    for (int i = 0; i < 4; i++) {
        tx_buf[i] = (u8 *)kmalloc(1792);
        if (!tx_buf[i]) return false;
        memset(tx_buf[i], 0, 1792);
    }
    tx_slot = 0;
    rx_offset = 0;

    outw(io_base + REG_IMR, ISR_ROK | ISR_TOK);
    /* accept broadcast, multicast, our own address and anything else,
       plus wrap so a frame can run past the end of the ring */
    outl(io_base + REG_RCR, 0x0F | (1 << 7));
    outb(io_base + REG_CMD, CMD_RX_EN | CMD_TX_EN);

    for (int i = 0; i < 6; i++) mac[i] = inb(io_base + REG_MAC + i);

    register_interrupt_handler(32 + dev.irq, rtl_isr);
    pic_unmask(dev.irq);

    up = true;
    return true;
}

void rtl_poll(void) {
    if (!up) return;
    /* Same reentrancy hazard as the e1000: the interrupt handler and this
       both drain the ring, so they must not overlap. */
    bool were_on = interrupts_enabled();
    cli();
    if (!(inb(io_base + REG_CMD) & CMD_BUFE)) handle_rx();
    if (were_on) sti();
}
