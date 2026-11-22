#include "idt.h"

#include <stdint.h>

// The Interrupt Descriptor Table (IDT) is to interrupts what the GDT
// is to segments: an array of gates the CPU walks through whenever an
// interrupt arrives. Each gate says "interrupt number N jumps to this
// address, running at ring 0, in this code segment".
//
// We fill in 48 gates: 0x00-0x1F for the exceptions the CPU raises on
// its own (divide by zero, page fault, ...) and 0x20-0x2F for the 16
// hardware interrupts - pic.c remaps the PIC so that IRQ0 arrives as
// 0x20, IRQ1 as 0x21, and so on. The handler addresses all come from
// isr.asm, which exports one stub per interrupt in isr_stub_table[].
//
// The remaining vectors (0x30-0xFF) are left "not present": a stray
// interrupt to one of them faults, and our exception handler reports
// it instead of letting the CPU reset.

/* The CPU looks up interrupt N in entry N. */
#define IDT_SIZE 256

/* One eight-byte IDT gate, exactly as the CPU lays it out. */
struct idt_entry {
    /* bits 0-15 of the handler address */
    uint16_t base_low;
    /* code segment the handler runs in (0x08) */
    uint16_t selector;
    /* always zero */
    uint8_t reserved;
    /* present bit, ring number, gate type */
    uint8_t flags;
    /* bits 16-31 of the handler address */
    uint16_t base_high;
} __attribute__((packed));

/* The pointer "lidt" wants: a 16-bit limit followed by the base. */
struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Present + 32-bit interrupt gate: interrupts stay off while it runs. */
#define IDT_FLAG_INTERRUPT_GATE 0x8E

/* One stub per vector, defined in isr.asm. */
extern void (*isr_stub_table[])(void);

static struct idt_entry idt[IDT_SIZE];
static struct idt_pointer idt_pointer;

/* Point one IDT entry at the given handler address. */
static void idt_set_gate(int vector, uint32_t handler) {
    idt[vector].base_low = (uint16_t)(handler & 0xFFFF);
    idt[vector].base_high = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[vector].selector = 0x08;
    idt[vector].reserved = 0;
    idt[vector].flags = IDT_FLAG_INTERRUPT_GATE;
}

/* Build the table from isr_stub_table[] and load it with "lidt". */
void idt_init(void) {
    idt_pointer.limit = (uint16_t)(sizeof(idt) - 1);
    idt_pointer.base = (uint32_t)&idt[0];

    // Point the 48 vectors we have stubs for; the rest stay not-present.
    for (int i = 0; i < 48; i++) {
        idt_set_gate(i, (uint32_t)isr_stub_table[i]);
    }

    __asm__ volatile("lidt %0" : : "m"(idt_pointer));
}
