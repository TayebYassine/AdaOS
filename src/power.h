#ifndef ADAOS_POWER_H
#define ADAOS_POWER_H

/* Power the machine off through ACPI, falling back to the chipsets QEMU,
 * Bochs and VirtualBox emulate. Never returns. */
void power_shutdown(void) __attribute__((noreturn));

/* Restart the machine; never returns. */
void power_reboot(void) __attribute__((noreturn));

#endif /* ADAOS_POWER_H */
