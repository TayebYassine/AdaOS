#ifndef ADAOS_IDT_H
#define ADAOS_IDT_H

/* Build the IDT from the stubs in isr.asm and load it with "lidt". */
void idt_init(void);

#endif /* ADAOS_IDT_H */
