/* Copied to physical 0x8000 at runtime, so it is linked there. Every address
   inside it is absolute; there is nothing to relocate it with. */
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(7); }
SECTIONS {
    . = 0x8000;
    .text : { *(.text*) *(.rodata*) *(.data*) } :text
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.bss*) }
}
