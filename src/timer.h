#ifndef ADAOS_TIMER_H
#define ADAOS_TIMER_H

#include <stdint.h>

// The Programmable Interval Timer (PIT): a counter that counts down from a
// programmed divisor and raises IRQ0 each time it reaches zero. That interrupt
// is the kernel's time base - uptime, sleeps, the wall clock and the scheduler
// all count its ticks - so it is set to 100 per second.

/* Start the PIT on IRQ0; call once, after interrupt_init(). */
void timer_init(void);

/* How many ticks have gone by since the kernel started. */
uint32_t timer_ticks(void);

/* Whole seconds of uptime since the kernel started. */
uint32_t timer_seconds(void);

/* Idle the CPU until the given number of milliseconds has passed. */
void timer_sleep(uint32_t milliseconds);

#endif /* ADAOS_TIMER_H */
