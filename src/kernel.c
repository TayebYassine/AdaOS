#include "gdt.h"
#include "idt.h"
#include "interrupt.h"
#include "keyboard.h"
#include "screen.h"
#include "shell.h"
#include "timer.h"

// Kernel entry point: brings up the CPU tables (GDT, IDT), the
// interrupt controller and the drivers, then hands over to the shell.
// Called once by boot.asm's _start.

/* Entry point of AdaOS. */
void kernel_main(void) {
    interrupt_disable();

    screen_clear();

    gdt_init();
    idt_init();
    interrupt_init();
    timer_init();
    keyboard_init();

    shell_run();
}
