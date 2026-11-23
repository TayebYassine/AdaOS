; Multiboot entry point: GRUB loads this file, and _start sets up the
; stack and calls kernel_main() in kernel.c. Everything else is brought
; up from C.

MAGIC    equ 0x1BADB002
FLAGS    equ 2
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .bss
align 16
stack_bottom: resb 16384   ; 16 KB stack for the kernel
stack_top:

section .text
global _start
extern kernel_main
_start:
    mov esp, stack_top
    push ebx
    push eax
    call kernel_main
.hang:
    cli
    hlt                    ; kernel_main returned, which it never should
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
