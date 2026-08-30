/* Linear framebuffer.
 *
 * Mode setting goes through the Bochs VBE dispatch interface: a pair of I/O
 * ports that QEMU's std VGA, VirtualBox and Bochs all implement. It needs no
 * real mode, no BIOS call and no bootloader cooperation, which is what makes
 * it usable from a kernel that is already in protected mode.
 *
 * Drawing goes to a back buffer in RAM and is pushed to the card in one go.
 * Reading from video memory over PCI is slow enough that compositing directly
 * in it is visibly sluggish. */
#include "fb.h"
#include "io.h"
#include "pci.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "printf.h"
#include "string.h"

#define VBE_INDEX 0x01CE
#define VBE_DATA  0x01CF

#define VBE_ID       0
#define VBE_XRES     1
#define VBE_YRES     2
#define VBE_BPP      3
#define VBE_ENABLE   4
#define VBE_BANK     5
#define VBE_VWIDTH   6
#define VBE_VHEIGHT  7
#define VBE_XOFF     8
#define VBE_YOFF     9

#define VBE_DISABLED 0x00
#define VBE_ENABLED  0x01
#define VBE_LFB      0x40

/* QEMU / Bochs std VGA */
#define VGA_VENDOR 0x1234
#define VGA_DEVICE 0x1111
/* VirtualBox ships the same VBE interface under a different id */
#define VBOX_VENDOR 0x80EE
#define VBOX_DEVICE 0xBEEF

static bool   active = false;
static u32    width, height, pitch;
static u8    *lfb;          /* mapped video memory */
static u8    *back;         /* back buffer we actually draw into */

static void vbe_write(u16 reg, u16 value) {
    outw(VBE_INDEX, reg);
    outw(VBE_DATA, value);
}

static u16 vbe_read(u16 reg) {
    outw(VBE_INDEX, reg);
    return inw(VBE_DATA);
}

bool fb_active(void) { return active; }
u32  fb_width(void)  { return width; }
u32  fb_height(void) { return height; }
u32  fb_pitch(void)  { return pitch; }
u8  *fb_pixels(void) { return back; }

bool fb_init(u32 w, u32 h) {
    active = false;

    /* Version 0xB0C2 or later understands the linear framebuffer bit. */
    u16 id = vbe_read(VBE_ID);
    if (id < 0xB0C0 || id > 0xB0CF) return false;

    /* The card's memory aperture is the first BAR of the VGA device. */
    pci_dev_t vga;
    if (!pci_find(VGA_VENDOR, VGA_DEVICE, &vga) &&
        !pci_find(VBOX_VENDOR, VBOX_DEVICE, &vga)) return false;

    u64 phys = vga.bar0 & 0xFFFFFFF0u;
    if (!phys) return false;

    vbe_write(VBE_ENABLE, VBE_DISABLED);
    vbe_write(VBE_XRES, (u16)w);
    vbe_write(VBE_YRES, (u16)h);
    vbe_write(VBE_BPP, 32);
    vbe_write(VBE_ENABLE, VBE_ENABLED | VBE_LFB);

    /* Confirm the card actually took the mode rather than assuming. */
    if (vbe_read(VBE_XRES) != (u16)w || vbe_read(VBE_YRES) != (u16)h) {
        vbe_write(VBE_ENABLE, VBE_DISABLED);
        return false;
    }

    width = w;
    height = h;
    pitch = w * 4;

    /* Map the aperture. It sits far above the identity mapped region, so it
       needs page table entries of its own. */
    u32 bytes = pitch * height;
    for (u64 off = 0; off < bytes; off += PAGE_SIZE) {
        if (!map_page(phys + off, phys + off, PTE_PRESENT | PTE_RW)) {
            vbe_write(VBE_ENABLE, VBE_DISABLED);
            return false;
        }
    }
    lfb = (u8 *)phys;

    back = (u8 *)kmalloc(bytes);
    if (!back) {
        vbe_write(VBE_ENABLE, VBE_DISABLED);
        return false;
    }

    active = true;
    fb_clear(0);
    fb_flush();
    return true;
}

void fb_put(u32 x, u32 y, u32 rgb) {
    if (!active || x >= width || y >= height) return;
    *(u32 *)(back + y * pitch + x * 4) = rgb;
}

u32 fb_get(u32 x, u32 y) {
    if (!active || x >= width || y >= height) return 0;
    return *(u32 *)(back + y * pitch + x * 4);
}

void fb_clear(u32 rgb) {
    if (!active) return;
    u32 *p = (u32 *)back;
    u32 n = width * height;
    for (u32 i = 0; i < n; i++) p[i] = rgb;
}

void fb_rect(u32 x, u32 y, u32 w, u32 h, u32 rgb) {
    if (!active) return;
    if (x >= width || y >= height) return;
    if (x + w > width)  w = width - x;
    if (y + h > height) h = height - y;
    for (u32 j = 0; j < h; j++) {
        u32 *row = (u32 *)(back + (y + j) * pitch) + x;
        for (u32 i = 0; i < w; i++) row[i] = rgb;
    }
}

void fb_frame(u32 x, u32 y, u32 w, u32 h, u32 rgb) {
    if (!active || w == 0 || h == 0) return;
    fb_rect(x, y, w, 1, rgb);
    fb_rect(x, y + h - 1, w, 1, rgb);
    fb_rect(x, y, 1, h, rgb);
    fb_rect(x + w - 1, y, 1, h, rgb);
}

void fb_flush(void) {
    if (!active) return;
    memcpy(lfb, back, pitch * height);
}

void fb_flush_rect(u32 x, u32 y, u32 w, u32 h) {
    if (!active) return;
    if (x >= width || y >= height) return;
    if (x + w > width)  w = width - x;
    if (y + h > height) h = height - y;
    for (u32 j = 0; j < h; j++) {
        u32 off = (y + j) * pitch + x * 4;
        memcpy(lfb + off, back + off, w * 4);
    }
}
