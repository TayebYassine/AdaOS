#include "io.h"

/*
 * "outb" = OUT a Byte, "inb" = IN a Byte: send/receive one byte to/from
 * an I/O port. CPUs have a second address space separate from memory,
 * used to talk to hardware. The VGA chip listens on ports 0x3D4/0x3D5,
 * the keyboard controller on ports 0x60/0x64.
 *
 * This is GCC inline assembly: "a" puts the value in register AL,
 * "Nd" is the port number, "=a" reads the result back from AL, and
 * "volatile" forbids the compiler from removing or reordering it.
 */
void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
