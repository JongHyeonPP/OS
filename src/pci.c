#include "pci.h"
#include "debug.h"
#include "driver.h"
#include "io.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static pci_device_t g_pci[PCI_MAX_DEVICES];
static uint32_t g_pci_count;

static int pci_config_args_valid(uint8_t slot, uint8_t function, uint8_t offset) {
    return slot < 32 && function < 8 && offset <= 0xFC;
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address =
        0x80000000U |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);
    if (!pci_config_args_valid(slot, function, offset)) return 0xFFFFFFFFU;
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address =
        0x80000000U |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)function << 8) |
        (offset & 0xFC);
    if (!pci_config_args_valid(slot, function, offset)) return;
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, value);
}

static void pci_record(uint8_t bus, uint8_t slot, uint8_t function) {
    uint32_t id = pci_config_read32(bus, slot, function, 0x00);
    pci_device_t *dev;
    uint32_t class_reg;
    uint32_t i;
    if ((id & 0xFFFF) == 0xFFFF || g_pci_count >= PCI_MAX_DEVICES) return;
    dev = &g_pci[g_pci_count++];
    dev->bus = bus;
    dev->slot = slot;
    dev->function = function;
    dev->vendor_id = (uint16_t)(id & 0xFFFF);
    dev->device_id = (uint16_t)((id >> 16) & 0xFFFF);
    class_reg = pci_config_read32(bus, slot, function, 0x08);
    dev->revision = (uint8_t)(class_reg & 0xFF);
    dev->prog_if = (uint8_t)((class_reg >> 8) & 0xFF);
    dev->subclass = (uint8_t)((class_reg >> 16) & 0xFF);
    dev->class_code = (uint8_t)((class_reg >> 24) & 0xFF);
    dev->header_type = (uint8_t)((pci_config_read32(bus, slot, function, 0x0C) >> 16) & 0xFF);
    for (i = 0; i < 6; i++) dev->bar[i] = pci_config_read32(bus, slot, function, (uint8_t)(0x10 + i * 4));
    driver_register("pci-device", DRIVER_BUS_PCI,
                    ((uint32_t)dev->vendor_id << 16) | dev->device_id,
                    ((uint32_t)dev->class_code << 8) | dev->subclass);
}

void pci_init(void) {
    uint32_t bus;
    uint32_t slot;
    uint32_t function;
    g_pci_count = 0;
    for (bus = 0; bus < 256 && g_pci_count < PCI_MAX_DEVICES; bus++) {
        for (slot = 0; slot < 32 && g_pci_count < PCI_MAX_DEVICES; slot++) {
            uint32_t id = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x00);
            uint8_t header;
            uint32_t max_function;
            if ((id & 0xFFFF) == 0xFFFF) continue;
            pci_record((uint8_t)bus, (uint8_t)slot, 0);
            header = (uint8_t)((pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0x0C) >> 16) & 0xFF);
            max_function = (header & 0x80) ? 8U : 1U;
            for (function = 1; function < max_function && g_pci_count < PCI_MAX_DEVICES; function++) {
                pci_record((uint8_t)bus, (uint8_t)slot, (uint8_t)function);
            }
        }
    }
    debug_puts("[pci] devices=");
    debug_dec(g_pci_count);
    debug_puts("\n");
}

uint32_t pci_count(void) {
    return g_pci_count;
}

const pci_device_t *pci_at(uint32_t index) {
    if (index >= g_pci_count) return 0;
    return &g_pci[index];
}

const pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass) {
    uint32_t i;
    for (i = 0; i < g_pci_count; i++) {
        if (g_pci[i].class_code == class_code && g_pci[i].subclass == subclass) return &g_pci[i];
    }
    return 0;
}

const pci_device_t *pci_find_vendor_device(uint16_t vendor_id, uint16_t device_id) {
    uint32_t i;
    for (i = 0; i < g_pci_count; i++) {
        if (g_pci[i].vendor_id == vendor_id && g_pci[i].device_id == device_id) return &g_pci[i];
    }
    return 0;
}

void pci_enable_busmaster(const pci_device_t *dev) {
    uint32_t cmd;
    if (!dev) return;
    cmd = pci_config_read32(dev->bus, dev->slot, dev->function, 0x04);
    cmd |= 0x00000007U;
    pci_config_write32(dev->bus, dev->slot, dev->function, 0x04, cmd);
}
