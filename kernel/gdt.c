/* Flat segmentation: every segment spans the whole 4 GiB address space, so
   paging does the real memory protection work. The TSS exists so that an
   interrupt taken in ring 3 knows which kernel stack to switch to. */
#include "gdt.h"
#include "string.h"

struct gdt_entry {
    u16 limit_low, base_low;
    u8  base_mid, access, granularity, base_high;
} __attribute__((packed));

struct gdt_ptr { u16 limit; u32 base; } __attribute__((packed));

struct tss_entry {
    u32 prev, esp0; u32 ss0, esp1, ss1, esp2, ss2;
    u32 cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap_base;
} __attribute__((packed));

#define GDT_ENTRIES 6
static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gdtp;
static struct tss_entry tss;

extern void gdt_flush(u32 gdtp_addr);
extern void tss_flush(void);

static void set_gate(int i, u32 base, u32 limit, u8 access, u8 gran) {
    gdt[i].base_low    = (u16)(base & 0xFFFF);
    gdt[i].base_mid    = (u8)((base >> 16) & 0xFF);
    gdt[i].base_high   = (u8)((base >> 24) & 0xFF);
    gdt[i].limit_low   = (u16)(limit & 0xFFFF);
    gdt[i].granularity = (u8)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].access      = access;
}

void tss_set_stack(u32 esp0) { tss.esp0 = esp0; }

void gdt_init(void) {
    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base  = (u32)&gdt;

    set_gate(0, 0, 0, 0, 0);                    /* required null descriptor */
    set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF);        /* ring 0 code */
    set_gate(2, 0, 0xFFFFF, 0x92, 0xCF);        /* ring 0 data */
    set_gate(3, 0, 0xFFFFF, 0xFA, 0xCF);        /* ring 3 code */
    set_gate(4, 0, 0xFFFFF, 0xF2, 0xCF);        /* ring 3 data */

    memset(&tss, 0, sizeof(tss));
    tss.ss0  = 0x10;                            /* kernel data segment */
    tss.esp0 = 0;
    tss.iomap_base = sizeof(tss);
    u32 base = (u32)&tss;
    set_gate(5, base, sizeof(tss) - 1, 0x89, 0x00);

    gdt_flush((u32)&gdtp);
    tss_flush();
}
