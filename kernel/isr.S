.section .text
.extern isr_dispatch

/* Common tail: build a registers_t on the stack, call into C, unwind. */
isr_common:
    pusha
    mov %ds, %ax
    push %eax
    mov $0x10, %ax          /* kernel data segment for the handler */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    cld
    push %esp               /* registers_t * */
    call isr_dispatch
    /* isr_dispatch returns the frame to resume on. Normally that is the one
       we passed in; after a task switch it belongs to a different task. */
    mov %eax, %esp
    pop %eax                /* restore caller's data segment */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    popa
    add $8, %esp            /* drop int_no and err_code */
    iret

.global isr0
isr0:
    cli
    push $0
    push $0
    jmp isr_common

.global isr1
isr1:
    cli
    push $0
    push $1
    jmp isr_common

.global isr2
isr2:
    cli
    push $0
    push $2
    jmp isr_common

.global isr3
isr3:
    cli
    push $0
    push $3
    jmp isr_common

.global isr4
isr4:
    cli
    push $0
    push $4
    jmp isr_common

.global isr5
isr5:
    cli
    push $0
    push $5
    jmp isr_common

.global isr6
isr6:
    cli
    push $0
    push $6
    jmp isr_common

.global isr7
isr7:
    cli
    push $0
    push $7
    jmp isr_common

.global isr8
isr8:
    cli
    push $8
    jmp isr_common

.global isr9
isr9:
    cli
    push $0
    push $9
    jmp isr_common

.global isr10
isr10:
    cli
    push $10
    jmp isr_common

.global isr11
isr11:
    cli
    push $11
    jmp isr_common

.global isr12
isr12:
    cli
    push $12
    jmp isr_common

.global isr13
isr13:
    cli
    push $13
    jmp isr_common

.global isr14
isr14:
    cli
    push $14
    jmp isr_common

.global isr15
isr15:
    cli
    push $0
    push $15
    jmp isr_common

.global isr16
isr16:
    cli
    push $0
    push $16
    jmp isr_common

.global isr17
isr17:
    cli
    push $17
    jmp isr_common

.global isr18
isr18:
    cli
    push $0
    push $18
    jmp isr_common

.global isr19
isr19:
    cli
    push $0
    push $19
    jmp isr_common

.global isr20
isr20:
    cli
    push $0
    push $20
    jmp isr_common

.global isr21
isr21:
    cli
    push $21
    jmp isr_common

.global isr22
isr22:
    cli
    push $0
    push $22
    jmp isr_common

.global isr23
isr23:
    cli
    push $0
    push $23
    jmp isr_common

.global isr24
isr24:
    cli
    push $0
    push $24
    jmp isr_common

.global isr25
isr25:
    cli
    push $0
    push $25
    jmp isr_common

.global isr26
isr26:
    cli
    push $0
    push $26
    jmp isr_common

.global isr27
isr27:
    cli
    push $0
    push $27
    jmp isr_common

.global isr28
isr28:
    cli
    push $0
    push $28
    jmp isr_common

.global isr29
isr29:
    cli
    push $29
    jmp isr_common

.global isr30
isr30:
    cli
    push $30
    jmp isr_common

.global isr31
isr31:
    cli
    push $0
    push $31
    jmp isr_common

.global isr32
isr32:
    cli
    push $0
    push $32
    jmp isr_common

.global isr33
isr33:
    cli
    push $0
    push $33
    jmp isr_common

.global isr34
isr34:
    cli
    push $0
    push $34
    jmp isr_common

.global isr35
isr35:
    cli
    push $0
    push $35
    jmp isr_common

.global isr36
isr36:
    cli
    push $0
    push $36
    jmp isr_common

.global isr37
isr37:
    cli
    push $0
    push $37
    jmp isr_common

.global isr38
isr38:
    cli
    push $0
    push $38
    jmp isr_common

.global isr39
isr39:
    cli
    push $0
    push $39
    jmp isr_common

.global isr40
isr40:
    cli
    push $0
    push $40
    jmp isr_common

.global isr41
isr41:
    cli
    push $0
    push $41
    jmp isr_common

.global isr42
isr42:
    cli
    push $0
    push $42
    jmp isr_common

.global isr43
isr43:
    cli
    push $0
    push $43
    jmp isr_common

.global isr44
isr44:
    cli
    push $0
    push $44
    jmp isr_common

.global isr45
isr45:
    cli
    push $0
    push $45
    jmp isr_common

.global isr46
isr46:
    cli
    push $0
    push $46
    jmp isr_common

.global isr47
isr47:
    cli
    push $0
    push $47
    jmp isr_common

.global isr128
isr128:
    cli
    push $0
    push $128
    jmp isr_common

.section .rodata
.global isr_stub_table
isr_stub_table:
    .long isr0
    .long isr1
    .long isr2
    .long isr3
    .long isr4
    .long isr5
    .long isr6
    .long isr7
    .long isr8
    .long isr9
    .long isr10
    .long isr11
    .long isr12
    .long isr13
    .long isr14
    .long isr15
    .long isr16
    .long isr17
    .long isr18
    .long isr19
    .long isr20
    .long isr21
    .long isr22
    .long isr23
    .long isr24
    .long isr25
    .long isr26
    .long isr27
    .long isr28
    .long isr29
    .long isr30
    .long isr31
    .long isr32
    .long isr33
    .long isr34
    .long isr35
    .long isr36
    .long isr37
    .long isr38
    .long isr39
    .long isr40
    .long isr41
    .long isr42
    .long isr43
    .long isr44
    .long isr45
    .long isr46
    .long isr47
    .long isr128
