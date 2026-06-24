#ifndef MOUSE_H
#define MOUSE_H
#include <stdint.h>

#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
#define MOUSE_MIDDLE 0x04

void    mouse_init(void);
int     mouse_get_x(void);
int     mouse_get_y(void);
uint8_t mouse_get_buttons(void);
int     mouse_moved(void);    /* returns 1 if state changed since last poll, clears flag */
int     mouse_clicked(void);  /* returns button mask if click event, clears flag */

#endif /* MOUSE_H */