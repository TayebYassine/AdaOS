#include "kprintf.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "screen.h"
#include "serial.h"

/* Characters collected before they are shown. */
#define KPRINTF_BATCH 192

/* One kprintf's output on its way out; on the stack, so an interrupt
 * cannot trample the message the shell is printing. */
struct output {
    char text[KPRINTF_BATCH];
    int length;
};

/* Show what has been collected so far and start again. */
static void flush(struct output* out) {
    if (out->length == 0) {
        return;
    }

    out->text[out->length] = '\0';
    print(out->text);
    serial_write(out->text);
    out->length = 0;
}

/* Collect one character, showing the batch when it is full. */
static void put(struct output* out, char c) {
    if (out->length == KPRINTF_BATCH - 1) {
        flush(out);
    }

    out->text[out->length] = c;
    out->length++;
}

/* Collect a string. */
static void puts(struct output* out, const char* s) {
    for (int i = 0; s[i] != '\0'; i++) {
        put(out, s[i]);
    }
}

/* Collect an unsigned integer in base 10 (no sign, no padding). */
static void print_uint(struct output* out, unsigned int value) {
    char digits[10];  // enough for "4294967295"
    int count = 0;

    do {
        digits[count] = (char)('0' + (value % 10));
        count++;
        value /= 10;
    } while (value != 0);

    while (count > 0) {
        count--;
        put(out, digits[count]);
    }
}

/* Collect a signed integer in base 10. */
static void print_int(struct output* out, int value) {
    unsigned int magnitude = (unsigned int)value;

    if (value < 0) {
        put(out, '-');
        magnitude = -magnitude;  // safe even for INT_MIN: unsigned wraps
    }

    print_uint(out, magnitude);
}

/* Collect an unsigned integer in lowercase hexadecimal. */
static void print_hex(struct output* out, unsigned int value) {
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
        put(out, digits[count]);
    }
}

/* Print fmt, substituting the varargs at each '%' specifier. */
void kprintf(const char* fmt, ...) {
    struct output out;
    va_list args;

    out.length = 0;
    va_start(args, fmt);

    for (int i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            put(&out, fmt[i]);
            continue;
        }

        i++;  // skip the '%'

        switch (fmt[i]) {
            case 's': {
                const char* s = va_arg(args, const char*);
                puts(&out, s != NULL ? s : "(null)");
                break;
            }
            case 'c': {
                put(&out, (char)va_arg(args, int));
                break;
            }
            case 'd': {
                print_int(&out, va_arg(args, int));
                break;
            }
            case 'x': {
                print_hex(&out, va_arg(args, unsigned int));
                break;
            }
            case '%': {
                put(&out, '%');
                break;
            }
            case '\0': {
                // A '%' at the very end: print it and stop.
                put(&out, '%');
                flush(&out);
                va_end(args);
                return;
            }
            default: {
                put(&out, '%');
                put(&out, fmt[i]);
                break;
            }
        }
    }

    flush(&out);
    va_end(args);
}
