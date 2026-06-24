#ifndef TIMER_H
#define TIMER_H
#include <stdint.h>

void timer_init(uint32_t hz);
uint32_t timer_ticks(void);
uint32_t timer_hz(void);
uint32_t timer_ms(void);
uint32_t timer_ms_to_ticks(uint32_t ms);
void timer_sleep(uint32_t ms);
#endif
