#include "block.h"
#include "debug.h"
#include "driver.h"

#define RAMDISK_SECTORS 128

typedef struct {
    block_device_t info;
    block_read_fn read;
    block_write_fn write;
    void *ctx;
    char name[BLOCK_NAME_MAX];
} block_slot_t;

static uint8_t g_ramdisk[RAMDISK_SECTORS * BLOCK_SECTOR_SIZE];
static block_slot_t g_blocks[BLOCK_MAX_DEVICES];
static uint32_t g_block_count;

static void memcopy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}

static int streq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static int name_char_ok(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' ||
           ch == '-';
}

static int block_name_valid(const char *name) {
    uint32_t len = 0;
    if (!name || !name[0]) return 0;
    while (name[len]) {
        if (len + 1U >= BLOCK_NAME_MAX || !name_char_ok(name[len])) return 0;
        len++;
    }
    return len > 0;
}

static void copy_name(char *dst, const char *src) {
    uint32_t i = 0;
    while (src && src[i] && i + 1U < BLOCK_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static uint32_t sat_add_u32(uint32_t a, uint32_t b) {
    if (b > 0xFFFFFFFFU - a) return 0xFFFFFFFFU;
    return a + b;
}

static int ramdisk_read(void *ctx, uint32_t lba, void *buf, uint32_t sectors) {
    uint8_t *data = (uint8_t *)ctx;
    memcopy((uint8_t *)buf, data + lba * BLOCK_SECTOR_SIZE, sectors * BLOCK_SECTOR_SIZE);
    return (int)sectors;
}

static int ramdisk_write(void *ctx, uint32_t lba, const void *buf, uint32_t sectors) {
    uint8_t *data = (uint8_t *)ctx;
    memcopy(data + lba * BLOCK_SECTOR_SIZE, (const uint8_t *)buf, sectors * BLOCK_SECTOR_SIZE);
    return (int)sectors;
}

int block_register(const char *name,
                   uint32_t sectors,
                   int writable,
                   block_read_fn read,
                   block_write_fn write,
                   void *ctx) {
    uint32_t i;
    if (!block_name_valid(name) || !read || sectors == 0 || g_block_count >= BLOCK_MAX_DEVICES) return -1;
    for (i = 0; i < g_block_count; i++) {
        if (streq(name, g_blocks[i].info.name)) return -1;
    }
    copy_name(g_blocks[g_block_count].name, name);
    g_blocks[g_block_count].info.name = g_blocks[g_block_count].name;
    g_blocks[g_block_count].info.sector_count = sectors;
    g_blocks[g_block_count].info.sector_size = BLOCK_SECTOR_SIZE;
    g_blocks[g_block_count].info.writable = writable ? 1 : 0;
    g_blocks[g_block_count].info.read_ops = 0;
    g_blocks[g_block_count].info.write_ops = 0;
    g_blocks[g_block_count].info.read_sectors = 0;
    g_blocks[g_block_count].info.write_sectors = 0;
    g_blocks[g_block_count].read = read;
    g_blocks[g_block_count].write = write;
    g_blocks[g_block_count].ctx = ctx;
    return (int)g_block_count++;
}

void block_init(void) {
    uint32_t i;
    for (i = 0; i < sizeof(g_ramdisk); i++) g_ramdisk[i] = 0;
    g_ramdisk[510] = 0x55;
    g_ramdisk[511] = 0xAA;
    g_block_count = 0;
    block_register("ram0", RAMDISK_SECTORS, 1, ramdisk_read, ramdisk_write, g_ramdisk);
    driver_register("ramdisk-block", DRIVER_BUS_BLOCK, RAMDISK_SECTORS, BLOCK_SECTOR_SIZE);
    debug_puts("[block] ram0 registered\n");
}

uint32_t block_count(void) {
    return g_block_count;
}

const block_device_t *block_at(uint32_t index) {
    if (index >= g_block_count) return 0;
    return &g_blocks[index].info;
}

int block_find(const char *name) {
    uint32_t i;
    if (!name) return -1;
    for (i = 0; i < g_block_count; i++) {
        if (streq(name, g_blocks[i].info.name)) return (int)i;
    }
    return -1;
}

int block_read(uint32_t dev, uint32_t lba, void *buf, uint32_t sectors) {
    block_slot_t *slot;
    int ret;
    if (dev >= g_block_count) return -1;
    slot = &g_blocks[dev];
    if (sectors == 0) return 0;
    if (!buf) return -1;
    if (lba > slot->info.sector_count || sectors > slot->info.sector_count - lba) return -1;
    ret = slot->read(slot->ctx, lba, buf, sectors);
    if (ret > 0) {
        slot->info.read_ops = sat_add_u32(slot->info.read_ops, 1);
        slot->info.read_sectors = sat_add_u32(slot->info.read_sectors, (uint32_t)ret);
    }
    return ret;
}

int block_write(uint32_t dev, uint32_t lba, const void *buf, uint32_t sectors) {
    block_slot_t *slot;
    int ret;
    if (dev >= g_block_count) return -1;
    slot = &g_blocks[dev];
    if (sectors == 0) return 0;
    if (!buf) return -1;
    if (!slot->info.writable || !slot->write) return -1;
    if (lba > slot->info.sector_count || sectors > slot->info.sector_count - lba) return -1;
    ret = slot->write(slot->ctx, lba, buf, sectors);
    if (ret > 0) {
        slot->info.write_ops = sat_add_u32(slot->info.write_ops, 1);
        slot->info.write_sectors = sat_add_u32(slot->info.write_sectors, (uint32_t)ret);
    }
    return ret;
}
