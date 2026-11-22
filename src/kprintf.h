#ifndef ADAOS_KPRINTF_H
#define ADAOS_KPRINTF_H

/*
 * Kernel printf: like printf, but writes to the screen. Supports the
 * format specifiers %s (string), %c (character), %d (decimal),
 * %x (hexadecimal) and %% (a literal '%').
 */
void kprintf(const char* fmt, ...);

#endif /* ADAOS_KPRINTF_H */
