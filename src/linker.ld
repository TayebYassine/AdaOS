/* Linker script: the kernel is loaded at 1 MiB, where GRUB puts a multiboot
   image, and every section follows in order. kernel_start and kernel_end mark
   the image so the memory manager can keep its pages reserved. */

ENTRY(_start)

SECTIONS {
    . = 1M;
    kernel_start = .;

    .text   : { *(.multiboot) *(.text) } /* entry point and code */
    .rodata : { *(.rodata*) }            /* constants and strings */
    .data   : { *(.data) }               /* initialised variables */
    .bss    : { *(COMMON) *(.bss) }      /* zero-initialised variables */

    . = ALIGN(4096);
    kernel_end = .;
}
