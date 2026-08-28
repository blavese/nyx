/* Picks a network card and hides which one it was from the stack above.
 *
 * e1000 is tried first because it is what VirtualBox and VMware present;
 * the RTL8139 is QEMU's older default and is kept as the fallback. */
#include "netdev.h"
#include "e1000.h"
#include "rtl8139.h"

typedef enum { NIC_NONE, NIC_E1000, NIC_RTL8139 } nic_t;
static nic_t nic = NIC_NONE;

bool netdev_init(void) {
    if (e1000_init())  { nic = NIC_E1000;   return true; }
    if (rtl_init())    { nic = NIC_RTL8139; return true; }
    nic = NIC_NONE;
    return false;
}

bool netdev_up(void) {
    switch (nic) {
        case NIC_E1000:   return e1000_up();
        case NIC_RTL8139: return rtl_up();
        default:          return false;
    }
}

bool netdev_send(const void *data, u16 len) {
    switch (nic) {
        case NIC_E1000:   return e1000_send(data, len);
        case NIC_RTL8139: return rtl_send(data, len);
        default:          return false;
    }
}

void netdev_poll(void) {
    switch (nic) {
        case NIC_E1000:   e1000_poll(); break;
        case NIC_RTL8139: rtl_poll();   break;
        default: break;
    }
}

const u8 *netdev_mac(void) {
    static const u8 zero[6] = { 0, 0, 0, 0, 0, 0 };
    switch (nic) {
        case NIC_E1000:   return e1000_mac();
        case NIC_RTL8139: return rtl_mac();
        default:          return zero;
    }
}

u32 netdev_rx_count(void) {
    switch (nic) {
        case NIC_E1000:   return e1000_rx_count();
        case NIC_RTL8139: return rtl_rx_count();
        default:          return 0;
    }
}

u32 netdev_tx_count(void) {
    switch (nic) {
        case NIC_E1000:   return e1000_tx_count();
        case NIC_RTL8139: return rtl_tx_count();
        default:          return 0;
    }
}

const char *netdev_name(void) {
    switch (nic) {
        case NIC_E1000:   return "e1000";
        case NIC_RTL8139: return "rtl8139";
        default:          return "none";
    }
}
