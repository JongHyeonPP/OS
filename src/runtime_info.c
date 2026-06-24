#include "runtime_info.h"
#include "block.h"
#include "driver.h"
#include "heap.h"
#include "pci.h"
#include "pmm.h"
#include "process.h"
#include "task.h"
#include "timer.h"
#include "uts.h"
#include "vfs.h"
#include "vga.h"
#include <stddef.h>
#include <stdint.h>

static void r_append_char(char *buf, uint32_t *pos, uint32_t max, char ch) {
    if (!buf || max == 0) return;
    if (*pos + 1 < max) buf[(*pos)++] = ch;
    buf[*pos < max ? *pos : max - 1] = 0;
}

static void r_append_str(char *buf, uint32_t *pos, uint32_t max, const char *s) {
    uint32_t i = 0;
    if (!s) return;
    while (s[i]) r_append_char(buf, pos, max, s[i++]);
}

static void r_append_uint(char *buf, uint32_t *pos, uint32_t max, uint32_t v) {
    char tmp[11];
    uint32_t n = 0;
    if (v == 0) {
        r_append_char(buf, pos, max, '0');
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (n) r_append_char(buf, pos, max, tmp[--n]);
}

static void r_copy(char *dst, uint32_t max, const char *src) {
    uint32_t i = 0;
    if (!dst || max == 0) return;
    while (src && src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static uint32_t total_heap_bytes(void) {
    return (uint32_t)(heap_used_bytes() + heap_free_bytes());
}

static uint32_t task_count_active(void) {
    uint32_t i, count = 0;
    for (i = 0; i < task_table_size(); i++) {
        const task_t *task = task_at(i);
        if (task && task->state != TASK_DEAD) count++;
    }
    return count;
}

static int percent_u32(uint32_t used, uint32_t total) {
    if (total == 0) return 0;
    if (used >= total) return 100;
    return (int)(((uint64_t)used * 100ULL) / (uint64_t)total);
}

void runtime_format_uint(uint32_t value, char *buf, uint32_t max) {
    uint32_t pos = 0;
    if (!buf || max == 0) return;
    buf[0] = 0;
    r_append_uint(buf, &pos, max, value);
}

void runtime_format_percent(int percent, char *buf, uint32_t max) {
    uint32_t pos = 0;
    if (!buf || max == 0) return;
    buf[0] = 0;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    r_append_uint(buf, &pos, max, (uint32_t)percent);
    r_append_char(buf, &pos, max, '%');
}

void runtime_format_temperature_c(int temp_c, char *buf, uint32_t max) {
    uint32_t pos = 0;
    if (!buf || max == 0) return;
    buf[0] = 0;
    if (temp_c < 0) {
        r_append_char(buf, &pos, max, '-');
        temp_c = -temp_c;
    }
    r_append_uint(buf, &pos, max, (uint32_t)temp_c);
    r_append_char(buf, &pos, max, 'o');
    r_append_char(buf, &pos, max, 'C');
}

void runtime_format_high_low(int high_c, int low_c, char *buf, uint32_t max) {
    uint32_t pos = 0;
    if (!buf || max == 0) return;
    buf[0] = 0;
    r_append_str(buf, &pos, max, "H:");
    r_append_uint(buf, &pos, max, (uint32_t)(high_c < 0 ? 0 : high_c));
    r_append_str(buf, &pos, max, "  L:");
    r_append_uint(buf, &pos, max, (uint32_t)(low_c < 0 ? 0 : low_c));
}

void runtime_format_minutes(int minutes, char *buf, uint32_t max) {
    uint32_t pos = 0;
    int h, m;
    if (!buf || max == 0) return;
    buf[0] = 0;
    if (minutes < 0) minutes = 0;
    h = minutes / 60;
    m = minutes % 60;
    if (h > 0) {
        r_append_uint(buf, &pos, max, (uint32_t)h);
        r_append_char(buf, &pos, max, 'h');
        r_append_char(buf, &pos, max, ' ');
    }
    r_append_uint(buf, &pos, max, (uint32_t)m);
    r_append_char(buf, &pos, max, 'm');
}

void runtime_format_relative_time(uint32_t age_seconds, char *buf, uint32_t max) {
    uint32_t pos = 0;
    if (!buf || max == 0) return;
    buf[0] = 0;
    if (age_seconds < 60U) {
        r_append_str(buf, &pos, max, "now");
    } else if (age_seconds < 3600U) {
        r_append_uint(buf, &pos, max, age_seconds / 60U);
        r_append_str(buf, &pos, max, "m ago");
    } else if (age_seconds < 86400U) {
        r_append_uint(buf, &pos, max, age_seconds / 3600U);
        r_append_str(buf, &pos, max, "h ago");
    } else {
        r_append_uint(buf, &pos, max, age_seconds / 86400U);
        r_append_str(buf, &pos, max, "d ago");
    }
}

void runtime_format_uptime(uint32_t seconds, char *buf, uint32_t max) {
    uint32_t pos = 0;
    uint32_t h = seconds / 3600U;
    uint32_t m = (seconds / 60U) % 60U;
    uint32_t s = seconds % 60U;
    if (!buf || max == 0) return;
    buf[0] = 0;
    r_append_uint(buf, &pos, max, h);
    r_append_char(buf, &pos, max, ':');
    r_append_char(buf, &pos, max, (char)('0' + m / 10U));
    r_append_char(buf, &pos, max, (char)('0' + m % 10U));
    r_append_char(buf, &pos, max, ':');
    r_append_char(buf, &pos, max, (char)('0' + s / 10U));
    r_append_char(buf, &pos, max, (char)('0' + s % 10U));
}

void runtime_format_bytes(uint32_t bytes, char *buf, uint32_t max) {
    uint32_t pos = 0;
    uint32_t value = bytes;
    const char *unit = "B";
    if (!buf || max == 0) return;
    buf[0] = 0;
    if (bytes >= 1024U * 1024U) {
        value = bytes / (1024U * 1024U);
        unit = "MB";
    } else if (bytes >= 1024U) {
        value = bytes / 1024U;
        unit = "KB";
    }
    r_append_uint(buf, &pos, max, value);
    r_append_char(buf, &pos, max, ' ');
    r_append_str(buf, &pos, max, unit);
}

void runtime_format_ipv4(uint32_t ip, char *buf, uint32_t max) {
    uint32_t pos = 0;
    if (!buf || max == 0) return;
    buf[0] = 0;
    r_append_uint(buf, &pos, max, (ip >> 24) & 0xFFU);
    r_append_char(buf, &pos, max, '.');
    r_append_uint(buf, &pos, max, (ip >> 16) & 0xFFU);
    r_append_char(buf, &pos, max, '.');
    r_append_uint(buf, &pos, max, (ip >> 8) & 0xFFU);
    r_append_char(buf, &pos, max, '.');
    r_append_uint(buf, &pos, max, ip & 0xFFU);
}

void runtime_format_mac(const uint8_t mac[6], char *buf, uint32_t max) {
    static const char hex[] = "0123456789ABCDEF";
    uint32_t pos = 0;
    int i;
    if (!buf || max == 0) return;
    buf[0] = 0;
    for (i = 0; i < 6; i++) {
        uint8_t b = mac ? mac[i] : 0;
        if (i) r_append_char(buf, &pos, max, ':');
        r_append_char(buf, &pos, max, hex[(b >> 4) & 0x0F]);
        r_append_char(buf, &pos, max, hex[b & 0x0F]);
    }
}

const netif_t *runtime_primary_netif(void) {
    uint32_t i;
    const netif_t *fallback = 0;
    for (i = 0; i < netif_count(); i++) {
        const netif_t *n = netif_at(i);
        if (!n) continue;
        if (!fallback) fallback = n;
        if (n->ipv4 && n->up && !(n->ipv4 >> 24 == 127U))
            return n;
    }
    return fallback;
}

void runtime_get_power_info(runtime_power_info_t *out) {
    uint32_t t;
    int i;
    if (!out) return;
    t = timer_ticks();
    out->percent = 85 - (int)((t / 10000U) % 25U);
    if (out->percent < 20) out->percent = 20;
    out->charging = 1;
    out->minutes_remaining = (100 - out->percent) * 3;
    out->health_percent = 100;
    out->status = out->charging ? "Charging" : "On Battery";
    out->health_status = "Normal";
    for (i = 0; i < RUNTIME_POWER_HISTORY; i++) {
        int sample = out->percent - (RUNTIME_POWER_HISTORY - 1 - i) * 4;
        if (sample < 8) sample = 8;
        if (sample > 100) sample = 100;
        out->history[i] = sample;
    }
}

void runtime_get_weather_info(runtime_weather_info_t *out) {
    uint32_t phase;
    int i;
    static const char *hours[RUNTIME_HOURLY_COUNT] = {
        "Now", "10", "11", "12", "1PM", "2", "3", "4"
    };
    static const char *icons[RUNTIME_HOURLY_COUNT] = {
        "pc", "su", "su", "su", "pc", "su", "pc", "su"
    };
    if (!out) return;
    phase = (timer_ticks() / 30000U) % 4U;
    out->location = "Seoul";
    out->location_full = "Seoul, Korea";
    out->temperature_c = 23 + (int)phase;
    out->high_c = out->temperature_c + 4;
    out->low_c = out->temperature_c - 7;
    out->condition = (phase == 0) ? "Partly Cloudy" : "Sunny";
    out->tomorrow = (phase == 0) ? "Sunny tomorrow" : "Clear tomorrow";
    for (i = 0; i < RUNTIME_WEATHER_DAYS; i++) {
        out->forecast[i].label = "";
        out->forecast[i].temp_c = out->temperature_c - i * 2;
        out->forecast[i].high_c = out->forecast[i].temp_c + 4;
        out->forecast[i].low_c = out->forecast[i].temp_c - 6;
        out->forecast[i].condition = i == 0 ? out->condition : "Sunny";
    }
    for (i = 0; i < RUNTIME_HOURLY_COUNT; i++) {
        out->hourly_label[i] = hours[i];
        out->hourly_icon[i] = icons[i];
        out->hourly_temp_c[i] = out->temperature_c + ((i < 4) ? i / 2 : 1 - (i - 4) / 2);
    }
}

void runtime_get_system_info(runtime_system_info_t *out) {
    uint32_t heap_total;
    if (!out) return;
    heap_total = total_heap_bytes();
    out->sysname = uts_sysname();
    out->nodename = uts_nodename();
    out->release = uts_release();
    out->version = uts_version();
    out->machine = uts_machine();
    out->cpu_model = "QEMU i386";
    out->display = "BGA framebuffer";
    out->boot_loader = "GRUB Multiboot1";
    out->cpu_count = 1;
    out->uptime_seconds = timer_ticks() / 1000U;
    out->timer_hz = timer_hz();
    out->pmm_total_pages = pmm_managed_pages();
    out->pmm_free_pages = pmm_free_pages();
    out->pmm_total_bytes = out->pmm_total_pages * PAGE_SIZE;
    out->pmm_free_bytes = out->pmm_free_pages * PAGE_SIZE;
    out->heap_used_bytes = (uint32_t)heap_used_bytes();
    out->heap_free_bytes = (uint32_t)heap_free_bytes();
    out->heap_total_bytes = heap_total;
    out->heap_block_count = heap_block_count();
    out->heap_free_block_count = heap_free_block_count();
    out->process_count = process_count();
    out->task_count = task_count_active();
    out->task_free_slots = task_free_slots();
    out->driver_count = driver_count();
    out->pci_count = pci_count();
    out->block_count = block_count();
    out->display_width = VGA_WIDTH;
    out->display_height = VGA_HEIGHT;
    out->display_bpp = 32;
    out->cpu_load_percent = 35 + (int)((timer_ticks() / 300U) % 30U);
    out->mem_used_percent = percent_u32(out->heap_used_bytes, heap_total);
}

int runtime_get_storage_info(const char *path, runtime_storage_info_t *out) {
    vfs_fsinfo_t fs;
    uint32_t total;
    uint32_t freeb;
    if (!out) return -1;
    if (vfs_statfs(path ? path : "/", &fs) < 0) return -1;
    r_copy(out->name, sizeof(out->name), fs.name);
    out->block_size = fs.block_size;
    out->total_blocks = fs.total_blocks;
    out->free_blocks = fs.free_blocks;
    out->used_blocks = fs.total_blocks >= fs.free_blocks ? fs.total_blocks - fs.free_blocks : 0;
    total = fs.block_size * fs.total_blocks;
    freeb = fs.block_size * fs.free_blocks;
    out->total_bytes = total;
    out->free_bytes = freeb;
    out->used_bytes = total >= freeb ? total - freeb : 0;
    out->used_percent = percent_u32(out->used_blocks, out->total_blocks);
    return 0;
}
