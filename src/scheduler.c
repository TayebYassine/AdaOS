#include "scheduler.h"

#include <stdint.h>

#include "interrupt.h"
#include "kmalloc.h"
#include "kprintf.h"
#include "libc.h"
#include "shell.h"
#include "timer.h"

/* Tasks the scheduler can hold. The shell and the idle task are two. */
#define MAX_TASKS 8

/* Stack each task gets; interrupt handlers run on it, so it is roomy. */
#define TASK_STACK_SIZE (16u * 1024u)

/* Dwords in a saved interrupt frame: 4 segment registers, 8 general
 * registers, vector, error code, eip, cs, eflags. */
#define FRAME_DWORDS 17

/* Task states. */
enum task_state {
    TASK_READY,     // can run as soon as it is picked
    TASK_RUNNING,   // on the CPU right now
    TASK_SLEEPING,  // waiting for its wake tick
    TASK_DEAD,      // finished or faulted, never runs again
};

struct task {
    uint32_t esp;         // start of its saved interrupt frame
    uint8_t* stack;       // the block from kmalloc(); 0 once reclaimed
    void (*entry)(void);  // where a fresh task begins; never returns
    enum task_state state;
    uint32_t wake_tick;
    uint32_t runtime_ticks;
    const char* name;
};

static struct task* tasks[MAX_TASKS];
static int task_count = 0;
static int current_index = 0;

/* The task on the CPU; 0 until scheduler_start(). */
static struct task* current = 0;

/* Set by the timer tick, consumed by interrupt_handler(). */
static volatile int need_resched = 0;

/* How often the idle task came back from hlt. */
static volatile uint32_t idle_wakes = 0;

/* From context.asm; never returns. */
extern void context_switch(uint32_t esp) __attribute__((noreturn));

/* Defined below; a task whose entry function returned uses it to leave. */
static void retire_and_switch(struct task* dying) __attribute__((noreturn));

/* The idle task: halt until the next interrupt, forever. */
static void idle_main(void) {
    for (;;) {
        __asm__ volatile("hlt");
        idle_wakes++;
    }
}

/* The shell, as a task. */
static void shell_main(void) { shell_run(); }

/* Every task starts here: context_switch() iret'ed into it. */
static void task_bootstrap(void) {
    struct task* me = current;

    me->entry();

    // The entry function returned, so the task is finished. It leaves the
    // CPU for good here; the stack goes back to the heap on the next tick.
    retire_and_switch(me);
}

/* Name of a state, for display. */
static const char* state_name(enum task_state state) {
    switch (state) {
        case TASK_READY:
            return "READY";
        case TASK_RUNNING:
            return "RUNNING";
        case TASK_SLEEPING:
            return "SLEEPING";
        default:
            return "DEAD";
    }
}

/* Build the initial interrupt frame, exactly the shape isr.asm leaves behind,
 * so context_switch() can start the task by iretting into task_bootstrap(). */
static uint32_t build_frame(uint32_t* frame) {
    frame[0] = 0x10;  // gs: kernel data selector from gdt.c
    frame[1] = 0x10;  // fs
    frame[2] = 0x10;  // es
    frame[3] = 0x10;  // ds
    frame[4] = 0;     // edi
    frame[5] = 0;     // esi
    frame[6] = 0;     // ebp
    frame[7] = 0;     // esp, ignored by popa
    frame[8] = 0;     // ebx
    frame[9] = 0;     // edx
    frame[10] = 0;    // ecx
    frame[11] = 0;    // eax
    frame[12] = 0;    // interrupt number
    frame[13] = 0;    // error code
    frame[14] = (uint32_t)task_bootstrap;
    frame[15] = 0x08;   // cs: kernel code selector
    frame[16] = 0x202;  // eflags: interrupts on

    return (uint32_t)frame;
}

/* Create a task with its own stack from the heap. */
static struct task* task_create(const char* name, void (*entry)(void)) {
    if (task_count >= MAX_TASKS) {
        return 0;
    }

    struct task* task = (struct task*)kmalloc(sizeof(struct task));
    uint8_t* stack = (uint8_t*)kmalloc(TASK_STACK_SIZE);

    if (task == 0 || stack == 0) {
        if (task != 0) {
            kfree(task);
        }
        if (stack != 0) {
            kfree(stack);
        }
        return 0;
    }

    memset(task, 0, sizeof(*task));
    task->stack = stack;
    task->entry = entry;
    task->state = TASK_READY;
    task->name = name;

    // The frame sits at the very top of the stack, aligned down to 16 bytes.
    uint32_t top = (((uint32_t)stack + TASK_STACK_SIZE) & ~0xFu) - 4u;
    task->esp = build_frame((uint32_t*)(top - FRAME_DWORDS * 4));

