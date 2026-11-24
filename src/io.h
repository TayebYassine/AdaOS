#ifndef ADAOS_IO_H
#define ADAOS_IO_H

#include <stdint.h>

// Port I/O: x86 has a second address space beside memory, and hardware is
// reached through numbered ports on it. These wrappers are single inline
// assembly instructions that move one byte or word across one port; they are
// volatile, so the compiler can neither drop them nor reorder them. Drivers
// use ports for device registers (VGA at 0x3D4/0x3D5, the keyboard
// controller at 0x60/0x64, the ACPI power button at 0x604).

/* Send one byte to an I/O port. */
void outb(uint16_t port, uint8_t value);

/* Send one 16-bit word to an I/O port. */
void outw(uint16_t port, uint16_t value);

/* Read one byte from an I/O port. */
uint8_t inb(uint16_t port);

#endif /* ADAOS_IO_H */
