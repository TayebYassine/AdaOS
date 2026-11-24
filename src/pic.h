#ifndef ADAOS_PIC_H
#define ADAOS_PIC_H

// The Programmable Interrupt Controller: a master 8259 handling IRQ0-IRQ7 and
// a slave handling IRQ8-IRQ15 through the master's IRQ2 line. The BIOS leaves
// them on vectors that collide with CPU exceptions, so they are remapped to
// 0x20-0x2F at boot, and every line stays masked until a handler unmasks it.
// Each serviced line must be acknowledged or the chip will not send the next.

/* Remap the PICs off the exception vectors and mask all 16 IRQ lines. */
void pic_init(void);

/* Let the given IRQ line (0-15) through to the CPU. */
void pic_unmask(int irq);

/* Acknowledge an IRQ, otherwise the PIC will not send the next one. */
void pic_send_eoi(int irq);

#endif /* ADAOS_PIC_H */
