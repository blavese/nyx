/* The kernel is linked to run at 1 MiB physically. Both loaders put it there
   and neither relocates it.

   .kheader comes first so a loader can read the entry point out of the first
   sixteen bytes of the flat image, and .multiboot must stay inside the first
   8 KiB for a multiboot loader to find it. */
ENTRY(_start)
SECTIONS {
    . = 1M;
    __kernel_start = .;
    .text : ALIGN(4K) { *(.kheader) *(.multiboot) *(.text*) }
    .rodata : ALIGN(4K) { *(.rodata*) }
    .data : ALIGN(4K) { *(.data*) }
    .bss : ALIGN(4K) { __bss_start = .; *(COMMON) *(.bss*) __bss_end = .; }
    __kernel_end = .;
}
