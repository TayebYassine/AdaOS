#ifndef ADAOS_IDT_H
#define ADAOS_IDT_H

// The Interrupt Descriptor Table: one eight-byte gate per interrupt vector,
// naming the address that vector jumps to, the code segment it runs in and the
// privilege level it may be entered from. This table is filled from the stubs
// in isr.asm (one per vector, exported through isr_stub_table[]), which save
// the register frame and call the C dispatcher. Vectors left "not present"
// fault on a stray interrupt instead of running anything.

/* Build the IDT from the stubs in isr.asm and load it with "lidt". */
void idt_init(void);

#endif /* ADAOS_IDT_H */
