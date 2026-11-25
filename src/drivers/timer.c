#include "timer.h"

#include <stdint.h>

#include "clock.h"
#include "interrupt.h"
#include "io.h"
#include "scheduler.h"

/* Hz the PIT crystal runs at. */
#define PIT_INPUT_FREQUENCY 1193182
/* Ticks per second we ask for. */
#define TIMER_FREQUENCY 100

/* Command port: channel, access mode and operating mode. */
#define PIT_COMMAND_PORT 0x43
/* Channel 0 data port: receives the countdown. */
#define PIT_CHANNEL0_DATA_PORT 0x40

// Command byte fields, OR-ed together: channel 0, low byte then high byte,
// square wave generator, binary counting.
#define PIT_COMMAND_CHANNEL0 0x00          // channel 0
#define PIT_COMMAND_LOHIBYTE 0x30          // write low byte, then high byte
#define PIT_COMMAND_MODE_SQUARE_WAVE 0x06  // mode 3: square wave
#define PIT_COMMAND_BINARY 0x00            // count in binary, not BCD

/* Only IRQ0 writes this, so it is volatile: the compiler must not cache it. */
static volatile uint32_t tick_count = 0;

/* IRQ0: count one tick and let the scheduler look at the tasks. */
static void timer_irq(struct interrupt_frame* frame) {
    tick_count++;

    if (tick_count % TIMER_FREQUENCY == 0) {
        clock_tick();
    }

    scheduler_tick(frame);
}

/* Program the PIT for 100 ticks per second and hook it to IRQ0. */
void timer_init(void) {
    // 1193182 / 100 = 11931 pulses: one tick about every 10 milliseconds.
    uint16_t divisor = (uint16_t)(PIT_INPUT_FREQUENCY / TIMER_FREQUENCY);

    outb(PIT_COMMAND_PORT, PIT_COMMAND_CHANNEL0 | PIT_COMMAND_LOHIBYTE |
                               PIT_COMMAND_MODE_SQUARE_WAVE |
                               PIT_COMMAND_BINARY);

    // The divisor is 16 bits but the port takes a byte at a time.
    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA_PORT, (uint8_t)((divisor >> 8) & 0xFF));

    // From now on every tick arrives as interrupt 0x20.
    interrupt_register_irq(0, timer_irq);
}

/* How many ticks have gone by since the kernel started. */
uint32_t timer_ticks(void) { return tick_count; }

/* Whole seconds of uptime since the kernel started. */
uint32_t timer_seconds(void) { return tick_count / TIMER_FREQUENCY; }

/* Idle the CPU until the given number of milliseconds has passed. */
void timer_sleep(uint32_t milliseconds) {
    uint32_t ticks = (milliseconds / 1000) * TIMER_FREQUENCY +
                     ((milliseconds % 1000) * TIMER_FREQUENCY + 999) / 1000;

    uint32_t target = tick_count + ticks;

    // Unsigned subtraction read as signed still works when the counter wraps.
    while ((int32_t)(tick_count - target) < 0) {
        interrupt_wait();
    }
}
