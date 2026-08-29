/* The BIOS loads the boot image at 0x7C00 as raw bytes, so everything is
   linked there and then flattened.

   The explicit PHDRS matters: without it lld gives the ELF headers a load
   segment of their own below 0x7C00, and flattening that would put 52 bytes
   of ELF header in front of the boot code. */
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(7); }
SECTIONS {
    . = 0x7C00;
    .text : { *(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON) } :text
    __loader_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
