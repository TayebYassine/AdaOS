#ifndef ADAOS_GDT_H
#define ADAOS_GDT_H

/* Load a GDT of our own and reload every segment register from it. */
void gdt_init(void);

#endif /* ADAOS_GDT_H */
