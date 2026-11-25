#include "libc.h"

#include <stdint.h>

/* Every fourth byte of a word, and the high bit of each of those bytes. */
#define WORD_ONES 0x01010101u
#define WORD_HIGHS 0x80808080u

/* Length of a string, not counting the ending '\0'. Reads a word at a time:
 * the byte loops these replace are the kernel's hottest code (ATA buffers,
 * screen scrolls, every path lookup). */
size_t strlen(const char* s) {
    const char* p = s;

    // Walk up to a word boundary first, so the wide reads are aligned.
    while (((uint32_t)p & 3u) != 0) {
        if (*p == '\0') {
            return (size_t)(p - s);
        }
        p++;
    }

    // A zero byte in a word shows up as a high bit in (~word & (word-1)).
    for (;;) {
        uint32_t word = *(const uint32_t*)p;

        if (((word - WORD_ONES) & ~word & WORD_HIGHS) != 0) {
            break;
        }

        p += 4;
    }

    while (*p != '\0') {
        p++;
    }

    return (size_t)(p - s);
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

/* Copy n bytes from src to dst, a word at a time. Returns dst. */
void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    size_t words = n / 4;

    if (words != 0) {
        __asm__ volatile("rep movsl"
                         : "+D"(d), "+S"(s), "+c"(words)
                         :
                         : "memory");
    }

    for (size_t tail = n % 4; tail > 0; tail--) {
        *d++ = *s++;
    }

    return dst;
}

/* Copy n bytes even when the two areas overlap. Returns dst. */
void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    if (d < s) {
        return memcpy(dst, src, n);
    }

    if (d > s) {
        // Backwards, so a byte is read before it is overwritten.
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }

    return dst;
}

/* Compare n bytes: < 0, 0 or > 0. Whole words are compared at once until an
 * answer can only come from inside one of them. */
int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* pa = (const unsigned char*)a;
    const unsigned char* pb = (const unsigned char*)b;
    size_t at = 0;

    while (at + 4 <= n) {
        uint32_t wa;
        uint32_t wb;

        memcpy(&wa, pa + at, 4);
        memcpy(&wb, pb + at, 4);

        if (wa != wb) {
            break;
        }

        at += 4;
    }

    // Byte by byte from the first word that differed, or from the tail.
    for (; at < n; at++) {
        if (pa[at] != pb[at]) {
            return pa[at] - pb[at];
        }
    }

    return 0;
}

/* Fill n bytes of dst with the byte value, a word at a time. Returns dst. */
void* memset(void* dst, int value, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    size_t words = n / 4;

    if (words != 0) {
        uint32_t pattern = (uint32_t)(unsigned char)value * WORD_ONES;

        __asm__ volatile("rep stosl"
                         : "+D"(d), "+c"(words)
                         : "a"(pattern)
                         : "memory");
    }

    for (size_t tail = n % 4; tail > 0; tail--) {
        *d++ = (unsigned char)value;
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

/* Read a number from the start of a string, 0 when there is none. A value
 * outside the int range stops at INT32_MIN or INT32_MAX: building it in signed
 * arithmetic would overflow, which is undefined behaviour and gave garbage for
 * long runs of digits. */
int atoi(const char* s) {
    int sign = 1;
    uint32_t value = 0;
    uint32_t limit;

    while (*s == ' ' || *s == '\t') {
        s++;
    }

    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    limit = (sign < 0) ? 2147483648u : 2147483647u;

    while (*s >= '0' && *s <= '9') {
        uint32_t digit = (uint32_t)(*s - '0');

        if (value > (limit - digit) / 10u) {
            value = limit;
            break;
        }

        value = value * 10u + digit;
        s++;
    }

    if (sign < 0) {
        return value >= 2147483648u ? (-2147483647 - 1) : -(int)value;
    }

    return (int)value;
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
