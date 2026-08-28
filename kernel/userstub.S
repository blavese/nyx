/* A minimal ring 3 program, assembled into the kernel image and copied into
   a user page at run time. It uses no absolute addresses, so it runs
   correctly wherever it is placed. */
.section .rodata
.global user_stub_start
.global user_stub_end

user_stub_start:
    movl $1, %eax           /* SYS_PUTC */
    movl $'r', %ebx
    int  $0x80
    movl $1, %eax
    movl $'i', %ebx
    int  $0x80
    movl $1, %eax
    movl $'n', %ebx
    int  $0x80
    movl $1, %eax
    movl $'g', %ebx
    int  $0x80
    movl $1, %eax
    movl $'3', %ebx
    int  $0x80
    movl $1, %eax
    movl $'\n', %ebx
    int  $0x80

    movl $0, %eax           /* SYS_EXIT */
    xorl %ebx, %ebx
    int  $0x80
1:  jmp 1b                  /* exit should not return */
user_stub_end:
