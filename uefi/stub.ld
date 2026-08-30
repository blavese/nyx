ENTRY(kentry)
PHDRS { text PT_LOAD FLAGS(7); }
SECTIONS {
    . = 0x100000;
    .text : { *(.entry) *(.text*) *(.rodata*) *(.data*) } :text
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
