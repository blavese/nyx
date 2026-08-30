/* The window server.
 *
 * A graphical program in ring 3 cannot touch the framebuffer and cannot be
 * handed a kernel pointer, so the surface it draws into has to be mapped
 * into its own address space. That is the whole job here:
 *
 *   - a window's pixels are allocated page aligned, so whole pages can be
 *     handed out without exposing anything else that shares them
 *   - those pages are mapped into the caller's directory with PTE_USER, at a
 *     fixed address the program is told about
 *   - the program draws into that memory directly, then asks for a repaint
 *
 * The kernel keeps the window; the program only ever gets a small integer
 * handle and a pointer to its own pixels. Nothing else crosses over.
 */
#include "winsrv.h"
#include "wm.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "sched.h"
#include "string.h"
#include "printf.h"

typedef struct {
    bool      used;
    u32       pid;             /* who owns it */
    window_t *win;
    u32      *raw;             /* what kmalloc returned, for kfree */
    u32      *pixels;          /* page aligned inside raw, and what is mapped */
    u64       bytes;           /* rounded up to whole pages */
    u64       user_addr;       /* where the owner sees it, 0 until mapped */
    u64       dir;             /* the address space it was mapped into */
} slot_t;

static slot_t slots[WINSRV_MAX];

void winsrv_init(void) {
    memset(slots, 0, sizeof(slots));
}

static slot_t *lookup(u32 pid, int handle) {
    if (handle < 0 || handle >= WINSRV_MAX) return 0;
    slot_t *s = &slots[handle];
    if (!s->used || s->pid != pid) return 0;
    return s;
}

/* The window manager calls this when the close button is used. The program
   still owns its handle; it finds out through a WM_EV_CLOSE event. */
static void on_wm_close(window_t *w) {
    for (int i = 0; i < WINSRV_MAX; i++)
        if (slots[i].used && slots[i].win == w) slots[i].win = 0;
}

static void free_slot(slot_t *s) {
    if (s->user_addr && s->dir) {
        for (u64 off = 0; off < s->bytes; off += PAGE_SIZE) {
            /* Only unmap where the directory is still the live one; a task
               that has already exited had its whole space torn down. */
            if (paging_current_directory() == s->dir)
                unmap_page(s->user_addr + off);
        }
    }
    if (s->win) { s->win->on_close = 0; wm_close(s->win); }
    if (s->raw) kfree(s->raw);
    memset(s, 0, sizeof(*s));
}

int winsrv_create(u32 pid, const char *title, int cw, int ch) {
    if (cw < 32 || ch < 32 || cw > 1600 || ch > 1200) return -1;

    int handle = -1;
    for (int i = 0; i < WINSRV_MAX; i++)
        if (!slots[i].used) { handle = i; break; }
    if (handle < 0) return -1;

    slot_t *s = &slots[handle];
    u64 bytes = ((u64)(cw * ch) * 4 + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    /* Over-allocate and align up. The heap is inside the identity mapped
       region, so an aligned virtual address is an aligned physical one, and
       the pages can be handed to a user directory as they are. */
    u32 *raw = (u32 *)kmalloc(bytes + PAGE_SIZE);
    if (!raw) return -1;
    u32 *pixels = (u32 *)(((u64)raw + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));
    memset(pixels, 0, bytes);

    /* Cascade windows so two programs do not open on top of each other. The
       vertical step has to clear a title bar, or the window underneath is
       there but cannot be clicked on. */
    int step = handle % 5;
    int x = 40 + step * 48, y = 36 + step * 38;
    window_t *w = wm_create(title, x, y, cw, ch);
    if (!w) { kfree(raw); return -1; }

    /* Point the window at the pages the program will get, and let the one
       the manager allocated go. */
    kfree(w->canvas);
    w->canvas = pixels;
    w->owned_by_user = true;
    w->on_close = on_wm_close;

    s->used = true;
    s->pid = pid;
    s->win = w;
    s->raw = raw;                 /* kfree wants the address kmalloc returned */
    s->pixels = pixels;           /* the mapping starts here, not at raw */
    s->bytes = bytes;
    s->user_addr = 0;
    s->dir = 0;
    return handle;
}

u64 winsrv_surface(u32 pid, int handle, u64 dir) {
    slot_t *s = lookup(pid, handle);
    if (!s) return 0;
    if (s->user_addr) return s->user_addr;

    u64 base = WINSRV_SURFACE_BASE + (u64)handle * WINSRV_SURFACE_STEP;
    for (u64 off = 0; off < s->bytes; off += PAGE_SIZE) {
        u64 phys = (u64)s->pixels + off;      /* identity mapped, so this is it */
        if (!map_page_in(dir, base + off, phys, PTE_PRESENT | PTE_RW | PTE_USER)) {
            for (u64 back = 0; back < off; back += PAGE_SIZE) unmap_page(base + back);
            return 0;
        }
    }

    s->user_addr = base;
    s->dir = dir;
    return base;
}

int winsrv_size(u32 pid, int handle) {
    slot_t *s = lookup(pid, handle);
    if (!s || !s->win) return -1;
    return (s->win->cw << 16) | (s->win->ch & 0xFFFF);
}

bool winsrv_poll(u32 pid, int handle, wm_event_t *out) {
    slot_t *s = lookup(pid, handle);
    if (!s) return false;
    if (!s->win) {
        /* The window is gone but the program has not noticed yet. */
        out->type = WM_EV_CLOSE;
        out->x = out->y = 0;
        out->buttons = out->key = 0;
        return true;
    }
    return wm_pop_event(s->win, out);
}

bool winsrv_commit(u32 pid, int handle) {
    slot_t *s = lookup(pid, handle);
    if (!s || !s->win) return false;
    wm_invalidate(s->win);
    return true;
}

bool winsrv_close(u32 pid, int handle) {
    slot_t *s = lookup(pid, handle);
    if (!s) return false;
    free_slot(s);
    return true;
}

window_t *winsrv_window(u32 pid, int handle) {
    slot_t *s = lookup(pid, handle);
    return s ? s->win : 0;
}

void winsrv_release(u32 pid) {
    for (int i = 0; i < WINSRV_MAX; i++)
        if (slots[i].used && slots[i].pid == pid) free_slot(&slots[i]);
}
