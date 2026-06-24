#ifndef ELF_H
#define ELF_H

#include <stdint.h>

typedef struct {
    uint32_t entry;
    uint32_t low_vaddr;
    uint32_t high_vaddr;
    uint32_t load_segments;
} elf_image_t;

#define ELF_PF_X 1
#define ELF_PF_W 2
#define ELF_PF_R 4

typedef int (*elf_segment_cb_t)(void *ctx,
                                uint32_t vaddr,
                                uint32_t memsz,
                                uint32_t flags,
                                const uint8_t *data,
                                uint32_t filesz);

int elf_probe32(const void *image, uint32_t size);
int elf_load32_metadata(const void *image, uint32_t size, elf_image_t *out);
int elf_load32_segments(const void *image, uint32_t size, elf_segment_cb_t cb, void *ctx);

#endif
