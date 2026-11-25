#include "power.h"

#include <stdint.h>

#include "io.h"

// Soft-off is one 16-bit write to the ACPI power-management port, but the
// port differs between chipsets, so every known one is tried in turn.
static void acpi_power_off(void) {
    outw(0x604, 0x2000);   // QEMU / KVM
    outw(0xB004, 0x2000);  // Bochs / older QEMU
    outw(0x4004, 0x3400);  // VirtualBox
}

/* Power off the machine through the ACPI "S5" (soft off) state. */
void power_shutdown(void) {
    acpi_power_off();

    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* Port reads to wait for the keyboard controller, a few milliseconds at worst. */
#define KBC_TIMEOUT 100000

/* Restart the machine. */
void power_reboot(void) {
    // Wait for the keyboard controller's input buffer to empty before the
    // reset command, or the chip may ignore it. The wait is bounded: a
    // controller that never clears its buffer would otherwise hang the
    // machine here, with the reset below and the triple fault after it both
    // unreachable.
    for (uint32_t i = 0; i < KBC_TIMEOUT; i++) {
        if ((inb(0x64) & 0x02) == 0) {
            break;
        }
    }

    outb(0x64, 0xFE);  // pulse the controller's reset line

    // The controller ignored us: load an empty IDT and trigger an
    // interrupt, and the resulting triple fault resets the CPU.
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
