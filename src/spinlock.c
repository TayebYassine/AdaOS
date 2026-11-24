#include "spinlock.h"

#include <stdint.h>

#include "interrupt.h"

/* Take the lock: interrupts off, then wait for the flag to clear. */
void spinlock_acquire(struct spinlock* lock) {
    uint32_t flags = interrupt_save();

    while (__sync_lock_test_and_set(&lock->locked, 1u) != 0) {
    }

    lock->flags = flags;
}

/* Give the lock back and restore the interrupt state from before. */
void spinlock_release(struct spinlock* lock) {
    __sync_lock_release(&lock->locked);
    interrupt_restore(lock->flags);
}
