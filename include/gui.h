#ifndef GUI_H
#define GUI_H
#include <stdint.h>

#define MAX_WINDOWS  16
#define MAX_BUTTONS  32

/* -----------------------------------------------------------------------
 * Window descriptor
 * --------------------------------------------------------------------- */
typedef struct {
    int x, y, w, h;
    const char *title;
    int visible;
    int focused;
    int drag_x, drag_y;   /* drag offset within title bar */
    int dragging;
    int maximized;
    int orig_x, orig_y, orig_w, orig_h;
    int resizing;
    int min_w, min_h;
    int space;   /* 0 = all spaces, 1-4 = specific space */
} gui_window_t;

/* -----------------------------------------------------------------------
 * Button descriptor
 * --------------------------------------------------------------------- */
typedef struct {
    int x, y, w, h;
    const char *label;
    uint32_t color;        /* background color */
    uint32_t text_color;
    int pressed;
    int hover;
    void (*on_click)(int id);
    int id;
    int win_idx;           /* which window owns this button (-1 = global) */
} gui_button_t;

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */
void gui_init(void);
void gui_run(void);

/* Primitive helpers */
void gui_draw_circle(int cx, int cy, int r, uint32_t color);
void gui_draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);
void gui_draw_rounded_rect_outline(int x, int y, int w, int h, int r, uint32_t color);

/* Scene components */
void gui_draw_desktop(void);
void gui_draw_menubar(void);
void gui_draw_dock(void);
void gui_draw_window(int idx);
void gui_draw_button(const gui_button_t *btn);
void gui_draw_cursor(int x, int y);

/* Hit-testing */
int gui_button_hit(const gui_button_t *btn, int x, int y);

#endif /* GUI_H */