#include "paging.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/* Single page directory covering 0-4GB identity mapped */
#define PAGE_DIR_SIZE  1024
#define PAGE_TBL_SIZE  1024
#define PAGING_USER_BASE 0x08000000U
#define PAGING_USER_TOP  0x80000000U
#define KERNEL_IDENTITY_TABLES 16

/* Statically allocated page directory and low identity tables (covers 0-64MB) */
static uint32_t page_dir[PAGE_DIR_SIZE]  __attribute__((aligned(4096)));
static uint32_t page_tbl[KERNEL_IDENTITY_TABLES][PAGE_TBL_SIZE] __attribute__((aligned(4096)));
static uint32_t *current_dir = page_dir;

static int user_page_ok(uint32_t virt) {
    return virt >= PAGING_USER_BASE && virt < PAGING_USER_TOP;
}

static void memset32(uint32_t *dst, uint32_t val, uint32_t count) {
    while (count--) *dst++ = val;
}

static void copy_page32(uint32_t *dst, const uint32_t *src) {
    uint32_t i;
    for (i = 0; i < PAGE_SIZE / sizeof(uint32_t); i++) dst[i] = src[i];
}

void paging_init(void) {
    int i, j;
    /* Clear page directory */
    memset32(page_dir, 0, PAGE_DIR_SIZE);

    /* Identity-map the low kernel/PMM-accessible physical range. */
    for (i = 0; i < KERNEL_IDENTITY_TABLES; i++) {
        for (j = 0; j < PAGE_TBL_SIZE; j++) {
            uint32_t phys = (uint32_t)(i * PAGE_TBL_SIZE + j) * PAGE_SIZE;
            page_tbl[i][j] = phys | PAGE_PRESENT | PAGE_WRITE;
        }
        page_dir[i] = (uint32_t)page_tbl[i] | PAGE_PRESENT | PAGE_WRITE;
    }

    /* Load page directory and enable paging */
    current_dir = page_dir;
    __asm__ volatile(
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r"(page_dir) : "eax"
    );
}

uint32_t *paging_kernel_directory(void) {
    return page_dir;
}

uint32_t *paging_current_directory(void) {
    return current_dir;
}

uint32_t *paging_clone_kernel_directory(void) {
    uint32_t *dir = (uint32_t *)pmm_alloc();
    uint32_t i;
    if (!dir) return 0;
    for (i = 0; i < PAGE_DIR_SIZE; i++) dir[i] = 0;
    for (i = 0; i < PAGE_DIR_SIZE; i++) {
        if (page_dir[i] & PAGE_PRESENT) dir[i] = page_dir[i] & ~PAGE_USER;
    }
    return dir;
}

