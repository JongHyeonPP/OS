#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>

#define KEY_UP        0x80
#define KEY_DOWN      0x81
#define KEY_LEFT      0x82
#define KEY_RIGHT     0x83
#define KEY_PGUP      0x84
#define KEY_PGDN      0x85
#define KEY_HOME      0x86
#define KEY_END       0x87
#define KEY_CTRL_SHIFT_ALPHA_BASE 0xA0
#define KEY_CTRL_DIGIT_BASE       0xD0
#define KEY_ESC       0x1B
#define KEY_ENTER     '\n'
#define KEY_BACKSPACE 0x08
#define KEY_NONE      0

void keyboard_init(void);
int  keyboard_poll(void);   /* returns next char (0 if none), non-blocking */

#endif /* KEYBOARD_H */
