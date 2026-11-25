#ifndef ADAOS_SCHEDULER_H
#define ADAOS_SCHEDULER_H

#include <stdint.h>

#include "interrupt.h"

// Kernel threads switched on the timer tick. Every task has its own stack and a
// saved register frame; a switch stores the frame the interrupt pushed, points
// esp at another task's frame and lets context.asm iret into it. Tasks run
// round-robin, and a sleeping task is simply skipped until its wake tick.

/* One task, as the "tasks" command shows it. */
struct task_info {
    const char* name;
    const char* state;       // "running", "ready", "sleeping" or "dead"
    uint32_t runtime_ticks;  // ticks spent on the CPU
};

/* Create the shell and idle tasks. Call after kmalloc_init() and
 * timer_init(), with interrupts on. */
void scheduler_init(void);

/* Become the first task and never return: the caller's stack is abandoned. */
void scheduler_start(void) __attribute__((noreturn));

/* Called from IRQ0: count the tick, wake sleepers, ask for a switch. */
void scheduler_tick(struct interrupt_frame* frame);

/* Did the timer ask for a switch? Checked at the end of the interrupt. */
int scheduler_should_switch(void);

/* Save the interrupted frame and switch to the next task; does not return. */
void scheduler_switch_from(struct interrupt_frame* frame);

/* Retire the running task after a fault and switch away; returns 0 only when
 * there is no task to retire (a fault before the scheduler started). */
int scheduler_kill_current(struct interrupt_frame* frame);

/* Sleep the current task without blocking the machine: it stops being
 * scheduled while everything else keeps running. */
void scheduler_sleep_ms(uint32_t milliseconds);

/* Task i for display; returns 0 when there is no such task. */
int scheduler_task_info(int index, struct task_info* info);

/* How many tasks exist. */
int scheduler_task_count(void);

/* How often the idle task was woken from hlt. */
uint32_t scheduler_idle_ticks(void);

#endif /* ADAOS_SCHEDULER_H */
