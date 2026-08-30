/* Physical frame allocator: one bit per 4 KiB frame.
 *
 * The bitmap lives immediately after the kernel image and marks itself used,
 * so it can never hand itself out.
 *
 * It is fed from the handoff structure rather than from multiboot, because
 * the two loaders that matter both build one and a machine that booted
 * through UEFI has no multiboot information at all. Anything the firmware
 * called reserved stays reserved: on a real machine that includes the
 * framebuffer, the tables and whatever the firmware still owns.
 */
#include "pmm.h"
#include "printf.h"
#include "string.h"
#include "paging.h"

extern u8 __kernel_start[], __kernel_end[];

static u32 *bitmap;
static u64  bitmap_bytes;
static u64  total_frames;
static u64  used_frames;

#define FRAME_IDX(a) ((a) / PAGE_SIZE)
#define IDX_ADDR(i)  ((u64)(i) * PAGE_SIZE)

static void mark_used(u64 frame) {
    if (frame >= total_frames) return;
    u64 i = frame / 32, b = frame % 32;
    if (!(bitmap[i] & (1u << b))) { bitmap[i] |= (1u << b); used_frames++; }
}

static void mark_free(u64 frame) {
    if (frame >= total_frames) return;
    u64 i = frame / 32, b = frame % 32;
    if (bitmap[i] & (1u << b)) { bitmap[i] &= ~(1u << b); used_frames--; }
}

static bool is_used(u64 frame) {
    if (frame >= total_frames) return true;
    return (bitmap[frame / 32] & (1u << (frame % 32))) != 0;
}

void pmm_reserve(u64 start, u64 size) {
    u64 first = start & ~(u64)(PAGE_SIZE - 1);
    u64 last = (start + size + PAGE_SIZE - 1) & ~(u64)(PAGE_SIZE - 1);
    for (u64 a = first; a < last; a += PAGE_SIZE) mark_used(FRAME_IDX(a));
}

void pmm_init(const handoff_t *h) {
    /* How much memory there is, which is the highest address the loader
       described as usable. Anything above that is not memory. */
    u64 highest = 0;
    for (u64 i = 0; i < h->region_count; i++) {
        const mem_region_t *r = &h->regions[i];
        if (r->type != MEM_USABLE) continue;
        if (r->base + r->len > highest) highest = r->base + r->len;
    }

    /* A 32-bit kernel could only ever see 4 GiB. This one could see more,
       but the bitmap and the identity map both have to cover what it
       claims, so it is capped at what is actually mapped. */
    u64 cap = KERNEL_SPACE_MB * 1024ull * 1024ull;
    if (highest > cap) highest = cap;
    if (highest == 0) highest = 64ull * 1024 * 1024;    /* a loader that said nothing */

    total_frames = highest / PAGE_SIZE;
    bitmap_bytes = (total_frames + 7) / 8;
    bitmap = (u32 *)(((u64)__kernel_end + PAGE_SIZE - 1) & ~(u64)(PAGE_SIZE - 1));

    /* Start with everything owned by nobody, then release what the loader
       said is real memory. */
    memset(bitmap, 0xFF, bitmap_bytes);
    used_frames = total_frames;

    for (u64 i = 0; i < h->region_count; i++) {
        const mem_region_t *r = &h->regions[i];
        if (r->type != MEM_USABLE) continue;
        u64 a = (r->base + PAGE_SIZE - 1) & ~(u64)(PAGE_SIZE - 1);
        u64 z = (r->base + r->len) & ~(u64)(PAGE_SIZE - 1);
        for (; a + PAGE_SIZE <= z && a < highest; a += PAGE_SIZE)
            mark_free(FRAME_IDX(a));
    }

    /* Never hand out the first megabyte: the interrupt vectors, the BIOS
       data area and the SMP trampoline all live there. */
    for (u64 a = 0; a < 0x100000; a += PAGE_SIZE) mark_used(FRAME_IDX(a));

    /* Nor the kernel, nor the bitmap that describes everything else. */
    u64 kstart = (u64)__kernel_start & ~(u64)(PAGE_SIZE - 1);
    u64 kend = ((u64)bitmap + bitmap_bytes + PAGE_SIZE - 1) & ~(u64)(PAGE_SIZE - 1);
    for (u64 a = kstart; a < kend; a += PAGE_SIZE) mark_used(FRAME_IDX(a));
}

u64 pmm_alloc_frame(void) {
    for (u64 i = 0; i < total_frames; i++) {
        if (is_used(i)) continue;
        mark_used(i);
        return IDX_ADDR(i);
    }
    return 0;
}

void pmm_free_frame(u64 addr) {
    if (!addr) return;
    mark_free(FRAME_IDX(addr));
}

u64 pmm_total_frames(void) { return total_frames; }
u64 pmm_used_frames(void)  { return used_frames; }
u64 pmm_free_frames(void)  { return total_frames - used_frames; }
