#include "debug.h"
#include "io.h"

#define COM1 0x3F8
#define DEBUG_LOG_SIZE 4096

static char g_debug_log[DEBUG_LOG_SIZE];
static uint32_t g_debug_log_len;
static uint32_t g_debug_log_base;

static uint32_t debug_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\npop %0\ncli" : "=r"(flags) : : "memory");
    return flags;
}

static void debug_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0\npopfl" : : "r"(flags) : "memory");
}

static void debug_log_append(char c) {
    uint32_t flags = debug_irq_save();
    if (g_debug_log_len + 1 >= DEBUG_LOG_SIZE) {
        uint32_t i;
        for (i = 0; i < DEBUG_LOG_SIZE / 2; i++) {
            g_debug_log[i] = g_debug_log[i + DEBUG_LOG_SIZE / 2];
        }
        g_debug_log_base += DEBUG_LOG_SIZE / 2;
        g_debug_log_len = DEBUG_LOG_SIZE / 2;
    }
    g_debug_log[g_debug_log_len++] = c;
    g_debug_log[g_debug_log_len] = 0;
    debug_irq_restore(flags);
}

void debug_init(void) {
    g_debug_log_len = 0;
    g_debug_log_base = 0;
    g_debug_log[0] = 0;
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void debug_putc(char c) {
    uint32_t timeout = 100000;
    while (timeout-- && !(inb(COM1 + 5) & 0x20)) {}
    if (inb(COM1 + 5) & 0x20) outb(COM1, (uint8_t)c);
    debug_log_append(c);
}

void debug_puts(const char *s) {
    if (!s) return;
    while (*s) {
        if (*s == '\n') debug_putc('\r');
        debug_putc(*s++);
    }
}

void debug_hex32(uint32_t v) {
    static const char h[] = "0123456789ABCDEF";
    int i;
    debug_puts("0x");
    for (i = 28; i >= 0; i -= 4) debug_putc(h[(v >> i) & 0xF]);
}

void debug_dec(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) {
        debug_putc('0');
        return;
    }
    while (v && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) debug_putc(buf[--i]);
}

const char *debug_log_buffer(void) {
    return g_debug_log;
}

uint32_t debug_log_size(void) {
    uint32_t flags = debug_irq_save();
    uint32_t size = g_debug_log_len;
    debug_irq_restore(flags);
    return size;
}

int debug_log_read(uint32_t *pos, char *buf, uint32_t len) {
    uint32_t flags;
    uint32_t start;
    uint32_t end;
    uint32_t n;
    uint32_t i;
    if (!pos || (!buf && len > 0)) return -1;
    if (len == 0) return 0;
    flags = debug_irq_save();
    start = *pos;
    end = g_debug_log_base + g_debug_log_len;
    if (start < g_debug_log_base || start > end) start = g_debug_log_base;
    n = end - start;
    if (n > len) n = len;
    for (i = 0; i < n; i++) buf[i] = g_debug_log[start - g_debug_log_base + i];
    *pos = start + n;
    debug_irq_restore(flags);
    return (int)n;
}

void panic(const char *msg) {
    debug_puts("\n[PANIC] ");
    debug_puts(msg ? msg : "(null)");
    debug_puts("\n");
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

void kassert_fail(const char *expr, const char *file, int line) {
    debug_puts("\n[ASSERT] ");
    debug_puts(expr);
    debug_puts(" at ");
    debug_puts(file);
    debug_putc(':');
    debug_dec((uint32_t)line);
    panic("assertion failed");
}
