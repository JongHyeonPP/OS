/*
 * vga.c  -  32-bit 800x600 framebuffer driver
 *
 * Uses the Bochs VBE dispi interface (exposed by QEMU -vga std) to set
 * 800x600x32bpp mode, then writes to the linear framebuffer at 0xE0000000
 * (the standard QEMU VBE LFB window).
 *
 * Double-buffering: all drawing goes into a RAM back-buffer; vga_flip()
 * copies it to the real hardware framebuffer.
 */
#include "vga.h"
#include "debug.h"
#include "font.h"
#include "io.h"
#include "paging.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Bochs VBE dispi registers
 * ======================================================================= */
#define VBE_DISPI_IOPORT_INDEX   0x01CE
#define VBE_DISPI_IOPORT_DATA    0x01CF

#define VBE_DISPI_INDEX_ID       0
#define VBE_DISPI_INDEX_XRES     1
#define VBE_DISPI_INDEX_YRES     2
#define VBE_DISPI_INDEX_BPP      3
#define VBE_DISPI_INDEX_ENABLE   4
#define VBE_DISPI_INDEX_BANK     5
#define VBE_DISPI_INDEX_VIRT_W   6
#define VBE_DISPI_INDEX_VIRT_H   7
#define VBE_DISPI_INDEX_X_OFF    8
#define VBE_DISPI_INDEX_Y_OFF    9

#define VBE_DISPI_DISABLED       0x00
#define VBE_DISPI_ENABLED        0x01
#define VBE_DISPI_LFB_ENABLED    0x40
#define VBE_DISPI_NOCLEARMEM     0x80

/* PCI config space ports */
#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC

/* Detect BGA framebuffer address via PCI BAR0 of device 00:02.0 */
static uint32_t vbe_lfb_addr    = 0xE0000000u;  /* fallback default */
static uint32_t vbe_lfb_pitch   = VGA_WIDTH * 4U;
static int      vbe_grub_set    = 0;            /* 1 if GRUB provided the fb */
static int      vbe_lfb_mapped  = 0;

static int clip_axis(int start, int len, int limit, int *lo, int *hi) {
    int64_t end;
    if (!lo || !hi || len <= 0 || limit <= 0) return 0;
    end = (int64_t)start + (int64_t)len;
    if (end <= 0 || start >= limit) return 0;
    *lo = start < 0 ? 0 : start;
    *hi = end > limit ? limit : (int)end;
    return *lo < *hi;
}

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg) {
    outl(PCI_ADDR, 0x80000000u | ((uint32_t)bus<<16) | ((uint32_t)dev<<11) | ((uint32_t)func<<8) | (reg & 0xFC));
    return inl(PCI_DATA);
}

/* Called by kernel_main() if GRUB passed framebuffer info via Multiboot */
void vga_set_fb_override(uint32_t addr, uint32_t pitch) {
    if (addr && pitch >= VGA_WIDTH * 4) {
        vbe_lfb_addr = addr;
        vbe_lfb_pitch = pitch;
        vbe_grub_set = 1;
    }
}

static void vbe_detect_lfb(void) {
    /* VGA PCI device is at 00:02.0 in QEMU default PC machine */
    uint32_t bar0 = pci_read32(0, 2, 0, 0x10) & ~0xFu;
    if (bar0 != 0 && bar0 != 0xFFFFFFF0u)
        vbe_lfb_addr = bar0;
    /* else keep 0xE0000000 fallback */
}

/* =========================================================================
 * Back buffer (in kernel data segment, ~1.8 MB for 800x600x32)
 * ======================================================================= */
static uint32_t backbuf[VGA_WIDTH * VGA_HEIGHT];

/* =========================================================================
 * Font data (lives here so font.h works without a separate .c unit)
 * ======================================================================= */
const uint8_t font8x8[128][8] = {
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    /* 0x20 = space */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},
    /* A-Z */
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
    /* [ \ ] ^ _ ` */
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},
    /* a-z */
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00},
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E},
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},
    {0x00,0x00,0x1E,0x03,0x1E,0x30,0x1F,0x00},
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},
    /* { | } ~ DEL */
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

/* =========================================================================
 * Bochs VBE helper
 * ======================================================================= */
static void vbe_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA,  value);
}

