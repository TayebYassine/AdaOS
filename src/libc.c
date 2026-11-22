#include "libc.h"

// The tiny C library of AdaOS: the handful of string and memory
// routines the kernel needs. The kernel is built with -fno-builtin
// and provides these itself, because there is no libc behind it.

/* Length of a string, not counting the ending '\0'. */
size_t strlen(const char* s) {
    size_t length = 0;

    while (s[length] != '\0') {
        length++;
    }

    return length;
}

/* Compare two strings: < 0, 0 or > 0. */
int strcmp(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

/* Compare at most n characters of two strings. */
int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];

        if (ca != cb) {
            return ca - cb;
        }

        if (ca == '\0') {
            return 0;
        }
    }

    return 0;
}

/* Copy n bytes from src to dst. Returns dst. */
void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }

    return dst;
}

/* Fill n bytes of dst with the byte value. Returns dst. */
void* memset(void* dst, int value, size_t n) {
    unsigned char* d = (unsigned char*)dst;

    for (size_t i = 0; i < n; i++) {
        d[i] = (unsigned char)value;
    }

    return dst;
}
