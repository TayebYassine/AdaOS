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

/* Copy src (with its '\0') into dst. Returns dst. */
char* strcpy(char* dst, const char* src) {
    char* out = dst;

    while (*src != '\0') {
        *out++ = *src++;
    }

    *out = '\0';
    return dst;
}

/* Copy at most n characters and always terminate dst. Returns dst. */
char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;

    for (; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }

    if (i < n) {
        dst[i] = '\0';
    }

    return dst;
}

/* Append src to the end of dst. Returns dst. */
char* strcat(char* dst, const char* src) {
    strcpy(dst + strlen(dst), src);
    return dst;
}

/* Append at most n characters of src and always terminate dst. Returns dst. */
char* strncat(char* dst, const char* src, size_t n) {
    char* out = dst + strlen(dst);

    while (n > 0 && *src != '\0') {
        *out++ = *src++;
        n--;
    }

    *out = '\0';
    return dst;
}

/* First place c appears in s, or 0. */
char* strchr(const char* s, int c) {
    char wanted = (char)c;

    for (;; s++) {
        if (*s == wanted) {
            return (char*)s;
        }

        if (*s == '\0') {
            return 0;
        }
    }
}

/* Last place c appears in s, or 0. */
char* strrchr(const char* s, int c) {
    char wanted = (char)c;
    char* last = 0;

    for (;; s++) {
        if (*s == wanted) {
            last = (char*)s;
        }

        if (*s == '\0') {
            return last;
        }
    }
}

/* First place needle appears in haystack, or 0. */
char* strstr(const char* haystack, const char* needle) {
    size_t length = strlen(needle);

    if (length == 0) {
        return (char*)haystack;
    }

    for (; *haystack != '\0'; haystack++) {
        if (*haystack == *needle && strncmp(haystack, needle, length) == 0) {
            return (char*)haystack;
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

/* Copy n bytes even when the two areas overlap. Returns dst. */
void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }

    return dst;
}

/* Compare n bytes: < 0, 0 or > 0. */
int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* pa = (const unsigned char*)a;
    const unsigned char* pb = (const unsigned char*)b;

    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) {
            return pa[i] - pb[i];
        }
    }

    return 0;
}

/* Fill n bytes of dst with the byte value. Returns dst. */
void* memset(void* dst, int value, size_t n) {
    unsigned char* d = (unsigned char*)dst;

    for (size_t i = 0; i < n; i++) {
        d[i] = (unsigned char)value;
    }

    return dst;
}

/* Uppercase and lowercase versions of a character. */
int toupper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 'A';
    }

    return c;
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 'a';
    }

    return c;
}

/* Read a number from the start of a string, 0 when there is none. */
int atoi(const char* s) {
    int sign = 1;
    int value = 0;

    while (*s == ' ' || *s == '\t') {
        s++;
    }

    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
    }

    return value * sign;
}

/* Write value as decimal text into buf, which needs 12 bytes. Returns buf. */
char* utoa(uint32_t value, char* buf) {
    char digits[11];
    size_t count = 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }

    while (value > 0) {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    }

    for (size_t i = 0; i < count; i++) {
        buf[i] = digits[count - 1 - i];
    }

    buf[count] = '\0';
    return buf;
}

char* itoa(int value, char* buf) {
    if (value < 0) {
        buf[0] = '-';
        utoa((uint32_t)(-(int64_t)value), buf + 1);
        return buf;
    }

    return utoa((uint32_t)value, buf);
}
