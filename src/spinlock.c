#include "spinlock.h"

#include <stdint.h>

#include "interrupt.h"

// Spinlocks: one task inside a kernel data structure at a time.
//
// On a single CPU a lock means mostly "keep interrupts out": an IRQ
// handler that wanted the same structure could never wait for the task
// it interrupted, so it would spin forever. The interrupt state is saved
// in the lock and put back on release, so nesting works and a caller
// that had interrupts off does not get them switched on behind its back.
//
// The flag is set with an atomic exchange as well, which costs nothing
// here and means a second CPU would not walk in during the same window.

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
