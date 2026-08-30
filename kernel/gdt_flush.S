/* Loading the descriptor table, in long mode.
 *
 * The far jump that reloads cs cannot be written as one instruction here:
 * long mode has no far jump to an immediate. The way round it is to push a
 * selector and a return address and let a far return do the work. */
.code64
.section .text

.global gdt_flush
.type gdt_flush, @function
gdt_flush:
    lgdt (%rdi)

    mov $0x10, %ax              /* kernel data */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    /* Far return to the next instruction, through the kernel code selector,
       which is what actually reloads cs. */
    lea .reloaded(%rip), %rax
    push $0x08
    push %rax
    lretq
.reloaded:
    ret

.global tss_flush
.type tss_flush, @function
tss_flush:
    mov %di, %ax
    ltr %ax
    ret
