#ifndef ADAOS_IO_H
#define ADAOS_IO_H

#include <stdint.h>

/* Send one byte to an I/O port. */
void outb(uint16_t port, uint8_t value);

/* Read one byte from an I/O port. */
uint8_t inb(uint16_t port);

#endif /* ADAOS_IO_H */
