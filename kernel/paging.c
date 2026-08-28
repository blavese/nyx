/* Two-level x86 paging. The kernel identity-maps the low region so that
   turning paging on does not move the ground out from under itself. */
#include "paging.h"
#include "pmm.h"
#include "printf.h"
#include "string.h"
#include "idt.h"
#include "io.h"

#define ENTRIES     1024
#define IDENTITY_MB 16u

static u32 *page_directory;      /* 1024 page-directory entries */

static u32 *table_for(u32 virt, bool create, u32 flags) {
    u32 di = virt >> 22;
    if (!(page_directory[di] & PTE_PRESENT)) {
        if (!create) return 0;
        u32 frame = pmm_alloc_frame();
        if (!frame) return 0;
        memset((void *)frame, 0, PAGE_SIZE);       /* identity mapped, safe */
        page_directory[di] = frame | PTE_PRESENT | PTE_RW | (flags & PTE_USER);
    }
    return (u32 *)(page_directory[di] & ~0xFFFu);
}

bool map_page(u32 virt, u32 phys, u32 flags) {
    u32 *t = table_for(virt, true, flags);
    if (!t) return false;
    t[(virt >> 12) & 0x3FF] = (phys & ~0xFFFu) | (flags & 0xFFF) | PTE_PRESENT;
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
    return true;
}

void unmap_page(u32 virt) {
    u32 *t = table_for(virt, false, 0);
    if (!t) return;
    t[(virt >> 12) & 0x3FF] = 0;
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

u32 virt_to_phys(u32 virt) {
    u32 *t = table_for(virt, false, 0);
    if (!t) return 0;
    u32 e = t[(virt >> 12) & 0x3FF];
    if (!(e & PTE_PRESENT)) return 0;
    return (e & ~0xFFFu) | (virt & 0xFFF);
}

u32 paging_directory_phys(void) { return (u32)page_directory; }

static void page_fault(registers_t *r) {
    u32 cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    panic("page fault at %p  eip=%p  [%s %s %s]",
          (void *)cr2, (void *)r->eip,
          (r->err_code & 1) ? "protection" : "not-present",
          (r->err_code & 2) ? "write" : "read",
          (r->err_code & 4) ? "user" : "kernel");
}

void paging_init(void) {
    u32 pd = pmm_alloc_frame();
    if (!pd) panic("paging: no frame for the page directory");
    page_directory = (u32 *)pd;
    memset(page_directory, 0, PAGE_SIZE);

    /* Identity map the low IDENTITY_MB so kernel code, the frame bitmap and
       the heap all keep the addresses they already have. */
    for (u32 a = 0; a < IDENTITY_MB * 1024u * 1024u; a += PAGE_SIZE) {
        if (!map_page(a, a, PTE_PRESENT | PTE_RW))
            panic("paging: identity map failed at %p", (void *)a);
    }

    register_interrupt_handler(14, page_fault);

    __asm__ volatile ("mov %0, %%cr3" :: "r"((u32)page_directory));
    u32 cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;                    /* CR0.PG */
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0));
}
