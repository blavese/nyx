.section .text
.global gdt_flush
.type gdt_flush, @function
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)
    mov $0x10, %ax          /* kernel data selector */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    ljmp $0x08, $.flush     /* far jump reloads cs */
.flush:
    ret

.global tss_flush
.type tss_flush, @function
tss_flush:
    mov $0x2B, %ax          /* TSS selector (index 5, RPL 3) */
    ltr %ax
    ret
