#pragma once
#include "types.h"
void kputc(char c);
void kputs(const char *s);
void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list ap);
void panic(const char *fmt, ...) __attribute__((noreturn));
