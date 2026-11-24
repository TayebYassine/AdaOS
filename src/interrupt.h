#ifndef ADAOS_INTERRUPT_H
#define ADAOS_INTERRUPT_H

#include <stdint.h>

// The C half of interrupt handling: isr.asm saves the registers and calls
// interrupt_handler() with this frame, and this file decides what runs - one
// handler per IRQ line, the report for CPU exceptions, the end-of-interrupt
// acknowledge, and the small helpers that switch interrupts on and off.

/* Register frame a handler receives, listed from the lowest address up. */
struct interrupt_frame {
    uint32_t gs, fs, es, ds;                          // segment registers
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  // pushed by pusha
    uint32_t int_number;       // vector: 0x20 = IRQ0, 0x21 = IRQ1...
    uint32_t error_code;       // 0 when the CPU pushed none
    uint32_t eip, cs, eflags;  // pushed by the CPU
};

/* Remap and mask the PIC, then switch interrupts on. */
void interrupt_init(void);

/* Handle "irq" (0-15) with the given function from now on. */
void interrupt_register_irq(int irq,
                            void (*handler)(struct interrupt_frame* frame));

/* Called from isr.asm for every interrupt; also sends the EOI to the PIC. */
void interrupt_handler(struct interrupt_frame* frame);

/* Park the CPU until the next interrupt arrives. */
void interrupt_wait(void);

/* Switch hardware interrupts on. */
void interrupt_enable(void);

/* Switch hardware interrupts off. */
void interrupt_disable(void);

/* The flags register as interrupt_save() found it, IF included. */
typedef uint32_t interrupt_flags;

/* Block hardware interrupts and hand back the old state, so a lock can put
 * back exactly what it found instead of blindly enabling. */
interrupt_flags interrupt_save(void);

/* Put back the state interrupt_save() returned. */
void interrupt_restore(interrupt_flags flags);

#endif /* ADAOS_INTERRUPT_H */
