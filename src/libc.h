#ifndef ADAOS_LIBC_H
#define ADAOS_LIBC_H

#include <stddef.h>
#include <stdint.h>

/* Length of a string, not counting the ending '\0'. */
size_t strlen(const char* s);

/* Compare two strings: < 0, 0 or > 0. */
int strcmp(const char* a, const char* b);

/* Compare at most n characters of two strings. */
int strncmp(const char* a, const char* b, size_t n);

/* Copy src (with its '\0') into dst. Returns dst. */
char* strcpy(char* dst, const char* src);

/* Copy at most n characters and always terminate dst. Returns dst. */
char* strncpy(char* dst, const char* src, size_t n);

/* Append src to the end of dst. Returns dst. */
char* strcat(char* dst, const char* src);

/* Append at most n characters of src and always terminate dst. Returns dst. */
char* strncat(char* dst, const char* src, size_t n);

/* First place c appears in s, or 0. */
char* strchr(const char* s, int c);

/* Last place c appears in s, or 0. */
char* strrchr(const char* s, int c);

/* First place needle appears in haystack, or 0. */
char* strstr(const char* haystack, const char* needle);

/* Copy n bytes from src to dst. Returns dst. */
void* memcpy(void* dst, const void* src, size_t n);

/* Copy n bytes even when the two areas overlap. Returns dst. */
void* memmove(void* dst, const void* src, size_t n);

/* Compare n bytes: < 0, 0 or > 0. */
int memcmp(const void* a, const void* b, size_t n);

/* Fill n bytes of dst with the byte value. Returns dst. */
void* memset(void* dst, int value, size_t n);

/* Uppercase and lowercase versions of a character. */
int toupper(int c);
int tolower(int c);

/* Read a number from the start of a string, 0 when there is none. */
int atoi(const char* s);

/* Write value as decimal text into buf, which needs 12 bytes. Returns buf. */
char* utoa(uint32_t value, char* buf);
char* itoa(int value, char* buf);

#endif /* ADAOS_LIBC_H */
