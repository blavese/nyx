/* Building and launching ring 3 processes. */
#include "user.h"
#include "paging.h"
#include "pmm.h"
#include "sched.h"
#include "printf.h"
#include "string.h"
#include "fs.h"
#include "heap.h"
#include "elf.h"

#define USER_CODE_BASE  0x40000000u
#define USER_STACK_TOP  0x50000000u
#define USER_STACK_PAGES 4

extern u8 user_stub_start[], user_stub_end[];

/* Maps a fresh, zeroed frame into a user address space. Frames come from the
   identity mapped pool, so the kernel can still reach them by their physical
   address to fill them in. */
static u32 alloc_user_page(u32 dir, u32 virt) {
    u32 frame = pmm_alloc_frame();
    if (!frame) return 0;
    memset((void *)frame, 0, PAGE_SIZE);
    if (!map_page_in(dir, virt, frame, PTE_PRESENT | PTE_RW | PTE_USER)) {
        pmm_free_frame(frame);
        return 0;
    }
    return frame;
}

static bool build_stack(u32 dir) {
    for (u32 i = 1; i <= USER_STACK_PAGES; i++)
        if (!alloc_user_page(dir, USER_STACK_TOP - i * PAGE_SIZE)) return false;
    return true;
}

/* Copies a blob into consecutive user pages starting at base. */
static bool load_flat(u32 dir, u32 base, const u8 *data, u32 size) {
    u32 done = 0;
    while (done < size) {
        u32 frame = alloc_user_page(dir, base + done);
        if (!frame) return false;
        u32 n = size - done;
        if (n > PAGE_SIZE) n = PAGE_SIZE;
        memcpy((void *)frame, data + done, n);
        done += PAGE_SIZE;
    }
    return true;
}

int user_spawn_stub(const char *name) {
    u32 dir = paging_new_directory();
    if (!dir) return -1;

    u32 size = (u32)(user_stub_end - user_stub_start);
    if (!load_flat(dir, USER_CODE_BASE, user_stub_start, size) || !build_stack(dir)) {
        paging_free_directory(dir);
        return -2;
    }

    task_t *t = task_create_user(name, dir, USER_CODE_BASE, USER_STACK_TOP - 16);
    if (!t) { paging_free_directory(dir); return -3; }
    return (int)t->pid;
}

int user_spawn_flat(const char *name, const u8 *image, u32 size) {
    if (!image || size == 0) return -1;

    u32 dir = paging_new_directory();
    if (!dir) return -1;

    if (!load_flat(dir, USER_CODE_BASE, image, size) || !build_stack(dir)) {
        paging_free_directory(dir);
        return -2;
    }

    task_t *t = task_create_user(name, dir, USER_CODE_BASE, USER_STACK_TOP - 16);
    if (!t) { paging_free_directory(dir); return -3; }
    return (int)t->pid;
}

int user_spawn_elf(const char *name, const u8 *image, u32 size) {
    u32 dir = paging_new_directory();
    if (!dir) return ELF_ERR_MEMORY;

    u32 entry = 0;
    int rc = elf_load(dir, image, size, &entry);
    if (rc != ELF_OK) { paging_free_directory(dir); return rc; }

    if (!build_stack(dir)) { paging_free_directory(dir); return ELF_ERR_MEMORY; }

    task_t *t = task_create_user(name, dir, entry, USER_STACK_TOP - 16);
    if (!t) { paging_free_directory(dir); return ELF_ERR_MEMORY; }
    return (int)t->pid;
}
