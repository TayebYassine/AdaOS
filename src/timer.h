#ifndef ADAOS_TIMER_H
#define ADAOS_TIMER_H

#include <stdint.h>

/* Start the PIT on IRQ0; call once, after interrupt_init(). */
void timer_init(void);

/* How many ticks have gone by since the kernel started. */
uint32_t timer_ticks(void);

/* Whole seconds of uptime since the kernel started. */
uint32_t timer_seconds(void);

/* Idle the CPU until the given number of milliseconds has passed. */
void timer_sleep(uint32_t milliseconds);

#endif /* ADAOS_TIMER_H */
