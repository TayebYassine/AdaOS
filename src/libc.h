#ifndef ADAOS_LIBC_H
#define ADAOS_LIBC_H

#include <stddef.h> /* size_t */

/* Length of a string, not counting the ending '\0'. */
size_t strlen(const char* s);

/* Compare two strings: < 0, 0 or > 0. */
int strcmp(const char* a, const char* b);

/* Compare at most n characters of two strings. */
int strncmp(const char* a, const char* b, size_t n);

/* Copy n bytes from src to dst. Returns dst. */
void* memcpy(void* dst, const void* src, size_t n);

/* Fill n bytes of dst with the byte value. Returns dst. */
void* memset(void* dst, int value, size_t n);

#endif /* ADAOS_LIBC_H */
