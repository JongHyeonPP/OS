#include "timer.h"
#include "task.h"
#include "idt.h"
#include "pic.h"
#include "io.h"
#include <stdint.h>

#define PIT_CH0   0x40
#define PIT_CMD   0x43
#define PIT_BASE  1193182

static volatile uint32_t ticks = 0;
static uint32_t timer_frequency = 100;

static void timer_irq(void) {
    ticks++;
    scheduler_tick();
    pic_eoi(0);
}

void timer_init(uint32_t hz) {
    uint32_t divisor;
    if (hz == 0) hz = 100;
    if (hz > PIT_BASE) hz = PIT_BASE;
    timer_frequency = hz;
    divisor = PIT_BASE / hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
    irq_install_handler(0, timer_irq);
    pic_unmask(0);
}

uint32_t timer_ticks(void) {
    return ticks;
}

uint32_t timer_hz(void) {
    return timer_frequency;
}

uint32_t timer_ms(void) {
    uint32_t hz = timer_frequency ? timer_frequency : 100U;
    uint32_t tick_count = timer_ticks();
    uint32_t whole = tick_count / hz;
    uint32_t rem = tick_count % hz;
    if (whole > 0xFFFFFFFFU / 1000U) return 0xFFFFFFFFU;
    return whole * 1000U + (rem * 1000U) / hz;
}

static int tick_reached(uint32_t now, uint32_t target) {
    return (int32_t)(now - target) >= 0;
}

uint32_t timer_ms_to_ticks(uint32_t ms) {
    uint32_t hz = timer_frequency ? timer_frequency : 100U;
    uint32_t whole = ms / 1000U;
    uint32_t rem = ms % 1000U;
    uint32_t tick_count;
    uint32_t extra;
    if (ms == 0) return 0;
    if (whole > 0xFFFFFFFFU / hz) return 0xFFFFFFFFU;
    tick_count = whole * hz;
    extra = rem ? ((rem * hz) + 999U) / 1000U : 0;
    if (tick_count > 0xFFFFFFFFU - extra) return 0xFFFFFFFFU;
    tick_count += extra;
    return tick_count ? tick_count : 1U;
}

void timer_sleep(uint32_t ms) {
    uint32_t target;
    uint32_t wait_ticks = timer_ms_to_ticks(ms);
    if (wait_ticks == 0) return;
    target = ticks + wait_ticks;
    while (!tick_reached(ticks, target))
        __asm__ volatile("hlt");
}