static void vga_set_vbe_mode(void) {
    /* Disable VBE before changing settings */
    vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    vbe_write(VBE_DISPI_INDEX_XRES,   VGA_WIDTH);
    vbe_write(VBE_DISPI_INDEX_YRES,   VGA_HEIGHT);
    vbe_write(VBE_DISPI_INDEX_BPP,    32);
    vbe_write(VBE_DISPI_INDEX_VIRT_W, VGA_WIDTH);
    vbe_write(VBE_DISPI_INDEX_VIRT_H, VGA_HEIGHT);
    vbe_write(VBE_DISPI_INDEX_X_OFF,  0);
    vbe_write(VBE_DISPI_INDEX_Y_OFF,  0);
    /* Enable with LFB, keep memory (NOCLEARMEM for speed) */
    vbe_write(VBE_DISPI_INDEX_ENABLE,
              VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM);
}

/* =========================================================================
 * vga_init
 * ======================================================================= */
void vga_init(void) {
    int i;
    uint32_t map_bytes = 0;
    if (!vbe_grub_set) {
        vbe_detect_lfb();
        vga_set_vbe_mode();
        {
            uint32_t bar0 = pci_read32(0, 2, 0, 0x10) & ~0xFu;
            if (bar0 && bar0 != 0xFFFFFFF0u) vbe_lfb_addr = bar0;
        }
    }
    if (vbe_lfb_pitch < VGA_WIDTH * 4U ||
        vbe_lfb_pitch > (0xFFFFFFFFU - 0x1000U) / VGA_HEIGHT) {
            vbe_lfb_mapped = 0;
    } else {
        map_bytes = vbe_lfb_pitch * VGA_HEIGHT + 0x1000U;
        if (paging_map_range(vbe_lfb_addr, vbe_lfb_addr, map_bytes, PAGE_WRITE | PAGE_PCD) < 0) {
                    vbe_lfb_mapped = 0;
        } else {
            vbe_lfb_mapped = 1;
        }
    }
    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        backbuf[i] = COLOR_DESKTOP_BG;
    vga_flip();
}

/* =========================================================================
 * vga_flip  -  blit back buffer to hardware LFB
 * ======================================================================= */
void vga_flip(void) {
    volatile uint8_t *fb = (volatile uint8_t *)vbe_lfb_addr;
    int x, y;
    if (!vbe_lfb_mapped) return;
    for (y = 0; y < VGA_HEIGHT; y++) {
        volatile uint32_t *row = (volatile uint32_t *)(fb + (uint32_t)y * vbe_lfb_pitch);
        for (x = 0; x < VGA_WIDTH; x++)
            row[x] = backbuf[y * VGA_WIDTH + x];
    }
}

/* =========================================================================
 * Pixel operations
 * ======================================================================= */
void vga_put_pixel(int x, int y, uint32_t color) {
    if ((unsigned)x < VGA_WIDTH && (unsigned)y < VGA_HEIGHT)
        backbuf[y * VGA_WIDTH + x] = color;
}

uint32_t vga_get_pixel(int x, int y) {
    if ((unsigned)x < VGA_WIDTH && (unsigned)y < VGA_HEIGHT)
        return backbuf[y * VGA_WIDTH + x];
    return 0;
}

void vga_fill_rect(int x, int y, int w, int h, uint32_t color) {
    int cx, cy;
    int x0, x1, y0, y1;
    if (!clip_axis(x, w, VGA_WIDTH, &x0, &x1) ||
        !clip_axis(y, h, VGA_HEIGHT, &y0, &y1)) return;
    for (cy = y0; cy < y1; cy++)
        for (cx = x0; cx < x1; cx++)
            backbuf[cy * VGA_WIDTH + cx] = color;
}

void vga_draw_hline(int x, int y, int len, uint32_t color) {
    vga_fill_rect(x, y, len, 1, color);
}

void vga_draw_vline(int x, int y, int len, uint32_t color) {
    vga_fill_rect(x, y, 1, len, color);
}

void vga_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int64_t dx64 = (int64_t)x1 - (int64_t)x0;
    int64_t dy64 = (int64_t)y1 - (int64_t)y0;
    int dx, dy;
    if (dx64 < 0) dx64 = -dx64;
    if (dy64 < 0) dy64 = -dy64;
    if (dx64 > 4096 || dy64 > 4096) return;
    dx = (int)dx64;
    dy = (int)dy64;
    int sx = (x1 >= x0) ? 1 : -1;
    int sy = (y1 >= y0) ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        vga_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void vga_draw_line_thick(int x0, int y0, int x1, int y1, int t, uint32_t color) {
    int i, j;
    if (t < 1) t = 1;
    if (t > 64) t = 64;
    for (i = -t/2; i <= t/2; i++)
        for (j = -t/2; j <= t/2; j++)
            vga_draw_line(x0+i, y0+j, x1+i, y1+j, color);
}

