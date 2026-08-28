/* User programs are loaded well above the kernel, which lives in the low
   16 MiB that every address space shares. */
ENTRY(_start)
SECTIONS {
    . = 0x40000000;
    .text : ALIGN(4K) { *(.text._start) *(.text*) }
    .rodata : ALIGN(4K) { *(.rodata*) }
    .data : ALIGN(4K) { *(.data*) }
    .bss : ALIGN(4K) { *(COMMON) *(.bss*) }
}
