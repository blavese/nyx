/* Programmable Interval Timer on IRQ0. This is the heartbeat the scheduler
   preempts on. */
#include "timer.h"
#include "idt.h"
#include "pic.h"
#include "io.h"

static volatile u64 ticks = 0;
static u32 frequency = 100;

static void on_tick(registers_t *r) {
    (void)r;
    ticks++;
}

void timer_init(u32 hz) {
    frequency = hz;
    u32 divisor = 1193182u / hz;
    outb(0x43, 0x36);                       /* channel 0, lo/hi, square wave */
    outb(0x40, (u8)(divisor & 0xFF));
    outb(0x40, (u8)((divisor >> 8) & 0xFF));
    register_interrupt_handler(32, on_tick);
    pic_unmask(0);
}

u64 timer_ticks(void) { return ticks; }
u32 timer_hz(void)    { return frequency; }

void sleep_ms(u32 ms) {
    u32 delta = (ms * frequency) / 1000u;
    u64 target = ticks + delta;
    while (ticks < target) __asm__ volatile ("hlt");
}
