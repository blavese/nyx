#pragma once
#include "types.h"
void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
bool serial_has_input(void);
char serial_getc(void);
void serial_enable_irq(void);
bool serial_buffered(void);
int  serial_trygetc(void);
u32  serial_overruns(void);
u32  serial_isr_bytes(void);
u32  serial_read_bytes(void);
u32  serial_isr_calls(void);
