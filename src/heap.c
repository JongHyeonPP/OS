#include "heap.h"
#include <stdint.h>
#include <stddef.h>

/* Simple first-fit heap allocator */
typedef struct block {
    size_t         size;
    int            free;
    struct block  *next;
} block_t;

#define HEAP_ALIGN 8U
#define HEAP_MIN_SPLIT 16U
#define BLOCK_HDR ((sizeof(block_t) + (HEAP_ALIGN - 1U)) & ~(size_t)(HEAP_ALIGN - 1U))

static block_t *heap_head = NULL;

static uint32_t heap_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\npop %0\ncli" : "=r"(flags) : : "memory");
    return flags;
}

static void heap_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0\npopfl" : : "r"(flags) : "memory");
}

void heap_init(void *start, size_t size) {
    uintptr_t addr = (uintptr_t)start;
    uintptr_t aligned = (addr + (HEAP_ALIGN - 1U)) & ~(uintptr_t)(HEAP_ALIGN - 1U);
    size_t adjust;
    if (!start || aligned < addr) {
        heap_head = NULL;
        return;
    }
    adjust = (size_t)(aligned - addr);
    if (size <= adjust || size - adjust <= BLOCK_HDR) {
        heap_head = NULL;
        return;
    }
    heap_head = (block_t *)aligned;
    heap_head->size = size - adjust - BLOCK_HDR;
    heap_head->free = 1;
    heap_head->next = NULL;
}

static void split_block(block_t *b, size_t size) {
    if (b->size > size && b->size - size > BLOCK_HDR + HEAP_MIN_SPLIT) {
        block_t *newb = (block_t *)((uint8_t *)b + BLOCK_HDR + size);
        newb->size = b->size - size - BLOCK_HDR;
        newb->free = 1;
        newb->next = b->next;
        b->next = newb;
        b->size = size;
    }
}

static int block_in_heap(block_t *target) {
    block_t *cur = heap_head;
    while (cur) {
        if (cur == target) return 1;
        cur = cur->next;
    }
    return 0;
}

void *kmalloc(size_t size) {
    uint32_t flags;
    if (size == 0) return NULL;
    if (size > (size_t)-1 - (HEAP_ALIGN - 1U)) return NULL;
    size = (size + (HEAP_ALIGN - 1U)) & ~(size_t)(HEAP_ALIGN - 1U);
    flags = heap_irq_save();
    block_t *b = heap_head;
    while (b) {
        if (b->free && b->size >= size) {
            split_block(b, size);
            b->free = 0;
            {
                void *ptr = (void *)((uint8_t *)b + BLOCK_HDR);
                heap_irq_restore(flags);
                return ptr;
            }
        }
        b = b->next;
    }
    heap_irq_restore(flags);
    return NULL;
}

void kfree(void *ptr) {
    block_t *b;
    block_t *cur;
    uint32_t flags;
    uintptr_t addr;
    if (!ptr) return;
    addr = (uintptr_t)ptr;
    if (addr < BLOCK_HDR) return;
    flags = heap_irq_save();
    b = (block_t *)(addr - BLOCK_HDR);
    if (!block_in_heap(b) || b->free) {
        heap_irq_restore(flags);
        return;
    }
    b->free = 1;
    /* coalesce adjacent free blocks */
    cur = heap_head;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += BLOCK_HDR + cur->next->size;
            cur->next  = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
    heap_irq_restore(flags);
}

size_t heap_free_bytes(void) {
    size_t total = 0;
    uint32_t flags = heap_irq_save();
    block_t *b = heap_head;
    while (b) {
        if (b->free) total += b->size;
        b = b->next;
    }
    heap_irq_restore(flags);
    return total;
}

size_t heap_used_bytes(void) {
    size_t total = 0;
    uint32_t flags = heap_irq_save();
    block_t *b = heap_head;
    while (b) {
        if (!b->free) total += b->size;
        b = b->next;
    }
    heap_irq_restore(flags);
    return total;
}

uint32_t heap_block_count(void) {
    uint32_t total = 0;
    uint32_t flags = heap_irq_save();
    block_t *b = heap_head;
    while (b) {
        total++;
        b = b->next;
    }
    heap_irq_restore(flags);
    return total;
}

uint32_t heap_free_block_count(void) {
    uint32_t total = 0;
    uint32_t flags = heap_irq_save();
    block_t *b = heap_head;
    while (b) {
        if (b->free) total++;
        b = b->next;
    }
    heap_irq_restore(flags);
    return total;
}
