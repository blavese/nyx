/* Where a second processor starts.
 *
 * An application processor comes out of reset in 16-bit real mode at a
 * physical address the boot processor chose, which must be page aligned and
 * below a megabyte. It has no stack, no descriptor tables and no paging: it
 * is a fresh machine that happens to share memory with a running one.
 *
 * This is what it lands on. It is a flat binary rather than part of the
 * kernel because it has to be copied to a fixed low address and every
 * address inside it has to be absolute, which is exactly the situation the
 * bootloader is in, so it is built the same way.
 *
 * The boot processor fills in the parameter block below before sending the
 * startup signal: which page directory to use, which stack this processor
 * gets, and where in the kernel to go. Startup is serialised, so one stack
 * slot is enough.
 */
.code16
.section .text
.globl _start

.set BASE, 0x8000

_start:
    cli
    cld
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss

    lgdt    BASE + (gdt_desc - _start)

    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmpl   $0x08, $(BASE + (protected - _start))

.code32
protected:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss

    /* Share the boot processor's address space. Everything this CPU is about
       to touch is already mapped there, including the kernel it is going to
       jump into. */
    movl    BASE + (param_cr3 - _start), %eax
    movl    %eax, %cr3
    movl    %cr0, %eax
    orl     $0x80000000, %eax
    movl    %eax, %cr0

    /* Its own stack, handed over by whoever started it. */
    movl    BASE + (param_stack - _start), %esp
    xorl    %ebp, %ebp

    /* Say which processor this is, then leave the trampoline for good. */
    movl    BASE + (param_arg - _start), %eax
    pushl   %eax
    movl    BASE + (param_entry - _start), %eax
    call    *%eax

    /* The entry point is not expected to return. */
1:  hlt
    jmp     1b

.align 8
gdt:
    .quad   0
    /* code32: base 0, limit 4 GiB */
    .word   0xFFFF, 0x0000
    .byte   0x00, 0x9A, 0xCF, 0x00
    /* data32: base 0, limit 4 GiB */
    .word   0xFFFF, 0x0000
    .byte   0x00, 0x92, 0xCF, 0x00
gdt_end:

gdt_desc:
    .word   gdt_end - gdt - 1
    .long   BASE + (gdt - _start)

/* Filled in by smp.c before each processor is started. The signature lets it
   find them without knowing how the code above is laid out. */
.align 4
    .ascii  "NYXSMP01"
param_cr3:      .long 0
param_stack:    .long 0
param_entry:    .long 0
param_arg:      .long 0
