/* A bootloader, for booting nyx from a CD or a USB stick.
 *
 * Until now nyx has always been handed to QEMU with -kernel, which means
 * QEMU did the bootloader's job: it parsed the ELF, put the segments where
 * they belong, built a multiboot information structure and jumped to the
 * entry point. On a real machine nothing does that. This does.
 *
 * The BIOS loads this at 0x7C00 in 16-bit real mode with the boot drive in
 * DL, and from there it has to:
 *
 *   1. turn on the A20 gate, or every address above 1 MiB wraps
 *   2. ask the firmware for the memory map, because the kernel needs to be
 *      told what RAM exists and there is no other way to find out
 *   3. read the kernel off the disc, which can only be done in real mode,
 *      into a buffer below 1 MiB, which is all real mode can address
 *   4. copy each buffer up to 1 MiB, which real mode cannot do at all, so
 *      it is done in unreal mode: enter protected mode, load a segment with
 *      a 4 GiB limit, leave protected mode again, and keep the limit
 *   5. build the multiboot structure the kernel expects
 *   6. switch to protected mode for real and jump
 *
 * The four values the image builder patches sit right after the signature
 * below, so tools/mkiso.py can find them without parsing anything.
 */
.code16
.section .text
.globl _start

_start:
    jmp     real_start
    .byte   0x90

    /* Patched by tools/mkiso.py. */
    .ascii  "NYXBOOT1"
patch_lba:      .long 0          /* first 2048 byte sector of the payload */
patch_bytes:    .long 0          /* how many bytes of payload there are */
patch_entry:    .long 0          /* where to jump once it is in memory */
patch_load:     .long 0x100000   /* where the payload belongs */

/* Where things go while we work. All of it is scratch below 0x7C00, which
   the BIOS has already finished with. */
.set MBI_ADDR,     0x0500
.set MMAP_ADDR,    0x0600
.set CMDLINE_ADDR, 0x0E00
.set BUFFER_SEG,   0x1000        /* 0x10000, the disk read buffer */
.set CHUNK_SECTORS, 16           /* 16 x 2048 = 32 KiB per read */

real_start:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7C00, %sp
    movb    %dl, boot_drive
    sti

    movw    $msg_boot, %si
    call    print

    call    enable_a20
    call    probe_geometry
    call    read_memory_map
    call    enter_unreal
    call    load_payload
    call    build_mbi

    movw    $msg_go, %si
    call    print

    cli
    lgdt    gdt_desc
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmpl   $0x08, $protected

/* --- real mode helpers -------------------------------------------------- */

/* Prints the null terminated string at DS:SI through the BIOS. This is the
   only output there is on a machine that has not started nyx yet, so every
   failure below ends in one of these. */
print:
    pushw   %ax
    pushw   %bx
    movb    $0x0E, %ah
    movw    $0x0007, %bx
1:  lodsb
    testb   %al, %al
    jz      2f
    int     $0x10
    jmp     1b
2:  popw    %bx
    popw    %ax
    ret

die:
    call    print
    movw    $msg_halt, %si
    call    print
1:  hlt
    jmp     1b

/* The A20 gate starts closed on a PC for compatibility with software from
   1982. Try the fast path through the system control port first, then the
   keyboard controller, which is what older machines need. */
enable_a20:
    inb     $0x92, %al
    testb   $0x02, %al
    jnz     2f
    orb     $0x02, %al
    andb    $0xFE, %al           /* bit 0 would reset the machine */
    outb    %al, $0x92
2:
    /* Confirm it took: 0x0000:0x0500 and 0xFFFF:0x0510 are the same byte
       only while A20 is off. */
    pushw   %ds
    pushw   %es
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    $0xFFFF, %ax
    movw    %ax, %es
    movw    $0x0500, %si
    movw    $0x0510, %di
    movb    (%si), %al
    pushw   %ax
    movb    %es:(%di), %al
    pushw   %ax
    movb    $0x00, (%si)
    movb    $0xFF, %es:(%di)
    movb    (%si), %al
    cmpb    $0xFF, %al           /* still equal means the address wrapped */
    popw    %bx
    movb    %bl, %es:(%di)
    popw    %bx
    movb    %bl, (%si)
    popw    %es
    popw    %ds
    jne     a20_ok

    /* Fall back to the keyboard controller. */
    call    kbc_wait_in
    movb    $0xAD, %al
    outb    %al, $0x64
    call    kbc_wait_in
    movb    $0xD0, %al
    outb    %al, $0x64
    call    kbc_wait_out
    inb     $0x60, %al
    pushw   %ax
    call    kbc_wait_in
    movb    $0xD1, %al
    outb    %al, $0x64
    call    kbc_wait_in
    popw    %ax
    orb     $0x02, %al
    outb    %al, $0x60
    call    kbc_wait_in
    movb    $0xAE, %al
    outb    %al, $0x64
    call    kbc_wait_in
