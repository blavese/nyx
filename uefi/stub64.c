/* A stand-in kernel, to prove the UEFI path before there is a real one.
 *
 * The firmware has already put the machine in 64-bit long mode with memory
 * identity mapped, so this is entered with nothing to set up: it is handed a
 * pointer and the screen is already live. */
#include "../include/handoff.h"

__attribute__((section(".entry"), used))
void kentry(handoff_t *h) {
    if (h->magic != HANDOFF_MAGIC) for (;;) __asm__ volatile ("hlt");

    unsigned int *fb = (unsigned int *)h->fb_base;
    if (!fb) for (;;) __asm__ volatile ("hlt");

    /* A gradient, so a wrong pitch shows up as a slant rather than as a
       plausible flat colour. */
    for (unsigned int y = 0; y < h->fb_height; y++)
        for (unsigned int x = 0; x < h->fb_width; x++)
            fb[(unsigned long long)y * h->fb_pitch + x] =
                ((x * 255 / h->fb_width) << 16) | ((y * 255 / h->fb_height) << 8) | 0x40;

    /* A solid block whose size counts the usable memory regions, so the
       handoff is proven to have survived rather than merely to exist. */
    unsigned long long usable = 0;
    for (unsigned long long i = 0; i < h->region_count; i++)
        if (h->regions[i].type == MEM_USABLE) usable++;

    for (unsigned int y = 40; y < 90; y++)
        for (unsigned int x = 40; x < 40 + (unsigned int)usable * 12; x++)
            fb[(unsigned long long)y * h->fb_pitch + x] = 0x00FFFFFF;

    for (;;) __asm__ volatile ("hlt");
}
