#ifndef VGA_H
#define VGA_H
#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH  800
#define VGA_HEIGHT 600

/* 32-bit RGB colors */
#define RGB(r,g,b)  ((uint32_t)(((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b)))

/* Mac-like color palette */
#define COLOR_BLACK                 RGB(0,0,0)
#define COLOR_WHITE                 RGB(255,255,255)
#define COLOR_DESKTOP_BG            RGB(58,110,165)
#define COLOR_MENUBAR_BG            RGB(236,236,236)
#define COLOR_MENUBAR_TEXT          RGB(30,30,30)
#define COLOR_WIN_BG                RGB(240,240,240)
#define COLOR_WIN_TITLEBAR          RGB(220,220,220)
#define COLOR_WIN_TITLEBAR_INACTIVE RGB(235,235,235)
#define COLOR_TITLEBAR_TEXT         RGB(50,50,50)
#define COLOR_BTN_RED               RGB(255,95,86)
#define COLOR_BTN_YELLOW            RGB(255,189,46)
#define COLOR_BTN_GREEN             RGB(39,201,63)
#define COLOR_BTN_FACE              RGB(200,200,200)
#define COLOR_BTN_HOVER             RGB(180,180,210)
#define COLOR_BORDER                RGB(180,180,180)
#define COLOR_SHADOW                RGB(100,100,100)
#define COLOR_TEXT                  RGB(20,20,20)
#define COLOR_TEXT_GRAY             RGB(120,120,120)
#define COLOR_DOCK_BG               RGB(255,255,255)
#define COLOR_ACCENT                RGB(0,122,255)
#define COLOR_CURSOR                RGB(0,0,0)

/* Legacy aliases used by gui.c */
#define COLOR_TITLEBAR      COLOR_WIN_TITLEBAR
#define COLOR_BORDER_LIGHT  RGB(230,230,230)
#define COLOR_BORDER_DARK   RGB(130,130,130)
#define COLOR_BTN_PRESS     RGB(160,160,160)
#define COLOR_TASKBAR       RGB(210,210,210)
#define COLOR_CYAN          RGB(0,200,200)
#define COLOR_RED           RGB(200,50,50)
#define COLOR_YELLOW        RGB(220,180,0)
#define COLOR_GREEN         RGB(50,180,50)
#define COLOR_BLUE          RGB(50,50,200)
#define COLOR_MAGENTA       RGB(180,50,180)
#define COLOR_LRED          RGB(255,80,80)
#define COLOR_LGRAY         RGB(180,180,180)

void vga_init(void);
void vga_set_fb_override(uint32_t addr, uint32_t pitch); /* hint from GRUB */
void vga_flip(void);
void vga_put_pixel(int x, int y, uint32_t color);
uint32_t vga_get_pixel(int x, int y);
void vga_fill_rect(int x, int y, int w, int h, uint32_t color);
void vga_draw_hline(int x, int y, int len, uint32_t color);
void vga_draw_vline(int x, int y, int len, uint32_t color);
void vga_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void vga_draw_line_thick(int x0, int y0, int x1, int y1, int t, uint32_t color);
void vga_draw_rect_outline(int x, int y, int w, int h, uint32_t color);
void vga_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void vga_draw_char_trans(int x, int y, char c, uint32_t fg);
void vga_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void vga_draw_string_trans(int x, int y, const char *s, uint32_t fg);
/* Keep old name as alias */
#define vga_draw_string_transparent vga_draw_string_trans
void vga_blend_pixel(int x, int y, uint32_t color, uint8_t alpha);
void vga_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, uint8_t alpha);
void vga_blit(int x, int y, int w, int h, const uint32_t *data);
#endif