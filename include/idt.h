#pragma once
#include "types.h"

typedef struct {
    u32 ds;
    u32 edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;
    u32 int_no, err_code;
    u32 eip, cs, eflags, useresp, ss;
} registers_t;

typedef void (*isr_handler_t)(registers_t *);

void idt_init(void);
void register_interrupt_handler(u8 n, isr_handler_t h);
