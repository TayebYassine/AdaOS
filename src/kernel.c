#include <stdint.h>

#include "gdt.h"
#include "idt.h"
#include "interrupt.h"
#include "keyboard.h"
#include "pmm.h"
#include "screen.h"
#include "serial.h"
#include "shell.h"
#include "timer.h"

// Kernel entry point: brings up the CPU tables (GDT, IDT), the
// interrupt controller, the memory manager and the drivers, then hands
// over to the shell. Called once by boot.asm's _start.

/* Entry point of AdaOS. */
void kernel_main(uint32_t magic, uint32_t info_addr) {
    interrupt_disable();

    serial_init();

    screen_init();
    screen_clear();

    pmm_init(magic, info_addr);

    gdt_init();
    idt_init();
    interrupt_init();
    timer_init();
    keyboard_init();

    shell_run();
}
