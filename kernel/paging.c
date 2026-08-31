/* Four-level paging.
 *
 * A 64-bit virtual address is split into four nine-bit indices and an offset,
 * so translating one walks four tables instead of two. Every level has the
 * same shape: 512 entries, each holding a physical address and a handful of
 * flags, and every level except the last points at the next one.
 *
 *   47..39  which entry of the top table      (PML4)
 *   38..30  which entry of the one below      (page directory pointer table)
 *   29..21  which entry of the one below that (page directory)
 *   20..12  which entry of the last           (page table)
 *   11..0   the offset within the page
 *
 * Tables live in identity mapped memory, so a physical address is also a
 * usable pointer. That is what keeps this from needing a recursive mapping
 * or a temporary window to edit an address space that is not loaded.
 *
 * The kernel's own mappings are shared into every address space rather than
 * copied, which is what lets an interrupt taken in ring 3 land on a kernel
 * stack that is actually mapped. Sharing happens at the top level: the first
 * entries of the PML4 are the same physical tables in every space.
 */
#include "paging.h"
#include "pmm.h"
#include "printf.h"
#include "string.h"
#include "idt.h"
#include "io.h"

#define ENTRIES 512

static u64 *kernel_pml4;
static u64 *current_pml4;

static inline u64 index_of(u64 virt, int level) {
    /* level 4 is the PML4, 1 is the page table */
    return (virt >> (12 + 9 * (level - 1))) & 0x1FF;
}

/* Walks down one level, optionally creating the table below. */
static u64 *step(u64 *table, u64 virt, int level, bool create, u64 flags) {
    u64 i = index_of(virt, level);
    if (!(table[i] & PTE_PRESENT)) {
        if (!create) return 0;
        u64 frame = pmm_alloc_frame();
        if (!frame) return 0;
        memset((void *)frame, 0, PAGE_SIZE);
        table[i] = frame | PTE_PRESENT | PTE_RW;
    }
    /* A table shared with the kernel may have been created without the user
       bit; widen it if a user mapping now needs to go through it. The pages
       inside stay as they were, which is what keeps kernel memory out of
       reach even when a user page shares its table. */
    if (flags & PTE_USER) table[i] |= PTE_USER;

    /* A large page has no table below it. Nothing here creates one, but the
       trampoline in boot.S does, and walking into one would read 2 MiB of
       memory as if it were entries. */
    if (table[i] & PTE_HUGE) return 0;

    return (u64 *)(table[i] & PTE_ADDR_MASK);
}

/* The page table containing `virt`, or null. */
static u64 *table_for(u64 *pml4, u64 virt, bool create, u64 flags) {
    u64 *pdpt = step(pml4, virt, 4, create, flags);
    if (!pdpt) return 0;
    u64 *pd = step(pdpt, virt, 3, create, flags);
    if (!pd) return 0;
    return step(pd, virt, 2, create, flags);
}

static bool map_in(u64 *pml4, u64 virt, u64 phys, u64 flags) {
    u64 *pt = table_for(pml4, virt, true, flags);
    if (!pt) return false;
    pt[index_of(virt, 1)] = (phys & PTE_ADDR_MASK) | (flags & 0xFFF) | PTE_PRESENT;
    if (pml4 == current_pml4)
        __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
    return true;
}

bool map_page(u64 virt, u64 phys, u64 flags) {
    return map_in(current_pml4, virt, phys, flags);
}

bool map_page_in(u64 pml4_phys, u64 virt, u64 phys, u64 flags) {
    if (!pml4_phys) return false;
    return map_in((u64 *)pml4_phys, virt, phys, flags);
}

/* Removes a mapping from a named address space.
 *
 * Taking the directory rather than assuming the live one is what lets the
 * window manager replace a surface belonging to a program that is not the
 * one currently running. The flush is only meaningful when that space is
 * the live one, which is why it is conditional: invalidating an address in
 * a space that is not loaded does nothing, and doing it anyway on a space
 * that is loaded is required. */
void unmap_page_in(u64 pml4_phys, u64 virt) {
    if (!pml4_phys) return;
    u64 *pt = table_for((u64 *)pml4_phys, virt, false, 0);
    if (!pt) return;
    pt[index_of(virt, 1)] = 0;
    if (pml4_phys == (u64)current_pml4)
        __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}

void unmap_page(u64 virt) { unmap_page_in((u64)current_pml4, virt); }

/* How many top-level entries the kernel's own mappings occupy. Each covers
   512 GiB, so in practice this is one, and sharing it shares everything the
   kernel has mapped. */
#define KERNEL_PML4_ENTRIES 1

u64 paging_new_directory(void) {
    u64 frame = pmm_alloc_frame();
    if (!frame) return 0;
    u64 *pml4 = (u64 *)frame;
    memset(pml4, 0, PAGE_SIZE);

    for (u64 i = 0; i < ENTRIES; i++)
        if (kernel_pml4[i] & PTE_PRESENT) pml4[i] = kernel_pml4[i];

    return frame;
}

/* Releases what an address space owns without touching what it shares.
 *
 * Everything reachable from a top-level entry the kernel also has is shared
 * and must survive, so the walk below only descends into entries this space
 * added, and only frees a page that was mapped for user code. */
