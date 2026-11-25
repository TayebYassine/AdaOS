/* Linker script: the kernel is loaded at 1 MiB, where GRUB puts a multiboot
   image, and every section follows in order. kernel_start and kernel_end mark
   the image so the memory manager can keep its pages reserved. */

ENTRY(_start)

SECTIONS {
    . = 1M;
    kernel_start = .;

    /* The wildcards matter: a compiler is free to split a section into
       .text.foo / .data.foo / .bss.foo, and anything the script does not
       claim becomes an orphan that lands outside kernel_start..kernel_end.
       The page allocator would then hand those bytes out as frames and
       overwrite code or data at run time. */
    .text   : { *(.multiboot) *(.text) *(.text.*) }  /* entry point and code */
    .rodata : { *(.rodata*) }                        /* constants and strings */
    .data   : { *(.data) *(.data.*) }                /* initialised variables */
    .bss    : { *(COMMON) *(.bss) *(.bss.*) }        /* zero-initialised data */

    . = ALIGN(4096);
    kernel_end = .;
}
