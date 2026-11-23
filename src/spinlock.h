#ifndef ADAOS_SPINLOCK_H
#define ADAOS_SPINLOCK_H

#include <stdint.h>

/* A lock that keeps interrupts out while it is held. All zeroes means
 * unlocked, so a file-scope static needs no explicit setup. */
struct spinlock {
    volatile uint32_t locked;
    /* the interrupt state from before the lock was taken */
    uint32_t flags;
};

/* Take the lock, with hardware interrupts off until spinlock_release()
 * puts them back. Locks must be released in the opposite order they
 * were taken. */
void spinlock_acquire(struct spinlock* lock);

/* Release the lock and restore the interrupt state from before it. */
void spinlock_release(struct spinlock* lock);

#endif /* ADAOS_SPINLOCK_H */
