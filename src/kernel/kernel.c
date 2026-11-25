#include <stdint.h>

#include "clock.h"
#include "diskfs.h"
#include "fs.h"
#include "gdt.h"
#include "idt.h"
#include "interrupt.h"
#include "keyboard.h"
#include "kmalloc.h"
#include "paging.h"
#include "pmm.h"
#include "scheduler.h"
#include "screen.h"
#include "serial.h"
#include "setup.h"
#include "shell.h"
#include "timer.h"

// Kernel entry point: bring up the CPU tables and the interrupt controller,
// then the memory layers (frames, paging, heap), the file system and its disk,
// the drivers, the clock and the scheduler, which ends up in the shell.

/* Entry point of the kernel; called once by boot.asm's _start. */
void kernel_main(uint32_t magic, uint32_t info_addr) {
    interrupt_disable();

    serial_init();

    screen_init();
    screen_clear();

    pmm_init(magic, info_addr);
    paging_init();
    kmalloc_init();

    gdt_init();
    idt_init();
    interrupt_init();
    clock_init();
    timer_init();
    keyboard_init();

    fs_init();
    fs_load_module(magic, info_addr);

    setup_run_if_needed();

    diskfs_init();

    scheduler_init();

    scheduler_start();
}
