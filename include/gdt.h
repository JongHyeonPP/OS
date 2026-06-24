#ifndef GDT_H
#define GDT_H
#include <stdint.h>

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x1B
#define GDT_USER_DATA   0x23
#define GDT_TSS         0x28

void gdt_init(void);
void tss_set_kernel_stack(uint32_t esp0);
#endif
