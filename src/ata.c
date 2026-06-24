#include "ata.h"
#include "block.h"
#include "debug.h"
#include "driver.h"
#include "io.h"
#include <stdint.h>

#define ATA_MAX_DRIVES 4

#define ATA_REG_DATA       0
#define ATA_REG_ERROR      1
#define ATA_REG_SECCOUNT0  2
#define ATA_REG_LBA0       3
#define ATA_REG_LBA1       4
#define ATA_REG_LBA2       5
#define ATA_REG_HDDEVSEL   6
#define ATA_REG_COMMAND    7
#define ATA_REG_STATUS     7

#define ATA_CMD_IDENTIFY      0xEC
#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH   0xE7
#define ATA_LBA28_MAX         0x0FFFFFFFU

#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

typedef struct {
    uint16_t io;
    uint16_t ctrl;
    uint8_t slave;
    uint32_t sectors;
    char name[8];
    int present;
} ata_drive_t;

static ata_drive_t g_ata[ATA_MAX_DRIVES];
static uint32_t g_ata_count;

static void ata_delay(ata_drive_t *d) {
    inb(d->ctrl);
    inb(d->ctrl);
    inb(d->ctrl);
    inb(d->ctrl);
}

static int ata_wait_clear_bsy(ata_drive_t *d) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if ((inb(d->io + ATA_REG_STATUS) & ATA_SR_BSY) == 0) return 0;
    }
    return -1;
}

static int ata_wait_drq(ata_drive_t *d) {
    uint32_t timeout = 100000;
    while (timeout--) {
        uint8_t st = inb(d->io + ATA_REG_STATUS);
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return -1;
        if ((st & ATA_SR_BSY) == 0 && (st & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

static void ata_select(ata_drive_t *d, uint32_t lba) {
    outb(d->io + ATA_REG_HDDEVSEL,
         (uint8_t)(0xE0 | (d->slave ? 0x10 : 0) | ((lba >> 24) & 0x0F)));
    ata_delay(d);
}

static int ata_read_one(void *ctx, uint32_t lba, void *buf) {
    ata_drive_t *d = (ata_drive_t *)ctx;
    uint16_t *out = (uint16_t *)buf;
    uint32_t i;
    if (!d || !buf || !d->present || lba >= d->sectors || lba > ATA_LBA28_MAX) return -1;
    ata_select(d, lba);
    outb(d->io + ATA_REG_SECCOUNT0, 1);
    outb(d->io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(d->io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(d->io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(d->io + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);
    if (ata_wait_drq(d) < 0) return -1;
    for (i = 0; i < 256; i++) out[i] = inw(d->io + ATA_REG_DATA);
    return 0;
}

static int ata_write_one(void *ctx, uint32_t lba, const void *buf) {
    ata_drive_t *d = (ata_drive_t *)ctx;
    const uint16_t *in = (const uint16_t *)buf;
    uint32_t i;
    if (!d || !buf || !d->present || lba >= d->sectors || lba > ATA_LBA28_MAX) return -1;
    ata_select(d, lba);
    outb(d->io + ATA_REG_SECCOUNT0, 1);
    outb(d->io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(d->io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(d->io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(d->io + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);
    if (ata_wait_drq(d) < 0) return -1;
    for (i = 0; i < 256; i++) outw(d->io + ATA_REG_DATA, in[i]);
    outb(d->io + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    return ata_wait_clear_bsy(d);
}

static int ata_block_read(void *ctx, uint32_t lba, void *buf, uint32_t sectors) {
    ata_drive_t *d = (ata_drive_t *)ctx;
    uint32_t i;
    if (!d || !buf) return -1;
    if (sectors == 0) return 0;
    if (lba > d->sectors || sectors > d->sectors - lba) return -1;
    if (lba > ATA_LBA28_MAX || sectors - 1U > ATA_LBA28_MAX - lba) return -1;
    for (i = 0; i < sectors; i++) {
        if (ata_read_one(ctx, lba + i, (uint8_t *)buf + i * BLOCK_SECTOR_SIZE) < 0) return -1;
    }
    return (int)sectors;
}

static int ata_block_write(void *ctx, uint32_t lba, const void *buf, uint32_t sectors) {
    ata_drive_t *d = (ata_drive_t *)ctx;
    uint32_t i;
    if (!d || !buf) return -1;
    if (sectors == 0) return 0;
    if (lba > d->sectors || sectors > d->sectors - lba) return -1;
    if (lba > ATA_LBA28_MAX || sectors - 1U > ATA_LBA28_MAX - lba) return -1;
    for (i = 0; i < sectors; i++) {
        if (ata_write_one(ctx, lba + i, (const uint8_t *)buf + i * BLOCK_SECTOR_SIZE) < 0) return -1;
    }
    return (int)sectors;
}

static uint32_t ata_identify_sectors(ata_drive_t *d) {
    uint16_t id[256];
    uint8_t st;
    uint32_t i;
    ata_select(d, 0);
    outb(d->io + ATA_REG_SECCOUNT0, 0);
    outb(d->io + ATA_REG_LBA0, 0);
    outb(d->io + ATA_REG_LBA1, 0);
    outb(d->io + ATA_REG_LBA2, 0);
    outb(d->io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    st = inb(d->io + ATA_REG_STATUS);
    if (st == 0) return 0;
    if (ata_wait_clear_bsy(d) < 0) return 0;
    if (inb(d->io + ATA_REG_LBA1) != 0 || inb(d->io + ATA_REG_LBA2) != 0) return 0;
    if (ata_wait_drq(d) < 0) return 0;
    for (i = 0; i < 256; i++) id[i] = inw(d->io + ATA_REG_DATA);
    return ((uint32_t)id[61] << 16) | id[60];
}

static void make_ata_name(char *name, uint32_t index) {
    name[0] = 'a';
    name[1] = 't';
    name[2] = 'a';
    name[3] = (char)('0' + index);
    name[4] = 0;
}

static void ata_probe_one(uint16_t io, uint16_t ctrl, uint8_t slave) {
    ata_drive_t *d;
    uint32_t sectors;
    int blk;
    if (g_ata_count >= ATA_MAX_DRIVES) return;
    d = &g_ata[g_ata_count];
    d->io = io;
    d->ctrl = ctrl;
    d->slave = slave;
    d->present = 0;
    d->sectors = 0;
    make_ata_name(d->name, g_ata_count);
    sectors = ata_identify_sectors(d);
    if (sectors == 0) return;
    d->sectors = sectors;
    d->present = 1;
    blk = block_register(d->name, d->sectors, 1, ata_block_read, ata_block_write, d);
    if (blk >= 0) {
        driver_register("ata-pio", DRIVER_BUS_BLOCK, io, sectors);
        debug_puts("[ata] registered ");
        debug_puts(d->name);
        debug_puts(" sectors=");
        debug_dec(sectors);
        debug_puts("\n");
        g_ata_count++;
    }
}

void ata_init(void) {
    g_ata_count = 0;
    ata_probe_one(0x1F0, 0x3F6, 0);
    ata_probe_one(0x1F0, 0x3F6, 1);
    ata_probe_one(0x170, 0x376, 0);
    ata_probe_one(0x170, 0x376, 1);
    debug_puts("[ata] drives=");
    debug_dec(g_ata_count);
    debug_puts("\n");
}
