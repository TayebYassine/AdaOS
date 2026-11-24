#include "power.h"

#include <stdint.h>

#include "io.h"

// Leaving the machine: powering it off, or starting it over. Both are
// done by talking to a chip, and neither ever comes back, so they live
// here instead of in the shell command that asks for them - the setup
// program needs exactly the same two things.

/* Power off the machine through the ACPI "S5" (soft off) state. */
void power_shutdown(void) {
    outw(0x604, 0x2000);   // QEMU / KVM
    outw(0xB004, 0x2000);  // Bochs / older QEMU
    outw(0x4004, 0x3400);  // VirtualBox

    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* Restart the machine. */
void power_reboot(void) {
    while ((inb(0x64) & 0x02) != 0) {
    }

    outb(0x64, 0xFE);

    // The controller ignored us: force a triple fault instead.
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) empty_idt = {0, 0};

    __asm__ volatile("lidt %0" : : "m"(empty_idt));
    __asm__ volatile("int $0");

    for (;;) {
        __asm__ volatile("hlt");
    }
}