a20_ok:
    ret

kbc_wait_in:
    inb     $0x64, %al
    testb   $0x02, %al
    jnz     kbc_wait_in
    ret

kbc_wait_out:
    inb     $0x64, %al
    testb   $0x01, %al
    jz      kbc_wait_out
    ret

/* A CD reports 2048 byte sectors and a USB stick reports 512, and INT 13h
   counts in whatever the device uses. The image builder works in 2048 byte
   units because that is what an ISO is made of, so everything here is scaled
   by whatever the firmware says once, at the start. */
probe_geometry:
    movw    $2048, sector_size
    movw    $1, lba_scale

    movw    $params, %si
    movw    $0x1E, (%si)         /* buffer size the call expects */
    movb    boot_drive, %dl
    movb    $0x48, %ah
    int     $0x13
    jc      1f

    movw    24(%si), %ax         /* bytes per sector */
    testw   %ax, %ax
    jz      1f
    cmpw    $2048, %ax
    ja      1f                   /* something unexpected: keep the default */
    movw    %ax, sector_size

    /* lba_scale converts an ISO sector number into a device one. */
    movw    $2048, %ax
    xorw    %dx, %dx
    divw    sector_size
    movw    %ax, lba_scale

1:  /* Sectors per 32 KiB chunk, which is what each read asks for. */
    movw    $32768, %ax
    xorw    %dx, %dx
    divw    sector_size
    movw    %ax, chunk_sectors
    ret

/* Asks the firmware what memory exists and writes it out in the format the
   multiboot specification uses: a 4 byte size in front of each 20 byte
   entry. Without this the kernel has no idea how much RAM it has. */
read_memory_map:
    movw    $MMAP_ADDR, %di
    xorl    %ebx, %ebx
    xorl    %ebp, %ebp           /* running total of bytes written */

1:  movl    $20, %ecx
    movl    $0x0000E820, %eax
    movl    $0x534D4150, %edx    /* 'SMAP' */
    pushw   %di
    addw    $4, %di              /* leave room for the size field */
    int     $0x15
    popw    %di
    jc      2f
    cmpl    $0x534D4150, %eax
    jne     2f

    movl    $20, (%di)           /* the size the kernel steps over */
    addw    $24, %di
    addl    $24, %ebp

    testl   %ebx, %ebx
    jz      3f
    cmpw    $0x0E00 - 24, %di    /* out of room; stop rather than overrun */
    jae     3f
    jmp     1b

2:  testl   %ebp, %ebp
    jnz     3f
    movw    $msg_nomem, %si
    jmp     die

3:  movl    %ebp, mmap_bytes
    ret

/* Enters protected mode just long enough to give FS a 4 GiB limit, then
   leaves again. The limit stays in the hidden part of the segment register,
   so real mode code can reach the whole address space through FS. This is
   the only way to write above 1 MiB without giving up the BIOS. */
enter_unreal:
    cli
    pushw   %ds
    lgdt    gdt_desc
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    jmp     1f
1:  movw    $0x10, %bx
    movw    %bx, %fs
    movl    %cr0, %eax
    andl    $0xFFFFFFFE, %eax
    movl    %eax, %cr0
    jmp     2f
2:  popw    %ds
    sti
    ret

/* Reads the payload off the boot device 32 KiB at a time and copies each
   chunk to its real home above 1 MiB. */
load_payload:
    movl    patch_lba, %eax
    movl    %eax, cur_lba
    movl    patch_load, %eax
    movl    %eax, cur_dest
    movl    patch_bytes, %eax
    movl    %eax, remaining

1:  cmpl    $0, remaining
    jbe     9f

    /* One extended read into the buffer below 1 MiB, in whatever sector
       size this device happens to use. The last chunk asks for only as many
       sectors as are left: reading past the end of the image is what a
       drive refuses, and it looks exactly like a broken disc. */
    movw    chunk_sectors, %ax
    movl    remaining, %ecx
    cmpl    $CHUNK_SECTORS * 2048, %ecx
    jae     2f
    movw    %cx, %ax
    addw    sector_size, %ax
    decw    %ax
    xorw    %dx, %dx
    divw    sector_size
2:  movw    $dap, %si
    movw    %ax, dap_count
    movl    cur_lba, %eax
    movzwl  lba_scale, %ecx
    mull    %ecx
    movl    %eax, dap_lba
    movb    boot_drive, %dl
    movb    $0x42, %ah
    int     $0x13
    jc      read_failed

    /* Copy it up. ECX counts dwords; a chunk is 32 KiB so it fits in CX. */
    movl    $BUFFER_SEG * 16, %esi
    movl    cur_dest, %edi
    movl    remaining, %ecx
    cmpl    $CHUNK_SECTORS * 2048, %ecx
    jbe     2f
    movl    $CHUNK_SECTORS * 2048, %ecx
