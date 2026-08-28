#include "string.h"

/* The volatile pointers here are load-bearing. Without them the compiler
   recognises each of these loops as the very function it is compiling and
   replaces the body with a call to itself, which recurses until the kernel
   stack is gone. */
void *memset(void *d, int c, size_t n) {
    volatile u8 *p = d;
    while (n--) *p++ = (u8)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t n) {
    volatile u8 *dp = d;
    const volatile u8 *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    volatile u8 *dp = d;
    const volatile u8 *sp = s;
    if (dp < sp) { while (n--) *dp++ = *sp++; }
    else { dp += n; sp += n; while (n--) *--dp = *--sp; }
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const u8 *x = a, *y = b;
    while (n--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (u8)*a - (u8)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (u8)*a - (u8)*b : 0;
}
char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++)) { }
    return r;
}
char *strncpy(char *d, const char *s, size_t n) {
    char *r = d;
    while (n && *s) { *d++ = *s++; n--; }
    while (n--) *d++ = 0;
    return r;
}
char *strchr(const char *s, int c) {
    for (; *s; s++) if (*s == (char)c) return (char *)s;
    return c ? 0 : (char *)s;
}
