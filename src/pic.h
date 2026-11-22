#ifndef ADAOS_PIC_H
#define ADAOS_PIC_H

/* Remap the PICs off the exception vectors and mask all 16 IRQ lines. */
void pic_init(void);

/* Let the given IRQ line (0-15) through to the CPU. */
void pic_unmask(int irq);

/* Acknowledge an IRQ, otherwise the PIC will not send the next one. */
void pic_send_eoi(int irq);

#endif /* ADAOS_PIC_H */
