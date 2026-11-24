#include "pic.h"

#include <stdint.h>

#include "io.h"

// The two 8259 chips: master handles IRQ0-IRQ7, slave handles IRQ8-IRQ15 and
// feeds the master through IRQ2.
#define PIC_MASTER_COMMAND 0x20  // master: command and status
#define PIC_MASTER_DATA 0x21     // master: interrupt mask
#define PIC_SLAVE_COMMAND 0xA0   // slave: command and status
#define PIC_SLAVE_DATA 0xA1      // slave: interrupt mask

#define PIC_EOI 0x20  // end-of-interrupt command byte

/* The slave PIC must be reachable through IRQ2 on the master. */
#define PIC_SLAVE_IRQ 0x04

/* Remap both chips to 0x20-0x2F and mask all 16 lines. */
void pic_init(void) {
    // ICW1: initialise + expect ICW4
    outb(PIC_MASTER_COMMAND, 0x11);
    outb(PIC_SLAVE_COMMAND, 0x11);

    // ICW2: where each chip's IRQs start - IRQ0 becomes 0x20, IRQ8 becomes
    // 0x28.
    outb(PIC_MASTER_DATA, 0x20);
    outb(PIC_SLAVE_DATA, 0x28);

    // ICW3: the slave answers on IRQ2 of the master (0x04 = bit 2).
    outb(PIC_MASTER_DATA, PIC_SLAVE_IRQ);
    outb(PIC_SLAVE_DATA, 0x02);

    // ICW4: talk 8086, the mode every machine since 1984 uses.
    outb(PIC_MASTER_DATA, 0x01);
    outb(PIC_SLAVE_DATA, 0x01);

    // Mask every line: nothing gets through until a driver unmasks its own.
    outb(PIC_MASTER_DATA, 0xFF);
    outb(PIC_SLAVE_DATA, 0xFF);
}

/* Let the given IRQ line (0-15) through to the CPU. */
void pic_unmask(int irq) {
    uint16_t port = (irq < 8) ? PIC_MASTER_DATA : PIC_SLAVE_DATA;
    uint8_t line = (uint8_t)(irq % 8);

    // IRQ8-15 all arrive through IRQ2 on the master, so open that line too.
    if (irq >= 8) {
        outb(PIC_MASTER_DATA,
             inb(PIC_MASTER_DATA) & (uint8_t)~(1 << PIC_SLAVE_IRQ));
    }

    // One bit per line: 0 = let it through. Read first to keep the others.
    outb(port, inb(port) & (uint8_t)~(1 << line));
}

/* Acknowledge an IRQ, otherwise the PIC will not send the next one. */
void pic_send_eoi(int irq) {
    if (irq >= 8) {
        outb(PIC_SLAVE_COMMAND, PIC_EOI);
    }

    outb(PIC_MASTER_COMMAND, PIC_EOI);
}
