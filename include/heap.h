#ifndef HEAP_H
#define HEAP_H
#include <stdint.h>
#include <stddef.h>

void  heap_init(void *start, size_t size);
void *kmalloc(size_t size);
void  kfree(void *ptr);
size_t heap_free_bytes(void);
size_t heap_used_bytes(void);
uint32_t heap_block_count(void);
uint32_t heap_free_block_count(void);
#endif
