; context_switch(new_esp): the last step of a task switch.
;
; A switch happens inside the timer interrupt, where isr.asm has
; already pushed a complete register frame on the interrupted task's
; stack (layout in interrupt.h: gs first, eflags last). The scheduler
; saved the address of that frame as the task's stack pointer, so
; "resuming" a task is nothing more than making its frame the current
; stack and popping what the interrupt stub would have popped.
;
; This never returns to its caller: the "call" that brought us here
; belonged to the old task's stack, which is gone the moment esp moves.

section .text

global context_switch
; void context_switch(uint32_t new_esp);
context_switch:
    mov eax, [esp + 4]  ; the argument: the new task's saved esp
    mov esp, eax        ; switch stacks, abandoning the old one

    ; Undo what common_stub in isr.asm does, from the new task's frame.
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 8          ; drop the interrupt number and error code
    iret                ; and resume the new task where it stopped

section .note.GNU-stack noalloc noexec nowrite progbits
