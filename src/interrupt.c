#include "interrupt.h"

#include <stdint.h>

#include "kprintf.h"
#include "pic.h"
#include "screen.h"

// The C half of the interrupt machinery. isr.asm saves the registers
// and calls interrupt_handler() with a pointer to them; everything
// else lives here: the table of IRQ handlers, the report we print when
// the CPU faults, and the idle loop.

/* Where pic.c maps IRQ0. */
#define IRQ_BASE 0x20
/* IRQ0-IRQ15. */
#define IRQ_COUNT 16

static void (*irq_handlers[IRQ_COUNT])(struct interrupt_frame* frame);

/* What each of the CPU's own exceptions (0x00-0x1F) is called. */
static const char* exception_names[32] = {
    "Divide Error",                /* 0x00 */
    "Debug",                       /* 0x01 */
    "Non-Maskable Interrupt",      /* 0x02 */
    "Breakpoint",                  /* 0x03 */
    "Overflow",                    /* 0x04 */
    "Bound Range Exceeded",        /* 0x05 */
    "Invalid Opcode",              /* 0x06 */
    "Device Not Available",        /* 0x07 */
    "Double Fault",                /* 0x08 */
    "Coprocessor Segment Overrun", /* 0x09 */
    "Invalid TSS",                 /* 0x0A */
    "Segment Not Present",         /* 0x0B */
    "Stack-Segment Fault",         /* 0x0C */
    "General Protection Fault",    /* 0x0D */
    "Page Fault",                  /* 0x0E */
    "Reserved",                    /* 0x0F */
    "x87 Floating-Point",          /* 0x10 */
    "Alignment Check",             /* 0x11 */
    "Machine Check",               /* 0x12 */
    "SIMD Floating-Point",         /* 0x13 */
    "Virtualization",              /* 0x14 */
    "Control Protection",          /* 0x15 */
    "Reserved",                    /* 0x16 */
    "Reserved",                    /* 0x17 */
    "Reserved",                    /* 0x18 */
    "Reserved",                    /* 0x19 */
    "Reserved",                    /* 0x1A */
    "Reserved",                    /* 0x1B */
    "Reserved",                    /* 0x1C */
    "Reserved",                    /* 0x1D */
    "Security Exception",          /* 0x1E */
    "Reserved",                    /* 0x1F */
};

/* Report the exception and halt; there is nothing safe to return to. */
static void handle_exception(struct interrupt_frame* frame) {
    kprintf("\nAdaOS: exception %d (%s)\n", (int)frame->int_number,
            exception_names[frame->int_number]);
    kprintf("  error code 0x%x   eip 0x%x\n", frame->error_code, frame->eip);
    print("System halted.\n");

    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* Route one interrupt: exceptions halt, IRQs go to their registered handler. */
void interrupt_handler(struct interrupt_frame* frame) {
    if (frame->int_number < IRQ_BASE) {
        handle_exception(frame);
        return;
    }

    if (frame->int_number >= IRQ_BASE + IRQ_COUNT) {
        return;
    }

    int irq = (int)frame->int_number - IRQ_BASE;

    if (irq_handlers[irq] != 0) {
        irq_handlers[irq](frame);
    }

    pic_send_eoi(irq);
}

/* Remember "handler" for irq and open that line on the PIC. */
void interrupt_register_irq(int irq,
                            void (*handler)(struct interrupt_frame* frame)) {
    if (irq < 0 || irq >= IRQ_COUNT) {
        return;
    }

    irq_handlers[irq] = handler;
    pic_unmask(irq);
}

/* Allow hardware interrupts (sti). */
void interrupt_enable(void) { __asm__ volatile("sti" ::: "memory"); }

/* Block hardware interrupts (cli). */
void interrupt_disable(void) { __asm__ volatile("cli" ::: "memory"); }

/* Wake up on the next interrupt: sti followed by hlt. */
void interrupt_wait(void) { __asm__ volatile("sti; hlt" ::: "memory"); }

/* Remap and mask the PIC, then switch interrupts on. */
void interrupt_init(void) {
    pic_init();

    interrupt_enable();
}
