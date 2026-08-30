.code64
.section .text
.global idt_flush
.type idt_flush, @function
idt_flush:
    lidt (%rdi)
    ret
