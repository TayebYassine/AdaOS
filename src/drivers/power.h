#ifndef ADAOS_POWER_H
#define ADAOS_POWER_H

// Leaving the machine: the port writes that ask a chip to cut the power, and
// the ones that reset the CPU. Each path tries several chipset variants and
// ends in a halt loop, because none of them returns.

/* Power the machine off through ACPI, falling back to other chipsets. */
void power_shutdown(void) __attribute__((noreturn));

/* Restart the machine, forcing a triple fault when the controller ignores us.
 */
void power_reboot(void) __attribute__((noreturn));

#endif /* ADAOS_POWER_H */
