#ifndef PIC_H
#define PIC_H
#include <stdint.h>
void pic_init(void);
void pic_eoi(uint8_t irq);    /* send End Of Interrupt */
void pic_mask(uint8_t irq);   /* mask (disable) IRQ   */
void pic_unmask(uint8_t irq); /* unmask (enable) IRQ  */
#endif