#ifndef ADAOS_KPRINTF_H
#define ADAOS_KPRINTF_H

// Formatted printing: a small printf whose specifiers are %s, %c, %d, %x and
// %%. The text is built in a stack buffer and handed to the screen and the
// serial console a batch at a time, because every screen character write also
// programs the VGA CRT controller, and one update per batch is far cheaper
// than one per character.

/* Print "fmt" to the screen: %s, %c, %d, %x and %%. */
void kprintf(const char* fmt, ...);

#endif /* ADAOS_KPRINTF_H */
