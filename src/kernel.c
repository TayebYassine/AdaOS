#include "screen.h"

/* Entry point of AdaOS */
void kernel_main(void) {
    screen_clear();

    print("Hello from AdaOS!\n");
    print("This is the first test.\n");

    // Halt the CPU in a loop instead of just returning.
    // The loop simply keeps the CPU idle and low-power until the
    // user powers off the machine.
    for (;;) {
        __asm__ volatile("hlt");
    }
}