static void free_table(u64 *table, int level) {
    for (u64 i = 0; i < ENTRIES; i++) {
        u64 e = table[i];
        if (!(e & PTE_PRESENT)) continue;
        if (e & PTE_HUGE) continue;
        u64 below = e & PTE_ADDR_MASK;
        if (level > 1) {
            free_table((u64 *)below, level - 1);
            pmm_free_frame(below);
        } else if (e & PTE_USER) {
            pmm_free_frame(below);
        }
    }
}

void paging_free_directory(u64 pml4_phys) {
    if (!pml4_phys || (u64 *)pml4_phys == kernel_pml4) return;
    u64 *pml4 = (u64 *)pml4_phys;

    for (u64 i = 0; i < ENTRIES; i++) {
        if (!(pml4[i] & PTE_PRESENT)) continue;
        /* Compare the table address alone: this space may have widened the
           user bit on an entry whose table belongs to the kernel. */
        if ((kernel_pml4[i] & PTE_PRESENT) &&
            (pml4[i] & PTE_ADDR_MASK) == (kernel_pml4[i] & PTE_ADDR_MASK)) continue;

        u64 below = pml4[i] & PTE_ADDR_MASK;
        free_table((u64 *)below, 3);
        pmm_free_frame(below);
    }
    pmm_free_frame(pml4_phys);
}

void paging_switch(u64 pml4_phys) {
    if (!pml4_phys) return;
    current_pml4 = (u64 *)pml4_phys;
    __asm__ volatile ("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

u64 paging_current_directory(void) { return (u64)current_pml4; }
u64 paging_kernel_directory(void)  { return (u64)kernel_pml4; }

u64 virt_to_phys_in(u64 pml4_phys, u64 virt) {
    if (!pml4_phys) return 0;
    u64 *pt = table_for((u64 *)pml4_phys, virt, false, 0);
    if (!pt) return 0;
    u64 e = pt[index_of(virt, 1)];
    if (!(e & PTE_PRESENT)) return 0;
    return (e & PTE_ADDR_MASK) | (virt & 0xFFF);
}

u64 virt_to_phys(u64 virt) {
    return virt_to_phys_in((u64)current_pml4, virt);
}

/* Checks every level. A table shared with the kernel can carry the user bit
   on the entry above it while the individual kernel pages inside it do not,
   so no single level answers the question. */
bool virt_is_user_in(u64 pml4_phys, u64 virt) {
    if (!pml4_phys) return false;
    u64 *table = (u64 *)pml4_phys;

    for (int level = 4; level >= 2; level--) {
        u64 e = table[index_of(virt, level)];
        if (!(e & PTE_PRESENT) || !(e & PTE_USER)) return false;
        if (e & PTE_HUGE) return true;
        table = (u64 *)(e & PTE_ADDR_MASK);
    }
    u64 pte = table[index_of(virt, 1)];
    return (pte & PTE_PRESENT) && (pte & PTE_USER);
}

static void page_fault(registers_t *r) {
    u64 cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    panic("page fault at %p  rip=%p  [%s %s %s]",
          (void *)cr2, (void *)r->rip,
          (r->err_code & 1) ? "protection" : "not-present",
          (r->err_code & 2) ? "write" : "read",
          (r->err_code & 4) ? "user" : "kernel");
}

void paging_init(void) {
    u64 frame = pmm_alloc_frame();
    if (!frame) panic("paging: no frame for the top level table");
    kernel_pml4 = (u64 *)frame;
    current_pml4 = kernel_pml4;
    memset(kernel_pml4, 0, PAGE_SIZE);

    /* Identity map the low region so kernel code, the frame bitmap and the
       heap all keep the addresses they already have. The trampoline that got
       us here mapped the first 4 GiB with large pages; this replaces that
       with 4 KiB pages, which is what the rest of the kernel expects to be
       able to change one page at a time. */
    for (u64 a = 0; a < KERNEL_SPACE_MB * 1024ull * 1024ull; a += PAGE_SIZE) {
        if (!map_page(a, a, PTE_PRESENT | PTE_RW))
            panic("paging: identity map failed at %p", (void *)a);
    }

    register_interrupt_handler(14, page_fault);

    __asm__ volatile ("mov %0, %%cr3" :: "r"((u64)kernel_pml4) : "memory");
}

/* Maps a physical range that sits above the identity mapped region, which is
   what every memory-mapped device needs. Returns a pointer to it.
 *
 * The address is mapped where it already is, so the pointer and the physical
 * address are the same number. That only works while nothing else wants that
 * part of the address space, which on a machine with 64 bits of it is a safe
 * assumption for a long time yet. */
void *paging_map_device(u64 phys, u64 bytes) {
    u64 first = phys & ~0xFFFull;
    u64 last = (phys + bytes + PAGE_SIZE - 1) & ~0xFFFull;
    for (u64 a = first; a < last; a += PAGE_SIZE) {
        if (virt_to_phys(a) == a) continue;          /* already there */
        if (!map_page(a, a, PTE_PRESENT | PTE_RW | PTE_NOCACHE)) return 0;
    }
    return (void *)phys;
}
