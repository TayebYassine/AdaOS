#ifndef ADAOS_GDT_H
#define ADAOS_GDT_H

// The Global Descriptor Table: the array of eight-byte segment descriptors the
// CPU reads whenever a segment register is loaded. Each entry gives a segment
// its base, its limit and a privilege level. This kernel uses a flat model -
// every segment starts at 0 and spans 4 GB, so selector + offset is already a
// linear address - which makes the selectors (not the bases) the part that
// matters: 0x00 null, 0x08 kernel code, 0x10 kernel data, 0x18/0x20 user
// code/data. Loading a new table leaves CS pointing at the old one until a far
// jump reloads it.

/* Load a GDT of our own and reload every segment register from it. */
void gdt_init(void);

#endif /* ADAOS_GDT_H */
