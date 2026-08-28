/* First-fit free list with boundary tags and coalescing on free. */
#include "heap.h"
#include "printf.h"
#include "string.h"

typedef struct block {
    u32 size;              /* payload bytes */
    bool free;
    struct block *next, *prev;
} block_t;

#define HDR sizeof(block_t)
#define MIN_SPLIT 16

static block_t *head;
static u32 total, used;

void heap_init(u32 start, u32 size) {
    head = (block_t *)start;
    head->size = size - HDR;
    head->free = true;
    head->next = head->prev = 0;
    total = size;
    used = 0;
}

static void split(block_t *b, size_t n) {
    if (b->size < n + HDR + MIN_SPLIT) return;
    block_t *nb = (block_t *)((u8 *)b + HDR + n);
    nb->size = b->size - n - HDR;
    nb->free = true;
    nb->next = b->next;
    nb->prev = b;
    if (b->next) b->next->prev = nb;
    b->next = nb;
    b->size = (u32)n;
}

void *kmalloc(size_t n) {
    if (n == 0) return 0;
    n = (n + 7) & ~7u;                        /* 8-byte align */
    for (block_t *b = head; b; b = b->next) {
        if (b->free && b->size >= n) {
            split(b, n);
            b->free = false;
            used += b->size + HDR;
            return (u8 *)b + HDR;
        }
    }
    return 0;
}

void *kcalloc(size_t n) {
    void *p = kmalloc(n);
    if (p) memset(p, 0, n);
    return p;
}

void kfree(void *p) {
    if (!p) return;
    block_t *b = (block_t *)((u8 *)p - HDR);
    if (b->free) return;
    b->free = true;
    used -= b->size + HDR;

    if (b->next && b->next->free) {           /* merge forward */
        b->size += HDR + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    if (b->prev && b->prev->free) {           /* merge backward */
        b->prev->size += HDR + b->size;
        b->prev->next = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

u32 heap_used(void)  { return used; }
u32 heap_total(void) { return total; }