uint32_t *paging_clone_user_directory(uint32_t *src) {
    uint32_t *dir;
    uint32_t dir_idx;
    if (!src) return 0;
    dir = paging_clone_kernel_directory();
    if (!dir) return 0;
    for (dir_idx = PAGING_USER_BASE >> 22; dir_idx < (PAGING_USER_TOP >> 22); dir_idx++) {
        uint32_t pde = src[dir_idx];
        uint32_t *src_tbl;
        uint32_t tbl_idx;
        if ((pde & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) continue;
        src_tbl = (uint32_t *)(pde & ~0xFFF);
        for (tbl_idx = 0; tbl_idx < PAGE_TBL_SIZE; tbl_idx++) {
            uint32_t pte = src_tbl[tbl_idx];
            uint32_t virt;
            void *phys;
            if ((pte & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) continue;
            virt = (dir_idx << 22) | (tbl_idx << 12);
            if (!user_page_ok(virt)) continue;
            phys = pmm_alloc();
            if (!phys) {
                paging_destroy_user_directory(dir);
                return 0;
            }
            copy_page32((uint32_t *)phys, (const uint32_t *)(pte & ~0xFFF));
            if (paging_map_user_page(dir, virt, (uint32_t)phys, pte & PAGE_WRITE) < 0) {
                pmm_free(phys);
                paging_destroy_user_directory(dir);
                return 0;
            }
        }
    }
    return dir;
}

void paging_switch_directory(uint32_t *dir) {
    if (!dir) return;
    current_dir = dir;
    __asm__ volatile("mov %0, %%cr3" : : "r"(dir) : "memory");
}

int paging_map_in_directory(uint32_t *dir, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_idx = virt >> 22;
    uint32_t tbl_idx = (virt >> 12) & 0x3FF;
    uint32_t *tbl;

    if (!dir) dir = current_dir;
    if (!(dir[dir_idx] & PAGE_PRESENT)) {
        uint32_t *new_tbl = (uint32_t *)pmm_alloc();
        uint32_t i;
        if (!new_tbl) return -1;
        for (i = 0; i < PAGE_TBL_SIZE; i++) new_tbl[i] = 0;
        dir[dir_idx] = (uint32_t)new_tbl | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
    } else if (flags & PAGE_USER) {
        dir[dir_idx] |= PAGE_USER;
    }

    tbl = (uint32_t *)(dir[dir_idx] & ~0xFFF);
    tbl[tbl_idx] = (phys & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;

    if (dir == current_dir) {
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
    return (paging_get_flags_in_directory(dir, virt) & PAGE_PRESENT) ? 0 : -1;
}

int paging_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    return paging_map_in_directory(current_dir, virt, phys, flags);
}

int paging_map_range(uint32_t virt, uint32_t phys, uint32_t size, uint32_t flags) {
    uint32_t virt_start;
    uint32_t phys_start;
    uint32_t offset;
    uint32_t pages;
    uint32_t i;
    if (size == 0) return 0;
    virt_start = virt & ~(PAGE_SIZE - 1U);
    phys_start = phys & ~(PAGE_SIZE - 1U);
    offset = virt - virt_start;
    if (size > 0xFFFFFFFFU - offset) return -1;
    size += offset;
    pages = size / PAGE_SIZE;
    if (size & (PAGE_SIZE - 1)) pages++;
    if (pages > 0x100000U) return -1;
    for (i = 0; i < pages; i++) {
        uint32_t delta = i * PAGE_SIZE;
        if (virt_start > 0xFFFFFFFFU - delta ||
            phys_start > 0xFFFFFFFFU - delta) return -1;
        if (paging_map(virt_start + delta, phys_start + delta, flags) < 0) return -1;
    }
    return 0;
}

uint32_t paging_get_phys(uint32_t virt) {
    return paging_get_phys_in_directory(current_dir, virt);
}

uint32_t paging_get_flags(uint32_t virt) {
    return paging_get_flags_in_directory(current_dir, virt);
}

uint32_t paging_get_phys_in_directory(uint32_t *dir, uint32_t virt) {
    uint32_t dir_idx = virt >> 22;
    uint32_t tbl_idx = (virt >> 12) & 0x3FF;
    uint32_t *tbl;
    if (!dir) dir = current_dir;
    if (!(dir[dir_idx] & PAGE_PRESENT)) return 0;
    tbl = (uint32_t *)(dir[dir_idx] & ~0xFFF);
    if (!(tbl[tbl_idx] & PAGE_PRESENT)) return 0;
    return (tbl[tbl_idx] & ~0xFFF) | (virt & 0xFFF);
}

uint32_t paging_get_flags_in_directory(uint32_t *dir, uint32_t virt) {
    uint32_t dir_idx = virt >> 22;
    uint32_t tbl_idx = (virt >> 12) & 0x3FF;
    uint32_t *tbl;
    if (!dir) dir = current_dir;
    if (!(dir[dir_idx] & PAGE_PRESENT)) return 0;
    tbl = (uint32_t *)(dir[dir_idx] & ~0xFFF);
    if (!(tbl[tbl_idx] & PAGE_PRESENT)) return 0;
    return tbl[tbl_idx] & 0xFFF;
}

int paging_map_user_page(uint32_t *dir, uint32_t virt, uint32_t phys, uint32_t flags) {
    if (!dir || (virt & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1))) return -1;
    if (!user_page_ok(virt)) return -1;
    if (paging_get_flags_in_directory(dir, virt) & PAGE_PRESENT) return -1;
    if (paging_map_in_directory(dir, virt, phys, flags | PAGE_USER) < 0) return -1;
    return (paging_get_flags_in_directory(dir, virt) & PAGE_PRESENT) ? 0 : -1;
}

int paging_set_user_page_flags(uint32_t *dir, uint32_t virt, uint32_t flags) {
    uint32_t dir_idx = virt >> 22;
    uint32_t tbl_idx = (virt >> 12) & 0x3FF;
    uint32_t *tbl;
    uint32_t pte;
    if (!dir || (virt & (PAGE_SIZE - 1)) || !user_page_ok(virt)) return -1;
    if (!(dir[dir_idx] & PAGE_PRESENT)) return -1;
    tbl = (uint32_t *)(dir[dir_idx] & ~0xFFF);
    pte = tbl[tbl_idx];
    if ((pte & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) return -1;
    tbl[tbl_idx] = (pte & ~0xFFF) | PAGE_PRESENT | PAGE_USER | (flags & PAGE_WRITE);
    if (dir == current_dir) __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

int paging_unmap_user_page(uint32_t *dir, uint32_t virt) {
    uint32_t dir_idx = virt >> 22;
    uint32_t tbl_idx = (virt >> 12) & 0x3FF;
    uint32_t *tbl;
    uint32_t pte;
    if (!dir || (virt & (PAGE_SIZE - 1)) || !user_page_ok(virt)) return -1;
    if (!(dir[dir_idx] & PAGE_PRESENT)) return -1;
    tbl = (uint32_t *)(dir[dir_idx] & ~0xFFF);
    pte = tbl[tbl_idx];
    if ((pte & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) return -1;
    pmm_free((void *)(pte & ~0xFFF));
    tbl[tbl_idx] = 0;
    if (dir == current_dir) __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

void paging_destroy_user_directory(uint32_t *dir) {
    uint32_t i;
    if (!dir || dir == page_dir) return;
    for (i = 0; i < PAGE_DIR_SIZE; i++) {
        uint32_t pde = dir[i];
        uint32_t j;
        uint32_t *tbl;
        if ((pde & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) continue;
        tbl = (uint32_t *)(pde & ~0xFFF);
        for (j = 0; j < PAGE_TBL_SIZE; j++) {
            uint32_t pte = tbl[j];
            if ((pte & (PAGE_PRESENT | PAGE_USER)) == (PAGE_PRESENT | PAGE_USER)) {
                pmm_free((void *)(pte & ~0xFFF));
                tbl[j] = 0;
            }
        }
        pmm_free(tbl);
        dir[i] = 0;
    }
    pmm_free(dir);
}
