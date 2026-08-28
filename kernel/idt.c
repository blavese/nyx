#include "idt.h"
#include "printf.h"
#include "string.h"
#include "io.h"
#include "pic.h"

u32 scheduler_switch(u32 esp);

struct idt_entry { u16 base_low; u16 sel; u8 zero; u8 flags; u16 base_high; } __attribute__((packed));
struct idt_ptr   { u16 limit; u32 base; } __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtp;
static isr_handler_t    handlers[256];

extern void idt_flush(u32);
extern void *isr_stub_table[];

static void set_gate(u8 n, u32 base, u16 sel, u8 flags) {
    idt[n].base_low  = (u16)(base & 0xFFFF);
    idt[n].base_high = (u16)((base >> 16) & 0xFFFF);
    idt[n].sel = sel;
    idt[n].zero = 0;
    idt[n].flags = flags;
}

void register_interrupt_handler(u8 n, isr_handler_t h) { handlers[n] = h; }

void idt_init(void) {
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (u32)&idt;
    memset(&idt, 0, sizeof(idt));
    memset(&handlers, 0, sizeof(handlers));

    for (int i = 0; i < 48; i++)
        set_gate((u8)i, (u32)isr_stub_table[i], 0x08, 0x8E);

    /* 0x80 is the syscall gate: DPL 3 so ring 3 may invoke it. */
    set_gate(0x80, (u32)isr_stub_table[48], 0x08, 0xEE);

    idt_flush((u32)&idtp);
}

static const char *EXC[] = {
    "divide by zero", "debug", "non-maskable interrupt", "breakpoint",
    "overflow", "bound range exceeded", "invalid opcode", "device not available",
    "double fault", "coprocessor segment overrun", "invalid TSS", "segment not present",
    "stack-segment fault", "general protection fault", "page fault", "reserved",
    "x87 floating point", "alignment check", "machine check", "SIMD floating point",
    "virtualization", "control protection", "reserved", "reserved",
    "reserved", "reserved", "reserved", "reserved",
    "hypervisor injection", "VMM communication", "security", "reserved"
};

/* Called from isr_common in isr.S */
u32 isr_dispatch(registers_t *r) {
    if (handlers[r->int_no]) handlers[r->int_no](r);
    else if (r->int_no < 32) {
        panic("unhandled exception %d (%s)\n  eip=%p err=%x cs=%x eflags=%x",
              r->int_no, EXC[r->int_no], (void *)r->eip, r->err_code, r->cs, r->eflags);
    }

    /* A hardware interrupt has to be acknowledged, or the PIC will never
       deliver another one at the same or lower priority. */
    if (r->int_no >= 32 && r->int_no < 48) pic_eoi((u8)(r->int_no - 32));

    /* The scheduler may hand back a different task's frame. */
    u32 resume = (u32)r;
    if (r->int_no == 32) resume = scheduler_switch(resume);
    return resume;
}
