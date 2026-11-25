; context_switch(new_esp): the last step of a task switch. The frame
; isr.asm built sits on the target task's stack, so resuming it means
; adopting that stack and undoing the stub. Never returns to its caller.

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
