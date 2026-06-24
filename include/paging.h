#ifndef PAGING_H
#define PAGING_H
#include <stdint.h>

#define PAGE_PRESENT  0x001
#define PAGE_WRITE    0x002
#define PAGE_USER     0x004
#define PAGE_PWT      0x008  /* Write-Through */
#define PAGE_PCD      0x010  /* Cache Disable (MMIO) */
#define PAGE_SIZE     4096

void paging_init(void);
int paging_map(uint32_t virt, uint32_t phys, uint32_t flags);
int paging_map_range(uint32_t virt, uint32_t phys, uint32_t size, uint32_t flags);
uint32_t paging_get_phys(uint32_t virt);
uint32_t paging_get_flags(uint32_t virt);
uint32_t *paging_kernel_directory(void);
uint32_t *paging_current_directory(void);
uint32_t *paging_clone_kernel_directory(void);
uint32_t *paging_clone_user_directory(uint32_t *src);
void paging_switch_directory(uint32_t *dir);
int paging_map_in_directory(uint32_t *dir, uint32_t virt, uint32_t phys, uint32_t flags);
uint32_t paging_get_phys_in_directory(uint32_t *dir, uint32_t virt);
uint32_t paging_get_flags_in_directory(uint32_t *dir, uint32_t virt);
int paging_map_user_page(uint32_t *dir, uint32_t virt, uint32_t phys, uint32_t flags);
int paging_set_user_page_flags(uint32_t *dir, uint32_t virt, uint32_t flags);
int paging_unmap_user_page(uint32_t *dir, uint32_t virt);
void paging_destroy_user_directory(uint32_t *dir);
#endif
