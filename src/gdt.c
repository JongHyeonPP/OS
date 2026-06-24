#include <stdint.h>
#include "gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

static struct gdt_entry gdt[6];
static struct gdt_ptr   gdtp;
static tss_entry_t      tss;

static void gdt_set_entry(int i, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t gran)
{
    gdt[i].base_low   = (base & 0xFFFF);
    gdt[i].base_mid   = (base >> 16) & 0xFF;
    gdt[i].base_high  = (base >> 24) & 0xFF;
    gdt[i].limit_low  = (limit & 0xFFFF);
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access      = access;
}

static void gdt_flush(uint32_t gdtp_addr) {
    __asm__ volatile(
        "lgdt (%0)\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        : : "r"(gdtp_addr) : "eax"
    );
}

static void tss_flush(void) {
    __asm__ volatile(
        "mov %0, %%ax\n"
        "ltr %%ax\n"
        : : "i"(GDT_TSS) : "eax"
    );
}

void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

void gdt_init(void) {
    uint32_t tss_base = (uint32_t)&tss;
    uint32_t tss_limit = sizeof(tss_entry_t) - 1;
    uint32_t i;

    gdtp.limit = (sizeof(struct gdt_entry) * 6) - 1;
    gdtp.base  = (uint32_t)&gdt;
    for (i = 0; i < sizeof(tss) / sizeof(uint32_t); i++)
        ((uint32_t *)&tss)[i] = 0;

    /* Entry 0: null descriptor */
    gdt_set_entry(0, 0, 0, 0, 0);

    /* Entry 1: kernel code segment — base=0, limit=4GB, ring 0, executable/readable */
    /* access: present(1) | DPL=0(00) | S=1 | executable(1) | conform(0) | readable(1) | accessed(0) = 0x9A */
    /* gran:   4K gran(1) | 32-bit(1) | 0 | AVL(0) | upper nibble of limit */
    gdt_set_entry(1, 0x00000000, 0xFFFFFFFF, 0x9A, 0xCF);

    /* Entry 2: kernel data segment — base=0, limit=4GB, ring 0, writable */
    /* access: present(1) | DPL=0(00) | S=1 | executable(0) | direction(0) | writable(1) | accessed(0) = 0x92 */
    gdt_set_entry(2, 0x00000000, 0xFFFFFFFF, 0x92, 0xCF);

    /* Entry 3/4: user code/data, ring 3 */
    gdt_set_entry(3, 0x00000000, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set_entry(4, 0x00000000, 0xFFFFFFFF, 0xF2, 0xCF);

    /* Entry 5: 32-bit available TSS */
    tss.ss0 = GDT_KERNEL_DATA;
    tss.esp0 = 0;
    tss.iomap_base = sizeof(tss_entry_t);
    gdt_set_entry(5, tss_base, tss_limit, 0x89, 0x40);

    gdt_flush((uint32_t)&gdtp);
    tss_flush();
}
