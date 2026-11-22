#include "libc.h"

/*
 * The tiny C library of AdaOS.
 */

size_t strlen(const char* s) {
    size_t length = 0;

    while (s[length] != '\0') {
        length++;
    }

    return length;
}

int strcmp(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }

    return (unsigned char)*a - (unsigned char)*b;
}

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

void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }

    return dst;
}

void* memset(void* dst, int value, size_t n) {
    unsigned char* d = (unsigned char*)dst;

    for (size_t i = 0; i < n; i++) {
        d[i] = (unsigned char)value;
    }

    return dst;
}
