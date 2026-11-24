#include "idt.h"

#include <stdint.h>

/* The CPU looks up interrupt N in entry N. */
#define IDT_SIZE 256

/* One eight-byte IDT gate, exactly as the CPU lays it out. */
struct idt_entry {
    uint16_t base_low;   // bits 0-15 of the handler address
    uint16_t selector;   // code segment the handler runs in (0x08)
    uint8_t reserved;    // always zero
    uint8_t flags;       // present bit, ring number, gate type
    uint16_t base_high;  // bits 16-31 of the handler address
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

    // The stubs cover the 32 exceptions and the 16 remapped IRQs; the
    // remaining vectors stay not-present so a stray interrupt faults.
    for (int i = 0; i < 48; i++) {
        idt_set_gate(i, (uint32_t)isr_stub_table[i]);
    }

    __asm__ volatile("lidt %0" : : "m"(idt_pointer));
}
