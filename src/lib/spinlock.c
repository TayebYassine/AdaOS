#include "spinlock.h"

#include <stdint.h>

#include "interrupt.h"

/* Locks taken and not yet released, newest last. Taking a lock turns
 * interrupts off, so nothing can switch tasks between the push and the pop and
 * the list always matches the nesting exactly. */
#define SPINLOCK_MAX_HELD 8

static struct spinlock* held_locks[SPINLOCK_MAX_HELD];
static int held_count = 0;

/* Take the lock: interrupts off, then wait for the flag to clear. */
void spinlock_acquire(struct spinlock* lock) {
    uint32_t flags = interrupt_save();

    while (__sync_lock_test_and_set(&lock->locked, 1u) != 0) {
    }

    lock->flags = flags;

    if (held_count < SPINLOCK_MAX_HELD) {
        held_locks[held_count] = lock;
        held_count++;
    }
}

/* Give the lock back and restore the interrupt state from before. */
void spinlock_release(struct spinlock* lock) {
    __sync_lock_release(&lock->locked);

    // Drop it from the held list; normally it is the newest one.
    for (int i = held_count - 1; i >= 0; i--) {
        if (held_locks[i] == lock) {
            for (int j = i; j + 1 < held_count; j++) {
                held_locks[j] = held_locks[j + 1];
            }
            held_count--;
            break;
        }
    }

    interrupt_restore(lock->flags);
}

/* Release everything still held and leave interrupts off. */
void spinlock_release_all_held(void) {
    while (held_count > 0) {
        held_count--;
        __sync_lock_release(&held_locks[held_count]->locked);
        held_locks[held_count] = 0;
    }

    // The caller is about to leave the CPU for good, and the task it switches
    // to turns interrupts on again from its own saved flags.
    interrupt_disable();
}
