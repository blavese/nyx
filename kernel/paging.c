/* Two-level x86 paging. The kernel identity-maps the low region so that
   turning paging on does not move the ground out from under itself. */
#include "paging.h"
#include "pmm.h"
#include "printf.h"
#include "string.h"
#include "idt.h"
#include "io.h"

#define ENTRIES     1024

static u32 *kernel_directory;    /* the one the kernel boots on */
static u32 *current_directory;   /* whichever is loaded in cr3 */

/* Directories live in identity mapped memory, so a physical address is also
   a usable pointer. That is what keeps this code from needing a recursive
   mapping or a temporary window to edit a foreign address space. */
static u32 *table_for_dir(u32 *dir, u32 virt, bool create, u32 flags) {
    u32 di = virt >> 22;
    if (!(dir[di] & PTE_PRESENT)) {
        if (!create) return 0;
        u32 frame = pmm_alloc_frame();
        if (!frame) return 0;
        memset((void *)frame, 0, PAGE_SIZE);
        dir[di] = frame | PTE_PRESENT | PTE_RW | (flags & PTE_USER);
    }
    /* A table shared with the kernel may have been created without the user
       bit; widen it if a user mapping now needs to go through it. */
    if (flags & PTE_USER) dir[di] |= PTE_USER;
    return (u32 *)(dir[di] & ~0xFFFu);
}

static bool map_in(u32 *dir, u32 virt, u32 phys, u32 flags) {
    u32 *t = table_for_dir(dir, virt, true, flags);
    if (!t) return false;
    t[(virt >> 12) & 0x3FF] = (phys & ~0xFFFu) | (flags & 0xFFF) | PTE_PRESENT;
    if (dir == current_directory)
        __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
    return true;
}

bool map_page(u32 virt, u32 phys, u32 flags) {
    return map_in(current_directory, virt, phys, flags);
}

bool map_page_in(u32 dir_phys, u32 virt, u32 phys, u32 flags) {
    if (!dir_phys) return false;
    return map_in((u32 *)dir_phys, virt, phys, flags);
}

void unmap_page(u32 virt) {
    u32 *t = table_for_dir(current_directory, virt, false, 0);
    if (!t) return;
    t[(virt >> 12) & 0x3FF] = 0;
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

u32 paging_new_directory(void) {
    u32 pd = pmm_alloc_frame();
    if (!pd) return 0;
    u32 *dir = (u32 *)pd;
    memset(dir, 0, PAGE_SIZE);

    /* Share the kernel's tables rather than copying them. Every address space
       sees the same kernel, which is what lets an interrupt taken in user code
       land on a kernel stack that is actually mapped. */
    u32 kernel_pdes = (KERNEL_SPACE_MB * 1024u * 1024u) / (PAGE_SIZE * 1024u);
    for (u32 i = 0; i < kernel_pdes; i++) dir[i] = kernel_directory[i];

    /* The framebuffer lives high; carry those entries across too. */
    for (u32 i = kernel_pdes; i < 1024; i++)
        if (kernel_directory[i] & PTE_PRESENT) dir[i] = kernel_directory[i];

    return pd;
}

void paging_free_directory(u32 dir_phys) {
    if (!dir_phys || (u32 *)dir_phys == kernel_directory) return;
    u32 *dir = (u32 *)dir_phys;

    /* Only release tables this space owns. Anything the kernel also has is
       shared and must survive. */
    for (u32 i = 0; i < 1024; i++) {
        if (!(dir[i] & PTE_PRESENT)) continue;
        if (dir[i] == kernel_directory[i]) continue;
        u32 table = dir[i] & ~0xFFFu;
        u32 *t = (u32 *)table;
        for (u32 j = 0; j < 1024; j++)
            if ((t[j] & PTE_PRESENT) && (t[j] & PTE_USER))
                pmm_free_frame(t[j] & ~0xFFFu);
        pmm_free_frame(table);
    }
    pmm_free_frame(dir_phys);
}

void paging_switch(u32 dir_phys) {
    if (!dir_phys) return;
    current_directory = (u32 *)dir_phys;
    __asm__ volatile ("mov %0, %%cr3" :: "r"(dir_phys) : "memory");
}

u32 paging_current_directory(void) { return (u32)current_directory; }
u32 paging_kernel_directory(void)  { return (u32)kernel_directory; }

u32 virt_to_phys_in(u32 dir_phys, u32 virt) {
    if (!dir_phys) return 0;
    u32 *t = table_for_dir((u32 *)dir_phys, virt, false, 0);
    if (!t) return 0;
    u32 e = t[(virt >> 12) & 0x3FF];
    if (!(e & PTE_PRESENT)) return 0;
    return (e & ~0xFFFu) | (virt & 0xFFF);
}

u32 virt_to_phys(u32 virt) {
    u32 *t = table_for_dir(current_directory, virt, false, 0);
    if (!t) return 0;
    u32 e = t[(virt >> 12) & 0x3FF];
    if (!(e & PTE_PRESENT)) return 0;
    return (e & ~0xFFFu) | (virt & 0xFFF);
}

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
    kernel_directory = (u32 *)pd;
    current_directory = kernel_directory;
    memset(kernel_directory, 0, PAGE_SIZE);

    /* Identity map the low IDENTITY_MB so kernel code, the frame bitmap and
       the heap all keep the addresses they already have. */
    for (u32 a = 0; a < KERNEL_SPACE_MB * 1024u * 1024u; a += PAGE_SIZE) {
        if (!map_page(a, a, PTE_PRESENT | PTE_RW))
            panic("paging: identity map failed at %p", (void *)a);
    }

    register_interrupt_handler(14, page_fault);

    __asm__ volatile ("mov %0, %%cr3" :: "r"((u32)kernel_directory));
    u32 cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;                    /* CR0.PG */
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0));
}
