/* The BIOS loads the MBR at 0x7C00; it relocates itself from there. */
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(7); }
SECTIONS {
    . = 0x7C00;
    .text : { *(.text*) *(.rodata*) *(.data*) } :text
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.bss*) }
}
