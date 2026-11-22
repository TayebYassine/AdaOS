; Interrupt service routine (ISR) stubs: the assembly half of the
; interrupt machinery. The CPU enters these stubs before any C code
; runs; each one builds a register frame on the stack (layout in
; interrupt.h) and hands it to interrupt_handler() in interrupt.c.
; isr_stub_table lets idt.c point every IDT entry at its stub.

section .text

; The CPU already pushed an error code, so only the number is left.
%macro ISR_ERROR 1
global isr%1
isr%1:
    push dword %1       ; interrupt number
    jmp common_stub
%endmacro

; No error code: push a zero so both frames look the same.
%macro ISR_NO_ERROR 1
global isr%1
isr%1:
    push dword 0        ; fake error code
    push dword %1       ; interrupt number
    jmp common_stub
%endmacro

; 0x00-0x1F: exceptions the CPU raises on its own.
ISR_NO_ERROR 0         ; divide by zero
ISR_NO_ERROR 1         ; debug
ISR_NO_ERROR 2         ; non-maskable interrupt
ISR_NO_ERROR 3         ; breakpoint
ISR_NO_ERROR 4         ; overflow
ISR_NO_ERROR 5         ; bound range exceeded
ISR_NO_ERROR 6         ; invalid opcode
ISR_NO_ERROR 7         ; device not available
ISR_ERROR    8         ; double fault (error code)
ISR_NO_ERROR 9         ; coprocessor segment overrun
ISR_ERROR    10        ; invalid TSS (error code)
ISR_ERROR    11        ; segment not present (error code)
ISR_ERROR    12        ; stack-segment fault (error code)
ISR_ERROR    13        ; general protection fault (error code)
ISR_ERROR    14        ; page fault (error code)
ISR_NO_ERROR 15        ; reserved
ISR_NO_ERROR 16        ; x87 floating-point exception
ISR_ERROR    17        ; alignment check (error code)
ISR_NO_ERROR 18        ; machine check
ISR_NO_ERROR 19        ; SIMD floating-point exception
ISR_NO_ERROR 20        ; virtualization exception
ISR_ERROR    21        ; control protection exception (error code)
ISR_NO_ERROR 22        ; reserved
ISR_NO_ERROR 23        ; reserved
ISR_NO_ERROR 24        ; reserved
ISR_NO_ERROR 25        ; reserved
ISR_NO_ERROR 26        ; reserved
ISR_NO_ERROR 27        ; reserved
ISR_NO_ERROR 28        ; reserved
ISR_NO_ERROR 29        ; reserved
ISR_NO_ERROR 30        ; security exception
ISR_NO_ERROR 31        ; reserved

; 0x20-0x2F: the 16 hardware IRQs, after pic.c remapped them.
ISR_NO_ERROR 32        ; IRQ0 - the timer
ISR_NO_ERROR 33        ; IRQ1 - the keyboard
ISR_NO_ERROR 34        ; IRQ2 - the slave PIC (wired into the master)
ISR_NO_ERROR 35        ; IRQ3 - serial port 2
ISR_NO_ERROR 36        ; IRQ4 - serial port 1
ISR_NO_ERROR 37        ; IRQ5 - parallel port 2
ISR_NO_ERROR 38        ; IRQ6 - floppy disk
ISR_NO_ERROR 39        ; IRQ7 - parallel port 1
ISR_NO_ERROR 40        ; IRQ8 - real-time clock
ISR_NO_ERROR 41        ; IRQ9 - ACPI / general
ISR_NO_ERROR 42        ; IRQ10 - general
ISR_NO_ERROR 43        ; IRQ11 - general
ISR_NO_ERROR 44        ; IRQ12 - PS/2 mouse
ISR_NO_ERROR 45        ; IRQ13 - floating-point unit
ISR_NO_ERROR 46        ; IRQ14 - primary ATA disk
ISR_NO_ERROR 47        ; IRQ15 - secondary ATA disk

; Everything the stubs share: the frame is on the stack, so all that is
; left is to hand it to C and undo it all afterwards.
extern interrupt_handler

common_stub:
    pusha               ; EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    push ds
    push es
    push fs
    push gs             ; pushed last = lowest address = start of frame

    ; Give every segment the kernel data selector (0x10) from gdt.c.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov ebp, esp        ; remember the frame (the old EBP is in pusha)
    and esp, -16        ; the i386 C ABI wants a 16-byte aligned stack
    sub esp, 16         ; room for the argument, alignment kept
    mov [esp], ebp      ; first argument: struct interrupt_frame*
    call interrupt_handler
    mov esp, ebp        ; back to the frame

    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8          ; drop the interrupt number and error code
    iret                ; and return to whatever was interrupted

; One stub address per interrupt number; idt.c indexes this table.
section .data

global isr_stub_table
isr_stub_table:
    dd isr0,  isr1,  isr2,  isr3      ; 0x00-0x03 CPU exceptions
    dd isr4,  isr5,  isr6,  isr7      ; 0x04-0x07
    dd isr8,  isr9,  isr10, isr11     ; 0x08-0x0B
    dd isr12, isr13, isr14, isr15     ; 0x0C-0x0F
    dd isr16, isr17, isr18, isr19     ; 0x10-0x13
    dd isr20, isr21, isr22, isr23     ; 0x14-0x17
    dd isr24, isr25, isr26, isr27     ; 0x18-0x1B
    dd isr28, isr29, isr30, isr31     ; 0x1C-0x1F
    dd isr32, isr33, isr34, isr35     ; 0x20-0x23 IRQ0-3
    dd isr36, isr37, isr38, isr39     ; 0x24-0x27 IRQ4-7
    dd isr40, isr41, isr42, isr43     ; 0x28-0x2B IRQ8-11
    dd isr44, isr45, isr46, isr47     ; 0x2C-0x2F IRQ12-15
