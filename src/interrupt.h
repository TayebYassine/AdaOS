#ifndef ADAOS_INTERRUPT_H
#define ADAOS_INTERRUPT_H

#include <stdint.h>

/* Register frame a handler receives, listed from the lowest address up. */
struct interrupt_frame {
    /* segment registers */
    uint32_t gs, fs, es, ds;
    /* pushed by pusha */
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    /* vector: 0x20 = IRQ0, 0x21 = IRQ1... */
    uint32_t int_number;
    /* 0 when the CPU did not push one */
    uint32_t error_code;
    /* pushed by the CPU */
    uint32_t eip, cs, eflags;
};

/* Remap and mask the PIC, then switch interrupts on. Safe because every line
 * starts masked. */
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

#endif /* ADAOS_INTERRUPT_H */