void vga_draw_rect_outline(int x, int y, int w, int h, uint32_t color) {
    vga_draw_hline(x,       y,       w, color);
    vga_draw_hline(x,       y+h-1,   w, color);
    vga_draw_vline(x,       y,       h, color);
    vga_draw_vline(x+w-1,   y,       h, color);
}

/* =========================================================================
 * Text rendering
 * ======================================================================= */
void vga_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    int row, col;
    uint8_t idx = (uint8_t)c;
    if (idx >= 128) idx = '?';
    for (row = 0; row < 8; row++) {
        uint8_t bits = font8x8[idx][row];
        for (col = 0; col < 8; col++)
            vga_put_pixel(x + col, y + row, (bits & (1u << col)) ? fg : bg);
    }
}

void vga_draw_char_trans(int x, int y, char c, uint32_t fg) {
    int row, col;
    uint8_t idx = (uint8_t)c;
    if (idx >= 128) idx = '?';
    for (row = 0; row < 8; row++) {
        uint8_t bits = font8x8[idx][row];
        for (col = 0; col < 8; col++)
            if (bits & (1u << col))
                vga_put_pixel(x + col, y + row, fg);
    }
}

void vga_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    int i;
    if (!s) return;
    for (i = 0; s[i]; i++)
        vga_draw_char(x + i * 8, y, s[i], fg, bg);
}

void vga_draw_string_trans(int x, int y, const char *s, uint32_t fg) {
    int i;
    if (!s) return;
    for (i = 0; s[i]; i++)
        vga_draw_char_trans(x + i * 8, y, s[i], fg);
}

/* =========================================================================
 * Alpha blending
 *
 * vga_blend_pixel: alpha=255 opaque, alpha=0 transparent
 * ======================================================================= */
void vga_blend_pixel(int x, int y, uint32_t color, uint8_t alpha) {
    if ((unsigned)x >= VGA_WIDTH || (unsigned)y >= VGA_HEIGHT) return;
    uint32_t dst = backbuf[y * VGA_WIDTH + x];
    /* Extract RGB channels of source and destination */
    uint32_t sr = (color >> 16) & 0xFF;
    uint32_t sg = (color >>  8) & 0xFF;
    uint32_t sb =  color        & 0xFF;
    uint32_t dr = (dst   >> 16) & 0xFF;
    uint32_t dg = (dst   >>  8) & 0xFF;
    uint32_t db =  dst          & 0xFF;
    uint32_t a  = (uint32_t)alpha;
    uint32_t ia = 255u - a;
    uint32_t or_ = (sr * a + dr * ia) / 255u;
    uint32_t og  = (sg * a + dg * ia) / 255u;
    uint32_t ob  = (sb * a + db * ia) / 255u;
    backbuf[y * VGA_WIDTH + x] = (or_ << 16) | (og << 8) | ob;
}

void vga_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    int cx, cy;
    int x0, x1, y0, y1;
    if (!clip_axis(x, w, VGA_WIDTH, &x0, &x1) ||
        !clip_axis(y, h, VGA_HEIGHT, &y0, &y1)) return;
    for (cy = y0; cy < y1; cy++)
        for (cx = x0; cx < x1; cx++)
            vga_blend_pixel(cx, cy, color, alpha);
}

/* =========================================================================
 * Blit a pre-rendered RGBA/RGB rectangle (32-bit pixels)
 * ======================================================================= */
void vga_blit(int x, int y, int w, int h, const uint32_t *data) {
    int cx, cy;
    int x0, x1, y0, y1;
    if (!data || w <= 0 || h <= 0 || w > 4096 || h > 4096 ||
        !clip_axis(x, w, VGA_WIDTH, &x0, &x1) ||
        !clip_axis(y, h, VGA_HEIGHT, &y0, &y1)) return;
    for (cy = y0; cy < y1; cy++)
        for (cx = x0; cx < x1; cx++)
            backbuf[cy * VGA_WIDTH + cx] = data[(cy - y) * w + (cx - x)];
}
