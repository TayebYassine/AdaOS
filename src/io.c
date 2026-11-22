#include "io.h"

// Port I/O: "outb" = OUT a Byte, "outw" = OUT a Word, "inb" = IN a
// Byte. CPUs have a second address space separate from memory, used
// to talk to hardware: the VGA chip listens on 0x3D4/0x3D5, the
// keyboard controller on 0x60/0x64, the ACPI power button on 0x604.
// These are GCC inline assembly instructions, marked "volatile" so
// the compiler can neither remove nor reorder them.

/* Send one byte to an I/O port. */
void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* Send one 16-bit word to an I/O port. */
void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

/* Read one byte from an I/O port. */
uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
