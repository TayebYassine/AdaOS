#include "kprintf.h"

#include "screen.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

/* Print an unsigned integer in base 10 (no sign, no padding). */
static void print_uint(unsigned int value) {
    char digits[10]; // enough for "4294967295"
    int count = 0;

    do {
        digits[count] = (char)('0' + (value % 10));
        count++;
        value /= 10;
    } while (value != 0);

    while (count > 0) {
        count--;
        print_char(digits[count]);
    }
}

/* Print a signed integer in base 10. */
static void print_int(int value) {
    unsigned int magnitude = (unsigned int)value;

    if (value < 0) {
        print_char('-');
        magnitude = -magnitude; // safe even for INT_MIN: unsigned wraps
    }

    print_uint(magnitude);
}

/* Print an unsigned integer in lowercase hexadecimal. */
static void print_hex(unsigned int value) {
    const char* hex = "0123456789abcdef";
    char digits[8]; // enough for "ffffffff"
    int count = 0;

    do {
        digits[count] = hex[value & 0xF];
        count++;
        value >>= 4;
    } while (value != 0);

    while (count > 0) {
        count--;
        print_char(digits[count]);
    }
}

void kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (int i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            print_char(fmt[i]);
            continue;
        }

        i++; // skip the '%'

        switch (fmt[i]) {
        case 's': {
            const char* s = va_arg(args, const char*);
            print(s != NULL ? s : "(null)");
            break;
        }
        case 'c':
            print_char((char)va_arg(args, int));
            break;
        case 'd':
            print_int(va_arg(args, int));
            break;
        case 'x':
            print_hex(va_arg(args, unsigned int));
            break;
        case '%':
            print_char('%');
            break;
        case '\0':
            print_char('%');
            va_end(args);
            return;
        default:
            print_char('%');
            print_char(fmt[i]);
            break;
        }
    }

    va_end(args);
}
