#include "elf.h"

#define EI_NIDENT 16
#define PT_LOAD   1
#define EM_386    3
#define ET_EXEC   2
#define EV_CURRENT 1

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

static int range_ok(uint32_t off, uint32_t len, uint32_t size) {
    return off <= size && len <= size - off;
}

static int checked_add_u32(uint32_t a, uint32_t b, uint32_t *out) {
    if (a > 0xFFFFFFFFU - b) return 0;
    *out = a + b;
    return 1;
}

static int checked_mul_u32(uint32_t a, uint32_t b, uint32_t *out) {
    if (a != 0 && b > 0xFFFFFFFFU / a) return 0;
    *out = a * b;
    return 1;
}

static int valid_load_align(uint32_t vaddr, uint32_t offset, uint32_t align) {
    if (align == 0 || align == 1) return 1;
    if (align & (align - 1U)) return 0;
    return (vaddr & (align - 1U)) == (offset & (align - 1U));
}

static int load_segment_bounds(const elf32_phdr_t *ph,
                               uint32_t image_size,
                               uint32_t *seg_end) {
    if (!range_ok(ph->p_offset, ph->p_filesz, image_size)) return 0;
    if (ph->p_memsz < ph->p_filesz) return 0;
    if (!valid_load_align(ph->p_vaddr, ph->p_offset, ph->p_align)) return 0;
    if (!checked_add_u32(ph->p_vaddr, ph->p_memsz, seg_end)) return 0;
    return 1;
}

static int load_segment_overlaps_prior(const void *image,
                                       uint32_t size,
                                       const elf32_ehdr_t *eh,
                                       uint16_t index,
                                       uint32_t seg_start,
                                       uint32_t seg_end) {
    uint16_t j;
    if (seg_start == seg_end) return 0;
    for (j = 0; j < index; j++) {
        const elf32_phdr_t *prev =
            (const elf32_phdr_t *)((const uint8_t *)image + eh->e_phoff + (uint32_t)j * sizeof(*prev));
        uint32_t prev_end;
        if (prev->p_type != PT_LOAD) continue;
        if (!load_segment_bounds(prev, size, &prev_end)) return 1;
        if (prev->p_vaddr == prev_end) continue;
        if (seg_start < prev_end && prev->p_vaddr < seg_end) return 1;
    }
    return 0;
}

int elf_probe32(const void *image, uint32_t size) {
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)image;
    uint32_t ph_size;
    if (!image || size < sizeof(*eh)) return 0;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') return 0;
    if (eh->e_ident[4] != 1 || eh->e_ident[5] != 1 || eh->e_ident[6] != EV_CURRENT) return 0;
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_386) return 0;
    if (eh->e_version != EV_CURRENT || eh->e_ehsize != sizeof(elf32_ehdr_t)) return 0;
    if (eh->e_phnum == 0) return 0;
    if (eh->e_phentsize != sizeof(elf32_phdr_t)) return 0;
    if (!checked_mul_u32((uint32_t)eh->e_phnum, sizeof(elf32_phdr_t), &ph_size)) return 0;
    if (!range_ok(eh->e_phoff, ph_size, size)) return 0;
    return 1;
}

int elf_load32_metadata(const void *image, uint32_t size, elf_image_t *out) {
    const elf32_ehdr_t *eh = (const elf32_ehdr_t *)image;
    uint16_t i;
    int entry_ok = 0;
    if (!out || !elf_probe32(image, size)) return -1;
    out->entry = eh->e_entry;
    out->low_vaddr = 0xFFFFFFFFU;
    out->high_vaddr = 0;
    out->load_segments = 0;
    for (i = 0; i < eh->e_phnum; i++) {
        const elf32_phdr_t *ph = (const elf32_phdr_t *)((const uint8_t *)image + eh->e_phoff + (uint32_t)i * sizeof(*ph));
        uint32_t seg_end;
        if (ph->p_type != PT_LOAD) continue;
        if (!load_segment_bounds(ph, size, &seg_end)) return -1;
        if (load_segment_overlaps_prior(image, size, eh, i, ph->p_vaddr, seg_end)) return -1;
        if (ph->p_vaddr < out->low_vaddr) out->low_vaddr = ph->p_vaddr;
        if (seg_end > out->high_vaddr) out->high_vaddr = seg_end;
        if ((ph->p_flags & ELF_PF_X) &&
            eh->e_entry >= ph->p_vaddr &&
            eh->e_entry < seg_end) entry_ok = 1;
        out->load_segments++;
    }
    if (out->load_segments == 0) return -1;
    if (!entry_ok) return -1;
    return 0;
}

int elf_load32_segments(const void *image, uint32_t size, elf_segment_cb_t cb, void *ctx) {
    const elf32_ehdr_t *eh;
    elf_image_t meta;
    uint16_t i;
    if (!cb || elf_load32_metadata(image, size, &meta) < 0) return -1;
    eh = (const elf32_ehdr_t *)image;
    for (i = 0; i < eh->e_phnum; i++) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)((const uint8_t *)image + eh->e_phoff + (uint32_t)i * sizeof(*ph));
        if (ph->p_type != PT_LOAD) continue;
        {
            uint32_t seg_end;
            if (!load_segment_bounds(ph, size, &seg_end)) return -1;
        }
        if (cb(ctx,
               ph->p_vaddr,
               ph->p_memsz,
               ph->p_flags,
               (const uint8_t *)image + ph->p_offset,
               ph->p_filesz) < 0) return -1;
    }
    return 0;
}
