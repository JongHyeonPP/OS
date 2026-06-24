#ifndef IDT_H
#define IDT_H
#include <stdint.h>
typedef void (*irq_handler_t)(void);
void idt_init(void);
void irq_install_handler(int irq, irq_handler_t handler);
uint32_t irq_count(uint32_t irq);
uint32_t exception_count(uint32_t vector);
uint32_t exception_last_vector(void);
uint32_t exception_last_error(void);
uint32_t exception_last_eip(void);
uint32_t exception_last_cs(void);
uint32_t exception_last_cr2(void);
#endif
