#ifndef PMM_H
#define PMM_H
#include <stdint.h>

#define PAGE_SIZE 4096

void  pmm_init(uint32_t mem_start, uint32_t mem_size);
void *pmm_alloc(void);
void  pmm_free(void *page);
uint32_t pmm_free_pages(void);
uint32_t pmm_managed_start(void);
uint32_t pmm_managed_end(void);
uint32_t pmm_managed_pages(void);
#endif
