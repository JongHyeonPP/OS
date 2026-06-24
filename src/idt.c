#include <stdint.h>
#include "idt.h"
#include "debug.h"
#include "pic.h"
#include "process.h"
#include "task.h"

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr   idtp;

static irq_handler_t irq_handlers[16];
static uint32_t irq_counts[16];
static uint32_t exception_counts[32];
static uint32_t exception_last_vec;
static uint32_t exception_last_err;
static uint32_t exception_last_ip;
static uint32_t exception_last_seg;
static uint32_t exception_last_fault_addr;

/* Forward declarations for IRQ stubs defined in idt_stubs.S */
extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq2_stub(void);
extern void irq3_stub(void);
extern void irq4_stub(void);
extern void irq5_stub(void);
extern void irq6_stub(void);
extern void irq7_stub(void);
extern void irq8_stub(void);
extern void irq9_stub(void);
extern void irq10_stub(void);
extern void irq11_stub(void);
extern void irq12_stub(void);
extern void irq13_stub(void);
extern void irq14_stub(void);
extern void irq15_stub(void);

/* Defined in idt_stubs.S — halts on unexpected exception */
extern void exc_halt_stub(void);
extern void exc0_stub(void);
extern void exc1_stub(void);
extern void exc2_stub(void);
extern void exc3_stub(void);
extern void exc4_stub(void);
extern void exc5_stub(void);
extern void exc6_stub(void);
extern void exc7_stub(void);
extern void exc8_stub(void);
extern void exc9_stub(void);
extern void exc10_stub(void);
extern void exc11_stub(void);
extern void exc12_stub(void);
extern void exc13_stub(void);
extern void exc14_stub(void);
extern void exc15_stub(void);
extern void exc16_stub(void);
extern void exc17_stub(void);
extern void exc18_stub(void);
extern void exc19_stub(void);
extern void exc20_stub(void);
extern void exc21_stub(void);
extern void exc22_stub(void);
extern void exc23_stub(void);
extern void exc24_stub(void);
extern void exc25_stub(void);
extern void exc26_stub(void);
extern void exc27_stub(void);
extern void exc28_stub(void);
extern void exc29_stub(void);
extern void exc30_stub(void);
extern void exc31_stub(void);

static void (*exception_stubs[32])(void) = {
    exc0_stub,  exc1_stub,  exc2_stub,  exc3_stub,
    exc4_stub,  exc5_stub,  exc6_stub,  exc7_stub,
    exc8_stub,  exc9_stub,  exc10_stub, exc11_stub,
    exc12_stub, exc13_stub, exc14_stub, exc15_stub,
    exc16_stub, exc17_stub, exc18_stub, exc19_stub,
    exc20_stub, exc21_stub, exc22_stub, exc23_stub,
    exc24_stub, exc25_stub, exc26_stub, exc27_stub,
    exc28_stub, exc29_stub, exc30_stub, exc31_stub
};

static void idt_set_gate(uint8_t num, uint32_t base) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel       = 0x08;
    idt[num].zero      = 0;
    idt[num].flags     = 0x8E;
}

void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

uint32_t irq_count(uint32_t irq) {
    if (irq >= 16) return 0;
    return irq_counts[irq];
}

uint32_t exception_count(uint32_t vector) {
    if (vector >= 32) return 0;
    return exception_counts[vector];
}

uint32_t exception_last_vector(void) {
    return exception_last_vec;
}

uint32_t exception_last_error(void) {
    return exception_last_err;
}

uint32_t exception_last_eip(void) {
    return exception_last_ip;
}

uint32_t exception_last_cs(void) {
    return exception_last_seg;
}

uint32_t exception_last_cr2(void) {
    return exception_last_fault_addr;
}

/* Called from IRQ stubs — handler sends EOI, or we do if none registered */
void __attribute__((cdecl)) irq_handler_c(int irq_num) {
    if (irq_num >= 0 && irq_num < 16) {
        irq_counts[irq_num]++;
        if (irq_handlers[irq_num])
            irq_handlers[irq_num]();
        else
            pic_eoi((uint8_t)irq_num);
    }
}

void __attribute__((cdecl)) exception_handler_c(uint32_t vector,
                                                uint32_t error,
                                                uint32_t eip,
                                                uint32_t cs) {
    uint32_t cr2 = 0;
    if (vector < 32) exception_counts[vector]++;
    if (vector == 14) __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    exception_last_vec = vector;
    exception_last_err = error;
    exception_last_ip = eip;
    exception_last_seg = cs;
    exception_last_fault_addr = cr2;
    debug_puts("[exception] vector=");
    debug_dec(vector);
    debug_puts(" error=");
    debug_hex32(error);
    debug_puts(" eip=");
    debug_hex32(eip);
    debug_puts(" cs=");
    debug_hex32(cs);
    if (vector == 14) {
        debug_puts(" cr2=");
        debug_hex32(cr2);
    }
    debug_puts("\n");
    if ((cs & 3U) == 3U) {
        process_t *proc = process_current();
        debug_puts("[exception] killing user process pid=");
        debug_dec(proc ? proc->pid : 0);
        debug_puts("\n");
        process_exit(-(int)vector);
        task_exit();
    }
    panic("cpu exception");
}

void idt_init(void) {
    int i;

    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base  = (uint32_t)&idt;
    for (i = 0; i < 16; i++) {
        irq_handlers[i] = 0;
        irq_counts[i] = 0;
    }
    for (i = 0; i < 32; i++) exception_counts[i] = 0;
    exception_last_vec = 0;
    exception_last_err = 0;
    exception_last_ip = 0;
    exception_last_seg = 0;
    exception_last_fault_addr = 0;

    /* Exceptions 0-31: vector-specific handlers with diagnostic logging */
    for (i = 0; i < 0x20; i++) {
        idt_set_gate((uint8_t)i, (uint32_t)exception_stubs[i]);
    }
    /* IRQ range and beyond: no-op (stubs will be installed below) */
    for (i = 0x20; i < 256; i++) {
        idt_set_gate((uint8_t)i, (uint32_t)exc_halt_stub);
    }

    /* Initialise PIC before installing IRQ stubs */
    pic_init();

    /* Install IRQ stubs at IDT entries 0x20-0x2F */
    idt_set_gate(0x20, (uint32_t)irq0_stub);
    idt_set_gate(0x21, (uint32_t)irq1_stub);
    idt_set_gate(0x22, (uint32_t)irq2_stub);
    idt_set_gate(0x23, (uint32_t)irq3_stub);
    idt_set_gate(0x24, (uint32_t)irq4_stub);
    idt_set_gate(0x25, (uint32_t)irq5_stub);
    idt_set_gate(0x26, (uint32_t)irq6_stub);
    idt_set_gate(0x27, (uint32_t)irq7_stub);
    idt_set_gate(0x28, (uint32_t)irq8_stub);
    idt_set_gate(0x29, (uint32_t)irq9_stub);
    idt_set_gate(0x2A, (uint32_t)irq10_stub);
    idt_set_gate(0x2B, (uint32_t)irq11_stub);
    idt_set_gate(0x2C, (uint32_t)irq12_stub);
    idt_set_gate(0x2D, (uint32_t)irq13_stub);
    idt_set_gate(0x2E, (uint32_t)irq14_stub);
    idt_set_gate(0x2F, (uint32_t)irq15_stub);

    __asm__ volatile("lidt (%0)" : : "r"(&idtp));
}
