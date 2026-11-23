#ifndef ADAOS_SCHEDULER_H
#define ADAOS_SCHEDULER_H

#include <stdint.h>

#include "interrupt.h"

/* One task, as the "tasks" command shows it. */
struct task_info {
    const char* name;
    const char* state;
    uint32_t runtime_ticks;
};

/* Create the shell and idle tasks. Call after kmalloc_init() and
 * timer_init(), with interrupts on. */
void scheduler_init(void);

/* Become the first task. Never returns: the caller's stack is left
 * behind, and the kernel continues inside the shell task. */
void scheduler_start(void) __attribute__((noreturn));

/* Called from IRQ0: count the tick, wake sleepers, ask for a switch. */
void scheduler_tick(struct interrupt_frame* frame);

/* Did the timer ask for a switch? Checked at the end of the interrupt. */
int scheduler_should_switch(void);

/* Save the interrupted task's frame and switch to the next task. Does
 * not return: the next task resumes instead. */
void scheduler_switch_from(struct interrupt_frame* frame);

/* Retire the running task after a fault and switch to another one; its
 * stack is reclaimed on the next tick. Returns 0 when there is no task to
 * retire (a fault before the scheduler started), and otherwise never
 * returns. */
int scheduler_kill_current(struct interrupt_frame* frame);

/* Sleep the current task for the given time without blocking the
 * machine: it stops being scheduled, everything else keeps running. */
void scheduler_sleep_ms(uint32_t milliseconds);

/* Task i for display; returns 0 when there is no such task. */
int scheduler_task_info(int index, struct task_info* info);

/* How many tasks exist. */
int scheduler_task_count(void);

/* How often the idle task was woken from hlt. */
uint32_t scheduler_idle_ticks(void);

#endif /* ADAOS_SCHEDULER_H */
