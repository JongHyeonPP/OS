#include "driver.h"
#include "debug.h"

static driver_t g_drivers[DRIVER_MAX];
static char g_driver_names[DRIVER_MAX][DRIVER_NAME_MAX];
static uint32_t g_driver_count;

static int bus_valid(driver_bus_t bus) {
    return bus >= DRIVER_BUS_PLATFORM && bus <= DRIVER_BUS_NET;
}

static int name_char_ok(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' ||
           ch == '-';
}

static int driver_name_valid(const char *name) {
    uint32_t len = 0;
    if (!name || !name[0]) return 0;
    while (name[len]) {
        if (len + 1U >= DRIVER_NAME_MAX || !name_char_ok(name[len])) return 0;
        len++;
    }
    return len > 0;
}

static void copy_name(char *dst, const char *src) {
    uint32_t i = 0;
    while (src && src[i] && i + 1U < DRIVER_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void driver_init(void) {
    uint32_t i;
    for (i = 0; i < DRIVER_MAX; i++) {
        g_drivers[i].name = 0;
        g_drivers[i].bus = DRIVER_BUS_PLATFORM;
        g_drivers[i].id0 = 0;
        g_drivers[i].id1 = 0;
        g_drivers[i].loaded = 0;
        g_driver_names[i][0] = 0;
    }
    g_driver_count = 0;
    driver_register("serial-console", DRIVER_BUS_PLATFORM, 0x3F8, 0);
    driver_register("pit-timer", DRIVER_BUS_PLATFORM, 1000, 0);
    driver_register("ps2-keyboard", DRIVER_BUS_PLATFORM, 1, 0);
    driver_register("ps2-mouse", DRIVER_BUS_PLATFORM, 12, 0);
    driver_register("framebuffer", DRIVER_BUS_PLATFORM, 800, 600);
    debug_puts("[driver] registry initialized\n");
}

int driver_register(const char *name, driver_bus_t bus, uint32_t id0, uint32_t id1) {
    if (!driver_name_valid(name) || !bus_valid(bus) || g_driver_count >= DRIVER_MAX) return -1;
    copy_name(g_driver_names[g_driver_count], name);
    g_drivers[g_driver_count].name = g_driver_names[g_driver_count];
    g_drivers[g_driver_count].bus = bus;
    g_drivers[g_driver_count].id0 = id0;
    g_drivers[g_driver_count].id1 = id1;
    g_drivers[g_driver_count].loaded = 1;
    return (int)g_driver_count++;
}

uint32_t driver_count(void) {
    return g_driver_count;
}

const driver_t *driver_at(uint32_t index) {
    if (index >= g_driver_count) return 0;
    return &g_drivers[index];
}

const char *driver_bus_name(driver_bus_t bus) {
    switch (bus) {
        case DRIVER_BUS_PLATFORM: return "platform";
        case DRIVER_BUS_PCI: return "pci";
        case DRIVER_BUS_BLOCK: return "block";
        case DRIVER_BUS_NET: return "net";
        default: return "unknown";
    }
}
