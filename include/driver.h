#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>

#define DRIVER_MAX 64
#define DRIVER_NAME_MAX 32

typedef enum {
    DRIVER_BUS_PLATFORM = 0,
    DRIVER_BUS_PCI      = 1,
    DRIVER_BUS_BLOCK    = 2,
    DRIVER_BUS_NET      = 3
} driver_bus_t;

typedef struct {
    const char *name;
    driver_bus_t bus;
    uint32_t id0;
    uint32_t id1;
    int loaded;
} driver_t;

void driver_init(void);
int driver_register(const char *name, driver_bus_t bus, uint32_t id0, uint32_t id1);
uint32_t driver_count(void);
const driver_t *driver_at(uint32_t index);
const char *driver_bus_name(driver_bus_t bus);

#endif
