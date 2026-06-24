#include "tty.h"
#include "debug.h"

#define TTY_BACKSPACE 0x08

static char g_tty_buf[TTY_BUF_SIZE];
static uint32_t g_tty_head;
static uint32_t g_tty_base;
static char g_tty_in[TTY_BUF_SIZE];
static uint32_t g_tty_in_head;
static uint32_t g_tty_in_tail;
static uint32_t g_tty_mode;

static uint32_t tty_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\npop %0\ncli" : "=r"(flags) : : "memory");
    return flags;
}

static void tty_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0\npopfl" : : "r"(flags) : "memory");
}

static int tty_read_ready_locked(void) {
    uint32_t cursor;
    if (g_tty_in_tail == g_tty_in_head) return 0;
    if ((g_tty_mode & TTY_MODE_CANON) == 0) return 1;
    cursor = g_tty_in_tail;
    while (cursor != g_tty_in_head) {
        if (g_tty_in[cursor] == '\n') return 1;
        cursor = (cursor + 1U) % TTY_BUF_SIZE;
    }
    return 0;
}

void tty_init(void) {
    g_tty_head = 0;
    g_tty_base = 0;
    g_tty_buf[0] = 0;
    g_tty_in_head = 0;
    g_tty_in_tail = 0;
    g_tty_mode = TTY_MODE_ECHO | TTY_MODE_CANON;
}

void tty_putc(char c) {
    uint32_t flags;
    debug_putc(c);
    flags = tty_irq_save();
    if (g_tty_head + 1 >= TTY_BUF_SIZE) {
        uint32_t i;
        for (i = 0; i < TTY_BUF_SIZE / 2; i++) {
            g_tty_buf[i] = g_tty_buf[i + TTY_BUF_SIZE / 2];
        }
        g_tty_base += TTY_BUF_SIZE / 2;
        g_tty_head = TTY_BUF_SIZE / 2;
    }
    g_tty_buf[g_tty_head++] = c;
    g_tty_buf[g_tty_head] = 0;
    tty_irq_restore(flags);
}

void tty_write(const char *s) {
    if (!s) return;
    while (*s) {
        if (*s == '\n') tty_putc('\r');
        tty_putc(*s++);
    }
}

void tty_write_buf(const char *s, uint32_t len) {
    uint32_t i;
    if (!s) return;
    for (i = 0; i < len; i++) {
        if (s[i] == '\n') tty_putc('\r');
        tty_putc(s[i]);
    }
}

void tty_input_char(char c) {
    uint32_t flags = tty_irq_save();
    uint32_t next = (g_tty_in_head + 1) % TTY_BUF_SIZE;
    int echo = (g_tty_mode & TTY_MODE_ECHO) != 0;
    int erased = 0;
    int accepted = 0;
    if (c == TTY_BACKSPACE) {
        if (g_tty_in_head != g_tty_in_tail) {
            uint32_t prev = (g_tty_in_head + TTY_BUF_SIZE - 1U) % TTY_BUF_SIZE;
            if (g_tty_in[prev] != '\n') {
                g_tty_in_head = prev;
                erased = 1;
            }
        }
        tty_irq_restore(flags);
        if (echo && erased) tty_write_buf("\b \b", 3);
        return;
    }
    if (next == g_tty_in_tail) {
        tty_irq_restore(flags);
        return;
    }
    g_tty_in[g_tty_in_head] = c;
    g_tty_in_head = next;
    tty_irq_restore(flags);
    accepted = 1;
    if (echo && accepted) tty_write_buf(&c, 1);
}

int tty_read(char *buf, uint32_t max) {
    uint32_t i = 0;
    uint32_t flags;
    if (max == 0) return 0;
    if (!buf) return -1;
    flags = tty_irq_save();
    if ((g_tty_mode & TTY_MODE_CANON) && !tty_read_ready_locked()) {
        tty_irq_restore(flags);
        return 0;
    }
    while (i < max && g_tty_in_tail != g_tty_in_head) {
        char c = g_tty_in[g_tty_in_tail];
        g_tty_in_tail = (g_tty_in_tail + 1) % TTY_BUF_SIZE;
        buf[i++] = c;
        if (c == '\n') break;
    }
    tty_irq_restore(flags);
    return (int)i;
}

uint32_t tty_get_mode(void) {
    uint32_t flags = tty_irq_save();
    uint32_t mode = g_tty_mode;
    tty_irq_restore(flags);
    return mode;
}

int tty_set_mode(uint32_t mode) {
    uint32_t flags;
    if (mode & ~(TTY_MODE_ECHO | TTY_MODE_CANON)) return -1;
    flags = tty_irq_save();
    g_tty_mode = mode;
    tty_irq_restore(flags);
    return 0;
}

int tty_read_ready(void) {
    uint32_t flags = tty_irq_save();
    int ready = tty_read_ready_locked();
    tty_irq_restore(flags);
    return ready;
}

const char *tty_buffer(void) {
    return g_tty_buf;
}

uint32_t tty_size(void) {
    uint32_t flags = tty_irq_save();
    uint32_t size = g_tty_head;
    tty_irq_restore(flags);
    return size;
}

int tty_output_read(uint32_t *pos, char *buf, uint32_t max) {
    uint32_t flags;
    uint32_t start;
    uint32_t end;
    uint32_t n;
    uint32_t i;
    if (!pos || (!buf && max > 0)) return -1;
    if (max == 0) return 0;
    flags = tty_irq_save();
    start = *pos;
    end = g_tty_base + g_tty_head;
    if (start < g_tty_base || start > end) start = g_tty_base;
    n = end - start;
    if (n > max) n = max;
    for (i = 0; i < n; i++) buf[i] = g_tty_buf[start - g_tty_base + i];
    *pos = start + n;
    tty_irq_restore(flags);
    return (int)n;
}
