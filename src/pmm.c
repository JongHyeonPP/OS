#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/* Simple stack-based physical page allocator */
#define MAX_PAGES 16384

static uint32_t page_stack[MAX_PAGES];
static uint32_t stack_top = 0;
static uint32_t managed_start = 0;
static uint32_t managed_end = 0;

static uint32_t pmm_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\npop %0\ncli" : "=r"(flags) : : "memory");
    return flags;
}

static void pmm_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0\npopfl" : : "r"(flags) : "memory");
}

void pmm_init(uint32_t mem_start, uint32_t mem_size) {
    uint32_t addr;
    uint32_t end_raw;
    uint32_t end;
    stack_top = 0;
    managed_start = 0;
    managed_end = 0;
    if (mem_start > 0xFFFFFFFFU - (PAGE_SIZE - 1)) return;
    addr = (mem_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    end_raw = mem_size > 0xFFFFFFFFU - mem_start ? 0xFFFFFFFFU : mem_start + mem_size;
    end = end_raw & ~(PAGE_SIZE - 1);
    managed_start = addr;
    while (addr < end && stack_top < MAX_PAGES) {
        page_stack[stack_top++] = addr;
        addr += PAGE_SIZE;
    }
    managed_end = addr;
}

void *pmm_alloc(void) {
    uint32_t flags = pmm_irq_save();
    void *page;
    if (stack_top == 0) {
        pmm_irq_restore(flags);
        return NULL;
    }
    page = (void *)page_stack[--stack_top];
    pmm_irq_restore(flags);
    return page;
}

void pmm_free(void *page) {
    uint32_t addr = (uint32_t)page;
    uint32_t i;
    uint32_t flags;
    if (!page || (addr & (PAGE_SIZE - 1)) != 0) return;
    flags = pmm_irq_save();
    if (addr < managed_start || addr >= managed_end) {
        pmm_irq_restore(flags);
        return;
    }
    for (i = 0; i < stack_top; i++) {
        if (page_stack[i] == addr) {
            pmm_irq_restore(flags);
            return;
        }
    }
    if (stack_top < MAX_PAGES)
        page_stack[stack_top++] = addr;
    pmm_irq_restore(flags);
}

uint32_t pmm_free_pages(void) {
    uint32_t flags = pmm_irq_save();
    uint32_t pages = stack_top;
    pmm_irq_restore(flags);
    return pages;
}

uint32_t pmm_managed_start(void) {
    return managed_start;
}

uint32_t pmm_managed_end(void) {
    return managed_end;
}

uint32_t pmm_managed_pages(void) {
    if (managed_end <= managed_start) return 0;
    return (managed_end - managed_start) / PAGE_SIZE;
}