    tasks[task_count] = task;
    task_count++;

    return task;
}

/* Build the tasks. The shell is first, so scheduler_start() enters it. */
void scheduler_init(void) {
    struct task* shell = task_create("shell", shell_main);
    struct task* idle = task_create("idle", idle_main);

    (void)shell;
    (void)idle;
}

/* Leave the boot context behind and become the first task. */
void scheduler_start(void) {
    if (task_count == 0) {
        for (;;) {
            __asm__ volatile("hlt");
        }
    }

    current_index = 0;
    current = tasks[0];
    current->state = TASK_RUNNING;

    context_switch(current->esp);
}

/* The next runnable task after "index", or -1. */
static int next_runnable(int index) {
    for (int step = 1; step <= MAX_TASKS; step++) {
        int i = (index + step) % MAX_TASKS;
        struct task* task = tasks[i];

        if (task != 0 &&
            (task->state == TASK_READY || task->state == TASK_RUNNING)) {
            return i;
        }
    }

    return -1;
}

/* Stop running "dying" for good and give the CPU to the next task. Its stack
 * is not freed here - this code still stands on it - but by the next tick. */
static void retire_and_switch(struct task* dying) {
    dying->state = TASK_DEAD;

    int index = next_runnable(current_index);

    if (index < 0) {
        kprintf("Scheduler: no task left to run\n");

        for (;;) {
            interrupt_wait();
        }
    }

    current_index = index;
    current = tasks[index];
    current->state = TASK_RUNNING;

    context_switch(current->esp);
}

/* Retire the running task after a fault and switch away; 0 when no task
 * faulted, which the caller has to treat as fatal. */
int scheduler_kill_current(struct interrupt_frame* frame) {
    (void)frame;

    if (current == 0) {
        return 0;
    }

    retire_and_switch(current);
}

/* Give a dead task's stack back, skipping the task running right now. */
static void reap_dead_tasks(void) {
    for (int i = 0; i < task_count; i++) {
        struct task* task = tasks[i];

        if (task != current && task->state == TASK_DEAD && task->stack != 0) {
            kfree(task->stack);
            task->stack = 0;
        }
    }
}

/* IRQ0: account for the tick, wake sleepers, ask for a switch. */
void scheduler_tick(struct interrupt_frame* frame) {
    (void)frame;

    if (current == 0) {
        return;
    }

    current->runtime_ticks++;

    uint32_t now = timer_ticks();

    for (int i = 0; i < task_count; i++) {
        struct task* task = tasks[i];

        if (task->state == TASK_SLEEPING &&
            (int32_t)(now - task->wake_tick) >= 0) {
            task->state = TASK_READY;
        }
    }

    reap_dead_tasks();

    need_resched = 1;
}

/* Did the timer ask for a switch? */
int scheduler_should_switch(void) { return need_resched != 0; }

/* Save the interrupted task and resume the next one. */
void scheduler_switch_from(struct interrupt_frame* frame) {
    need_resched = 0;

    if (current == 0) {
        return;
    }

    int index = next_runnable(current_index);

    // Only one runnable task: stay where we are.
    if (index < 0 || index == current_index) {
        return;
    }

    struct task* previous = current;
    previous->esp = (uint32_t)frame;

    if (previous->state == TASK_RUNNING) {
        previous->state = TASK_READY;
    }

    current_index = index;
    current = tasks[index];
    current->state = TASK_RUNNING;

    context_switch(current->esp);
}

/* Sleep without blocking the machine. */
void scheduler_sleep_ms(uint32_t milliseconds) {
    struct task* me = current;

    if (me == 0) {
        timer_sleep(milliseconds);
        return;
    }

    uint32_t ticks = (milliseconds / 1000) * 100 +
                     ((milliseconds % 1000) * 100 + 999) / 1000;

    me->wake_tick = timer_ticks() + ticks;
    me->state = TASK_SLEEPING;

    // The tick that reaches wake_tick makes the task READY again.
    while (me->state == TASK_SLEEPING) {
        interrupt_wait();
    }

    me->state = TASK_RUNNING;
}

/* Task i, for the "tasks" command. */
int scheduler_task_info(int index, struct task_info* info) {
    if (index < 0 || index >= task_count) {
        return 0;
    }

    info->name = tasks[index]->name;
    info->state = state_name(tasks[index]->state);
    info->runtime_ticks = tasks[index]->runtime_ticks;

    return 1;
}

/* How many tasks exist. */
int scheduler_task_count(void) { return task_count; }

/* How often the idle task was woken. */
uint32_t scheduler_idle_ticks(void) { return idle_wakes; }
