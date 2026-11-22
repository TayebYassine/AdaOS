#include "kprintf.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "screen.h"
#include "serial.h"

// kprintf: printf for the kernel. Writes straight to the screen
// through screen.c and, when COM1 is up, to the serial console too -
// so the same messages can be watched on the screen or captured to a
// file. It understands a small set of format specifiers (%s, %c, %d,
// %x, %%) - enough for messages that contain numbers.

/* Send one character to the screen and to the serial console. */
static void kputc(char c) {
    print_char(c);
    serial_write_char(c);
}

/* Send a string to the screen and to the serial console. */
static void kputs(const char* s) {
    print(s);
    serial_write(s);
}

/* Print an unsigned integer in base 10 (no sign, no padding). */
static void print_uint(unsigned int value) {
    char digits[10];  // enough for "4294967295"
    int count = 0;

    do {
        digits[count] = (char)('0' + (value % 10));
        count++;
        value /= 10;
    } while (value != 0);

    while (count > 0) {
        count--;
        kputc(digits[count]);
    }
}

/* Print a signed integer in base 10. */
static void print_int(int value) {
    unsigned int magnitude = (unsigned int)value;

    if (value < 0) {
        kputc('-');
        magnitude = -magnitude;  // safe even for INT_MIN: unsigned wraps
    }

    print_uint(magnitude);
}

/* Print an unsigned integer in lowercase hexadecimal. */
static void print_hex(unsigned int value) {
    const char* hex = "0123456789abcdef";
    char digits[8];  // enough for "ffffffff"
    int count = 0;

    do {
        digits[count] = hex[value & 0xF];
        count++;
        value >>= 4;
    } while (value != 0);

    while (count > 0) {
        count--;
        kputc(digits[count]);
    }
}

/* Print fmt, substituting the varargs at each '%' specifier. */
void kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (int i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            kputc(fmt[i]);
            continue;
        }

        i++;  // skip the '%'

        switch (fmt[i]) {
            case 's': {
                const char* s = va_arg(args, const char*);
                kputs(s != NULL ? s : "(null)");
                break;
            }
            case 'c': {
                kputc((char)va_arg(args, int));
                break;
            }
            case 'd': {
                print_int(va_arg(args, int));
                break;
            }
            case 'x': {
                print_hex(va_arg(args, unsigned int));
                break;
            }
            case '%': {
                kputc('%');
                break;
            }
            case '\0': {
                kputc('%');
                va_end(args);
                return;
            }
            default: {
                kputc('%');
                kputc(fmt[i]);
                break;
            }
        }
    }

    va_end(args);
}
