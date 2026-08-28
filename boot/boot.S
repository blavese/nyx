/* Multiboot 1 header. GRUB and QEMU's -kernel both understand this. */
.set ALIGN,    1<<0              /* page-align loaded modules */
.set MEMINFO,  1<<1              /* ask the loader for a memory map */
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 16
stack_bottom:
.skip 65536                      /* 64 KiB kernel stack */
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    /* The bootloader leaves the multiboot info pointer in ebx and the
       magic in eax. Set up our own stack before touching anything else. */
    mov $stack_top, %esp
    mov $0, %ebp

    push %ebx                    /* multiboot info struct */
    push %eax                    /* multiboot magic */
    call kmain

    /* kmain should never return. If it does, park the CPU. */
    cli
.Lhang:
    hlt
    jmp .Lhang
.size _start, . - _start
