#include "mouse.h"
#include "io.h"
#include "pic.h"
#include "idt.h"

/* PS/2 controller ports */
#define PS2_DATA    0x60
#define PS2_CMD     0x64
#define PS2_STATUS  0x64

/* Screen dimensions used for clamping */
#define SCREEN_W 800
#define SCREEN_H 600
#define PS2_WAIT_LIMIT 100000U

/* ---------- low-level PS/2 helpers ---------- */

static int ps2_wait_write(void) {
    uint32_t timeout = PS2_WAIT_LIMIT;
    while (timeout--) {
        if ((inb(PS2_STATUS) & 0x02) == 0) return 0;
    }
    return -1;
}

static int ps2_wait_read(void) {
    uint32_t timeout = PS2_WAIT_LIMIT;
    while (timeout--) {
        if (inb(PS2_STATUS) & 0x01) return 0;
    }
    return -1;
}

static int mouse_write(uint8_t data) {
    if (ps2_wait_write() < 0) return -1;
    outb(PS2_CMD, 0xD4);   /* route next byte to aux (mouse) device */
    if (ps2_wait_write() < 0) return -1;
    outb(PS2_DATA, data);
    return 0;
}

static int mouse_read(uint8_t *out) {
    if (!out || ps2_wait_read() < 0) return -1;
    *out = inb(PS2_DATA);
    return 0;
}

static int mouse_expect_ack(void) {
    uint8_t ack;
    if (mouse_read(&ack) < 0) return -1;
    return ack == 0xFA ? 0 : -1;
}

/* ---------- mouse state ---------- */

static uint8_t mouse_packet[3];
static int     packet_idx   = 0;
static int     mouse_x      = 400;   /* start at centre of 800x600 screen */
static int     mouse_y      = 300;
static uint8_t mouse_buttons = 0;
static int     mouse_dirty  = 0;
static int     mouse_click  = 0;

static uint32_t mouse_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\npop %0\ncli" : "=r"(flags) : : "memory");
    return flags;
}

static void mouse_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0\npopfl" : : "r"(flags) : "memory");
}

/* ---------- IRQ12 handler ---------- */

static void mouse_irq(void) {
    uint8_t data = inb(PS2_DATA);

    /* Synchronise on first byte: bit 3 must always be set */
    if (packet_idx == 0 && !(data & 0x08)) {
        pic_eoi(12);
        return;
    }

    mouse_packet[packet_idx++] = data;

    if (packet_idx == 3) {
        packet_idx = 0;

        uint8_t flags = mouse_packet[0];
        if (flags & 0xC0) {
            pic_eoi(12);
            return;
        }

        /* Sign-extend 9-bit deltas */
        int dx = (int)mouse_packet[1] - ((flags & 0x10) ? 256 : 0);
        int dy = (int)mouse_packet[2] - ((flags & 0x20) ? 256 : 0);

        mouse_x += dx;
        mouse_y -= dy;   /* Y is inverted: positive dy = up on screen = lower row index */

        /* Clamp to screen bounds */
        if (mouse_x < 0)          mouse_x = 0;
        if (mouse_x >= SCREEN_W)  mouse_x = SCREEN_W - 1;
        if (mouse_y < 0)          mouse_y = 0;
        if (mouse_y >= SCREEN_H)  mouse_y = SCREEN_H - 1;

        /* Button state change detection */
        uint8_t new_buttons = flags & 0x07;
        if (new_buttons != mouse_buttons) {
            /* Record newly-pressed buttons as a click event */
            uint8_t pressed = new_buttons & ~mouse_buttons;
            if (pressed) mouse_click = (int)pressed;
            mouse_buttons = new_buttons;
        }

        mouse_dirty = 1;
    }

    pic_eoi(12);
}

/* ---------- public API ---------- */

void mouse_init(void) {
    uint8_t status;
    /* Enable auxiliary (mouse) device */
    if (ps2_wait_write() < 0) return;
    outb(PS2_CMD, 0xA8);

    /* Enable IRQ12 via Compaq status byte bit 1 */
    if (ps2_wait_write() < 0) return;
    outb(PS2_CMD, 0x20);          /* read current status byte */
    if (ps2_wait_read() < 0) return;
    status = inb(PS2_DATA) | 0x02;
    if (ps2_wait_write() < 0) return;
    outb(PS2_CMD, 0x60);          /* write new status byte */
    if (ps2_wait_write() < 0) return;
    outb(PS2_DATA, status);

    /* Reset mouse to default settings */
    if (mouse_write(0xF6) < 0 || mouse_expect_ack() < 0) return;

    /* Enable data reporting (streaming mode) */
    if (mouse_write(0xF4) < 0 || mouse_expect_ack() < 0) return;

    irq_install_handler(12, mouse_irq);
    pic_unmask(12);
    pic_unmask(2);                /* IRQ2 is the cascade line; must be unmasked */
}

int mouse_get_x(void) {
    uint32_t flags = mouse_irq_save();
    int value = mouse_x;
    mouse_irq_restore(flags);
    return value;
}

int mouse_get_y(void) {
    uint32_t flags = mouse_irq_save();
    int value = mouse_y;
    mouse_irq_restore(flags);
    return value;
}

uint8_t mouse_get_buttons(void) {
    uint32_t flags = mouse_irq_save();
    uint8_t value = mouse_buttons;
    mouse_irq_restore(flags);
    return value;
}

int mouse_moved(void) {
    uint32_t flags = mouse_irq_save();
    int val = mouse_dirty;
    mouse_dirty = 0;
    mouse_irq_restore(flags);
    return val;
}

int mouse_clicked(void) {
    uint32_t flags = mouse_irq_save();
    int val = mouse_click;
    mouse_click = 0;
    mouse_irq_restore(flags);
    return val;
}
