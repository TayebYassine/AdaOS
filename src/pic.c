#include "pic.h"

#include <stdint.h>

#include "io.h"

// The PIC (Programmable Interrupt Controller) is a pair of 8259 chips
// that turn 16 electrical IRQ lines into CPU interrupts. The original
// IBM PC mapped them to 0x08-0x0F and 0x70-0x77, which collide with
// CPU exceptions nowadays (0x08 is the double fault), so the first
// thing we do is "remap" them to 0x20-0x2F: a keypress arrives as
// interrupt 0x21 (IRQ1), a timer tick as 0x20 (IRQ0).
//
// The two chips are programmed through these ports:
//
//   0x20 / 0x21 - master chip, handles IRQ0-IRQ7
//   0xA0 / 0xA1 - slave chip, handles IRQ8-IRQ15, and feeds into
//                 IRQ2 of the master

#define PIC_MASTER_COMMAND 0x20
#define PIC_MASTER_DATA 0x21
#define PIC_SLAVE_COMMAND 0xA0
#define PIC_SLAVE_DATA 0xA1

#define PIC_EOI 0x20

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
