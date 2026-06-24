#include <stdint.h>
#include "pic.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define PIC_EOI   0x20

/* ICW1: initialise + ICW4 needed */
#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
/* ICW4: 8086 mode */
#define ICW4_8086 0x01

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Short I/O delay — write to an unused port */
static inline void io_wait(void) {
    outb(0x80, 0);
}

void pic_init(void) {
    /* ICW1: start initialisation sequence (cascade mode) */
    outb(PIC1_CMD,  ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD,  ICW1_INIT | ICW1_ICW4);
    io_wait();

    /* ICW2: vector offsets */
    outb(PIC1_DATA, 0x20);   /* PIC1 maps IRQ 0-7  -> INT 0x20-0x27 */
    io_wait();
    outb(PIC2_DATA, 0x28);   /* PIC2 maps IRQ 8-15 -> INT 0x28-0x2F */
    io_wait();

    /* ICW3: cascade wiring */
    outb(PIC1_DATA, 0x04);   /* PIC1: slave on IRQ2 (bit mask) */
    io_wait();
    outb(PIC2_DATA, 0x02);   /* PIC2: cascade identity (IRQ2)  */
    io_wait();

    /* ICW4: 8086 mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Mask all IRQs initially */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    /* Unmask IRQ2 so the cascade line stays open */
    pic_unmask(2);
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

void pic_mask(uint8_t irq) {
    uint16_t port;
    uint8_t  mask;

    if (irq >= 16) return;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    mask = inb(port) | (uint8_t)(1 << irq);
    outb(port, mask);
}

void pic_unmask(uint8_t irq) {
    uint16_t port;
    uint8_t  mask;

    if (irq >= 16) return;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    mask = inb(port) & (uint8_t)~(1 << irq);
    outb(port, mask);
}
