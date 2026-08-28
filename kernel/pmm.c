/* Physical frame allocator: one bit per 4 KiB frame. The bitmap itself lives
   immediately after the kernel image, and is marked used so it cannot hand
   itself out. */
#include "pmm.h"
#include "printf.h"
#include "string.h"

extern u8 __kernel_start[], __kernel_end[];

static u32 *bitmap;
static u32  bitmap_bytes;
static u32  total_frames;
static u32  used_frames;

#define FRAME_IDX(a) ((a) / PAGE_SIZE)
#define IDX_ADDR(i)  ((i) * PAGE_SIZE)

static void mark_used(u32 frame) {
    if (frame >= total_frames) return;
    u32 i = frame / 32, b = frame % 32;
    if (!(bitmap[i] & (1u << b))) { bitmap[i] |= (1u << b); used_frames++; }
}
static void mark_free(u32 frame) {
    if (frame >= total_frames) return;
    u32 i = frame / 32, b = frame % 32;
    if (bitmap[i] & (1u << b)) { bitmap[i] &= ~(1u << b); used_frames--; }
}
static bool is_used(u32 frame) {
    if (frame >= total_frames) return true;
    return (bitmap[frame / 32] & (1u << (frame % 32))) != 0;
}

void pmm_init(multiboot_info_t *mbi) {
    /* Work out how much physical memory exists. */
    u64 highest = 0;
    if (mbi->flags & (1 << 6)) {
        u32 p = mbi->mmap_addr;
        u32 end = mbi->mmap_addr + mbi->mmap_length;
        while (p < end) {
            mb_mmap_entry_t *e = (mb_mmap_entry_t *)p;
            if (e->type == 1 && e->addr + e->len > highest) highest = e->addr + e->len;
            p += e->size + 4;
        }
    }
    if (highest == 0) highest = ((u64)mbi->mem_upper + 1024) * 1024;   /* fallback */
    if (highest > 0xFFFFF000ull) highest = 0xFFFFF000ull;

    total_frames = (u32)(highest / PAGE_SIZE);
    bitmap_bytes = (total_frames + 7) / 8;
    bitmap = (u32 *)(((u32)__kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

    /* Start with everything owned by nobody, then release what the firmware
       says is real RAM. */
    memset(bitmap, 0xFF, bitmap_bytes);
    used_frames = total_frames;

    if (mbi->flags & (1 << 6)) {
        u32 p = mbi->mmap_addr;
        u32 end = mbi->mmap_addr + mbi->mmap_length;
        while (p < end) {
            mb_mmap_entry_t *e = (mb_mmap_entry_t *)p;
            if (e->type == 1) {
                u64 a = e->addr, z = e->addr + e->len;
                for (u64 x = a; x + PAGE_SIZE <= z && x < highest; x += PAGE_SIZE)
                    mark_free(FRAME_IDX((u32)x));
            }
            p += e->size + 4;
        }
    } else {
        for (u32 x = 0x100000; x < highest; x += PAGE_SIZE) mark_free(FRAME_IDX(x));
    }

    /* Never hand out the first megabyte, the kernel, or the bitmap. */
    for (u32 a = 0; a < 0x100000; a += PAGE_SIZE) mark_used(FRAME_IDX(a));
    u32 kstart = (u32)__kernel_start & ~(PAGE_SIZE - 1);
    u32 kend   = ((u32)bitmap + bitmap_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (u32 a = kstart; a < kend; a += PAGE_SIZE) mark_used(FRAME_IDX(a));
}

void pmm_reserve(u32 start, u32 size) {
    u32 first = start & ~(PAGE_SIZE - 1);
    u32 last  = (start + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (u32 a = first; a < last; a += PAGE_SIZE) mark_used(FRAME_IDX(a));
}

u32 pmm_alloc_frame(void) {
    for (u32 i = 0; i < total_frames; i++) {
        if (!is_used(i)) { mark_used(i); return IDX_ADDR(i); }
    }
    return 0;
}

void pmm_free_frame(u32 addr) { mark_free(FRAME_IDX(addr)); }

u32 pmm_total_frames(void) { return total_frames; }
u32 pmm_used_frames(void)  { return used_frames; }
u32 pmm_free_frames(void)  { return total_frames - used_frames; }
