#pragma once
#include "types.h"
void kputc(char c);
void kputs(const char *s);
void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);

/* Formats into a buffer. Always terminates; returns the length it wanted. */
int  kformat(char *buf, size_t cap, const char *fmt, ...);
void panic(const char *fmt, ...) __attribute__((noreturn));