2:  addl    $3, %ecx
    shrl    $2, %ecx

3:  movl    %fs:(%esi), %eax
    movl    %eax, %fs:(%edi)
    addl    $4, %esi
    addl    $4, %edi
    decl    %ecx
    jnz     3b

    /* On to the next chunk. cur_lba stays in ISO sectors. */
    addl    $16, cur_lba
    movl    $CHUNK_SECTORS * 2048, %eax
    addl    %eax, cur_dest
    subl    %eax, remaining
    jc      9f                   /* the last chunk was short */

    movw    $msg_dot, %si
    call    print
    jmp     1b

9:  movw    $msg_nl, %si
    call    print
    ret

read_failed:
    movw    $msg_readfail, %si
    jmp     die

/* Fills in the structure the kernel is handed in EBX. Only the flags it
   actually reads are set: the memory map, the totals, and a command line,
   which is how `selftest` would be passed if anyone wanted to. */
build_mbi:
    /* zero it */
    movw    $MBI_ADDR, %di
    movw    $88, %cx
    xorb    %al, %al
1:  movb    %al, (%di)
    incw    %di
    decw    %cx
    jnz     1b

    movw    $MBI_ADDR, %di
    movl    $((1 << 0) | (1 << 2) | (1 << 6)), (%di)     /* mem, cmdline, mmap */
    movl    $640, 4(%di)                                  /* mem_lower, KiB */
    movl    $0, 8(%di)                                    /* filled in below */
    movl    $CMDLINE_ADDR, 16(%di)
    movl    mmap_bytes, %eax
    movl    %eax, 44(%di)
    movl    $MMAP_ADDR, 48(%di)

    /* mem_upper is the size of the first usable run above 1 MiB, in KiB.
       The kernel prefers the map and only falls back to this. */
    movw    $MMAP_ADDR, %si
    xorl    %edx, %edx
2:  movl    mmap_bytes, %eax
    movw    %si, %bx
    subw    $MMAP_ADDR, %bx
    cmpl    %eax, %ebx
    jae     3f
    cmpl    $1, 20(%si)          /* type 1 is usable RAM */
    jne     4f
    cmpl    $0x100000, 4(%si)
    jb      4f
    movl    12(%si), %edx        /* low half of the length */
    shrl    $10, %edx
    jmp     3f
4:  addw    $24, %si
    jmp     2b
3:  movw    $MBI_ADDR, %di
    movl    %edx, 8(%di)

    /* An empty command line, so the pointer is valid rather than dangling. */
    movw    $CMDLINE_ADDR, %di
    movb    $0, (%di)
    ret

/* --- protected mode ----------------------------------------------------- */

.code32
protected:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    $0x7C00, %esp

    movl    $0x2BADB002, %eax    /* what a multiboot kernel checks for */
    movl    $MBI_ADDR, %ebx
    movl    patch_entry, %ecx
    jmp     *%ecx

/* --- data --------------------------------------------------------------- */

.code16
boot_drive:  .byte 0
.align 4
mmap_bytes:  .long 0
sector_size:   .word 2048
lba_scale:     .word 1
chunk_sectors: .word CHUNK_SECTORS
cur_lba:     .long 0
cur_dest:    .long 0
remaining:   .long 0

.align 4
dap:
    .byte   16                   /* packet size */
    .byte   0
dap_count:
    .word   0
dap_offset:
    .word   0
dap_segment:
    .word   BUFFER_SEG
dap_lba:
    .long   0
    .long   0

/* INT 13h AH=48h writes its answer here. */
.align 4
params:
    .space  0x1E

.align 8
gdt:
    .quad   0
    /* code32: base 0, limit 4 GiB, present, ring 0, execute/read */
    .word   0xFFFF, 0x0000
    .byte   0x00, 0x9A, 0xCF, 0x00
    /* data32: base 0, limit 4 GiB, present, ring 0, read/write */
    .word   0xFFFF, 0x0000
    .byte   0x00, 0x92, 0xCF, 0x00
gdt_end:

gdt_desc:
    .word   gdt_end - gdt - 1
    .long   gdt

msg_boot:     .asciz "nyx bootloader\r\nloading "
msg_dot:      .asciz "."
msg_nl:       .asciz "\r\n"
msg_go:       .asciz "starting kernel\r\n"
msg_halt:     .asciz "\r\nhalted.\r\n"
msg_nomem:    .asciz "the firmware would not describe memory"
msg_readfail: .asciz "could not read the boot device"
