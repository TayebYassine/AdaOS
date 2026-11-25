#ifndef ADAOS_SPINLOCK_H
#define ADAOS_SPINLOCK_H

#include <stdint.h>

// A lock around kernel state that an interrupt handler could also reach.
// Taking it saves the interrupt flag and switches interrupts off, so no handler
// can interrupt the holder and wait forever for it; releasing it puts the saved
// flag back, which makes nesting safe and leaves a caller that already had
// interrupts off undisturbed. The flag itself is taken with an atomic exchange.

/* One lock: all zeroes means unlocked, so a static needs no setup. */
struct spinlock {
    volatile uint32_t locked;
    uint32_t flags;  // interrupt state from before the lock was taken
};

/* Take the lock, with interrupts off; locks release in reverse order. */
void spinlock_acquire(struct spinlock* lock);

/* Release the lock and restore the interrupt state from before it. */
void spinlock_release(struct spinlock* lock);

/* Release every lock still held, newest first, and leave interrupts off. The
 * scheduler calls this when a task dies: a task that faults inside a critical
 * section would otherwise leave the lock set for ever, and every other task
 * would spin on it with interrupts already off. */
void spinlock_release_all_held(void);

#endif /* ADAOS_SPINLOCK_H */
