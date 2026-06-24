#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

#define BLOCK_MAX_DEVICES 8
#define BLOCK_SECTOR_SIZE 512
#define BLOCK_NAME_MAX    32

typedef struct {
    const char *name;
    uint32_t sector_count;
    uint32_t sector_size;
    int writable;
    uint32_t read_ops;
    uint32_t write_ops;
    uint32_t read_sectors;
    uint32_t write_sectors;
} block_device_t;

typedef int (*block_read_fn)(void *ctx, uint32_t lba, void *buf, uint32_t sectors);
typedef int (*block_write_fn)(void *ctx, uint32_t lba, const void *buf, uint32_t sectors);

void block_init(void);
int block_register(const char *name,
                   uint32_t sectors,
                   int writable,
                   block_read_fn read,
                   block_write_fn write,
                   void *ctx);
uint32_t block_count(void);
const block_device_t *block_at(uint32_t index);
int block_find(const char *name);
int block_read(uint32_t dev, uint32_t lba, void *buf, uint32_t sectors);
int block_write(uint32_t dev, uint32_t lba, const void *buf, uint32_t sectors);

#endif
