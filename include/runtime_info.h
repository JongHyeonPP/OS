#ifndef RUNTIME_INFO_H
#define RUNTIME_INFO_H

#include <stdint.h>
#include "net.h"

#define RUNTIME_POWER_HISTORY 8
#define RUNTIME_WEATHER_DAYS  3
#define RUNTIME_HOURLY_COUNT  8

typedef struct {
    int percent;
    int charging;
    int minutes_remaining;
    int health_percent;
    int history[RUNTIME_POWER_HISTORY];
    const char *status;
    const char *health_status;
} runtime_power_info_t;

typedef struct {
    const char *label;
    int temp_c;
    int high_c;
    int low_c;
    const char *condition;
} runtime_weather_day_t;

typedef struct {
    const char *location;
    const char *location_full;
    int temperature_c;
    int high_c;
    int low_c;
    const char *condition;
    const char *next_summary;
    runtime_weather_day_t forecast[RUNTIME_WEATHER_DAYS];
    const char *hourly_label[RUNTIME_HOURLY_COUNT];
    const char *hourly_icon[RUNTIME_HOURLY_COUNT];
    int hourly_temp_c[RUNTIME_HOURLY_COUNT];
} runtime_weather_info_t;

typedef struct {
    const char *sysname;
    const char *nodename;
    const char *release;
    const char *version;
    const char *machine;
    const char *cpu_model;
    const char *display;
    const char *boot_loader;
    uint32_t cpu_count;
    uint32_t uptime_seconds;
    uint32_t timer_hz;
    uint32_t pmm_total_pages;
    uint32_t pmm_free_pages;
    uint32_t pmm_total_bytes;
    uint32_t pmm_free_bytes;
    uint32_t heap_used_bytes;
    uint32_t heap_free_bytes;
    uint32_t heap_total_bytes;
    uint32_t heap_block_count;
    uint32_t heap_free_block_count;
    uint32_t process_count;
    uint32_t task_count;
    uint32_t task_free_slots;
    uint32_t driver_count;
    uint32_t pci_count;
    uint32_t block_count;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t display_bpp;
    int cpu_load_percent;
    int mem_used_percent;
} runtime_system_info_t;

typedef struct {
    char name[16];
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t used_blocks;
    uint32_t total_bytes;
    uint32_t free_bytes;
    uint32_t used_bytes;
    int used_percent;
} runtime_storage_info_t;

void runtime_get_power_info(runtime_power_info_t *out);
void runtime_get_weather_info(runtime_weather_info_t *out);
void runtime_get_system_info(runtime_system_info_t *out);
int  runtime_get_storage_info(const char *path, runtime_storage_info_t *out);
const netif_t *runtime_primary_netif(void);
uint32_t runtime_dns_server4(void);

void runtime_format_uint(uint32_t value, char *buf, uint32_t max);
void runtime_format_percent(int percent, char *buf, uint32_t max);
void runtime_format_temperature_c(int temp_c, char *buf, uint32_t max);
void runtime_format_high_low(int high_c, int low_c, char *buf, uint32_t max);
void runtime_format_minutes(int minutes, char *buf, uint32_t max);
void runtime_format_relative_time(uint32_t age_seconds, char *buf, uint32_t max);
void runtime_format_uptime(uint32_t seconds, char *buf, uint32_t max);
void runtime_format_bytes(uint32_t bytes, char *buf, uint32_t max);
void runtime_format_ipv4(uint32_t ip, char *buf, uint32_t max);
void runtime_format_mac(const uint8_t mac[6], char *buf, uint32_t max);

#endif /* RUNTIME_INFO_H */
