#include "keyboard.h"
#include "io.h"
#include "pic.h"
#include "idt.h"
#include "tty.h"

/* PS/2 keyboard ports */
#define KB_DATA_PORT   0x60
#define KB_STATUS_PORT 0x64

/* Circular ring buffer */
#define KB_BUF_SIZE 64
static volatile uint8_t kb_buf[KB_BUF_SIZE];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;

/* Shift state bitmask: bit 0 = left, bit 1 = right. */
static volatile int shift_state = 0;
/* Ctrl state bitmask: bit 0 = left, bit 1 = right. */
static volatile int ctrl_state = 0;

static uint32_t keyboard_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\npop %0\ncli" : "=r"(flags) : : "memory");
    return flags;
}

static void keyboard_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0\npopfl" : : "r"(flags) : "memory");
}

/* Scancode set 1, unshifted, indexed 0x00-0x39 */
static const uint8_t sc_table[0x3A] = {
    /* 0x00 */ 0,
    /* 0x01 */ KEY_ESC,
    /* 0x02 */ '1',
    /* 0x03 */ '2',
    /* 0x04 */ '3',
    /* 0x05 */ '4',
    /* 0x06 */ '5',
    /* 0x07 */ '6',
    /* 0x08 */ '7',
    /* 0x09 */ '8',
    /* 0x0A */ '9',
    /* 0x0B */ '0',
    /* 0x0C */ '-',
    /* 0x0D */ '=',
    /* 0x0E */ KEY_BACKSPACE,
    /* 0x0F */ '\t',
    /* 0x10 */ 'q',
    /* 0x11 */ 'w',
    /* 0x12 */ 'e',
    /* 0x13 */ 'r',
    /* 0x14 */ 't',
    /* 0x15 */ 'y',
    /* 0x16 */ 'u',
    /* 0x17 */ 'i',
    /* 0x18 */ 'o',
    /* 0x19 */ 'p',
    /* 0x1A */ '[',
    /* 0x1B */ ']',
    /* 0x1C */ KEY_ENTER,
    /* 0x1D */ 0,        /* ctrl */
    /* 0x1E */ 'a',
    /* 0x1F */ 's',
    /* 0x20 */ 'd',
    /* 0x21 */ 'f',
    /* 0x22 */ 'g',
    /* 0x23 */ 'h',
    /* 0x24 */ 'j',
    /* 0x25 */ 'k',
    /* 0x26 */ 'l',
    /* 0x27 */ ';',
    /* 0x28 */ '\'',
    /* 0x29 */ '`',
    /* 0x2A */ 0,        /* left shift */
    /* 0x2B */ '\\',
    /* 0x2C */ 'z',
    /* 0x2D */ 'x',
    /* 0x2E */ 'c',
    /* 0x2F */ 'v',
    /* 0x30 */ 'b',
    /* 0x31 */ 'n',
    /* 0x32 */ 'm',
    /* 0x33 */ ',',
    /* 0x34 */ '.',
    /* 0x35 */ '/',
    /* 0x36 */ 0,        /* right shift */
    /* 0x37 */ '*',
    /* 0x38 */ 0,        /* alt */
    /* 0x39 */ ' '
};

/* Scancode set 1, shifted, indexed 0x00-0x39 */
static const uint8_t sc_shifted[0x3A] = {
    /* 0x00 */ 0,
    /* 0x01 */ KEY_ESC,
    /* 0x02 */ '!',
    /* 0x03 */ '@',
    /* 0x04 */ '#',
    /* 0x05 */ '$',
    /* 0x06 */ '%',
    /* 0x07 */ '^',
    /* 0x08 */ '&',
    /* 0x09 */ '*',
    /* 0x0A */ '(',
    /* 0x0B */ ')',
    /* 0x0C */ '_',
    /* 0x0D */ '+',
    /* 0x0E */ KEY_BACKSPACE,
    /* 0x0F */ '\t',
    /* 0x10 */ 'Q',
    /* 0x11 */ 'W',
    /* 0x12 */ 'E',
    /* 0x13 */ 'R',
    /* 0x14 */ 'T',
    /* 0x15 */ 'Y',
    /* 0x16 */ 'U',
    /* 0x17 */ 'I',
    /* 0x18 */ 'O',
    /* 0x19 */ 'P',
    /* 0x1A */ '{',
    /* 0x1B */ '}',
    /* 0x1C */ KEY_ENTER,
    /* 0x1D */ 0,        /* ctrl */
    /* 0x1E */ 'A',
    /* 0x1F */ 'S',
    /* 0x20 */ 'D',
    /* 0x21 */ 'F',
    /* 0x22 */ 'G',
    /* 0x23 */ 'H',
    /* 0x24 */ 'J',
    /* 0x25 */ 'K',
    /* 0x26 */ 'L',
    /* 0x27 */ ':',
    /* 0x28 */ '"',
    /* 0x29 */ '~',
    /* 0x2A */ 0,        /* left shift */
    /* 0x2B */ '|',
    /* 0x2C */ 'Z',
    /* 0x2D */ 'X',
    /* 0x2E */ 'C',
    /* 0x2F */ 'V',
    /* 0x30 */ 'B',
    /* 0x31 */ 'N',
    /* 0x32 */ 'M',
    /* 0x33 */ '<',
    /* 0x34 */ '>',
    /* 0x35 */ '?',
    /* 0x36 */ 0,        /* right shift */
    /* 0x37 */ '*',
    /* 0x38 */ 0,        /* alt */
    /* 0x39 */ ' '
};

static void kb_buf_push(uint8_t c) {
    int next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {   /* drop if full */
        kb_buf[kb_head] = c;
        kb_head = next;
    }
    if ((c >= 0x20 && c < 0x7F) || c == '\n' || c == '\t' || c == KEY_BACKSPACE) {
        tty_input_char((char)c);
    }
}

/*
 * Extended scancode prefix: 0xE0 arrives as a separate IRQ before the
 * actual extended scancode.  We track whether we are in an extended
 * sequence with this flag.
 */
static volatile int extended = 0;

static void keyboard_irq(void) {
    uint8_t sc = inb(KB_DATA_PORT);

    /* Extended scancode prefix */
    if (sc == 0xE0) {
        extended = 1;
        pic_eoi(1);
        return;
    }

    int released = (sc & 0x80) != 0;
    uint8_t key  = sc & 0x7F;

    if (extended) {
        extended = 0;
        if (key == 0x1D) {
            if (released) ctrl_state &= ~2;
            else ctrl_state |= 2;
            pic_eoi(1);
            return;
        }
        if (!released) {
            uint8_t ch = 0;
            switch (key) {
                case 0x48: ch = ctrl_state ? 0xC0 : KEY_UP;    break; /* Ctrl+Up=tile-full */
                case 0x50: ch = ctrl_state ? 0xC1 : KEY_DOWN;  break; /* Ctrl+Down=restore */
                case 0x4B: ch = ctrl_state ? 0xC2 : KEY_LEFT;  break; /* Ctrl+Left=tile-left */
                case 0x4D: ch = ctrl_state ? 0xC3 : KEY_RIGHT; break; /* Ctrl+Right=tile-right */
                case 0x49: ch = KEY_PGUP;  break;
                case 0x51: ch = KEY_PGDN;  break;
                case 0x47: ch = KEY_HOME;  break;
                case 0x4F: ch = KEY_END;   break;
                case 0x52: ch = 0xFD;      break; /* Insert = Writing Tools */
                case 0x53: ch = 0xFE;      break; /* Delete = Quick Note */
                default:   break;
            }
            if (ch) kb_buf_push(ch);
        }
        pic_eoi(1);
        return;
    }

    /* Modifier tracking */
    if (key == 0x2A || key == 0x36) {
        int bit = (key == 0x2A) ? 1 : 2;
        if (released) shift_state &= ~bit;
        else shift_state |= bit;
        pic_eoi(1);
        return;
    }
    if (key == 0x1D) {
        if (released) ctrl_state &= ~1;
        else ctrl_state |= 1;
        pic_eoi(1);
        return;
    }

    /* Function keys F1-F10 (scancodes 0x3B-0x44) → custom codes 0xF1-0xFA */
    if (!released && key >= 0x3B && key <= 0x44) {
        kb_buf_push((uint8_t)(0xF0 + (key - 0x3B + 1)));
        pic_eoi(1);
        return;
    }
    /* F11 (0x57) → 0xFB, F12 (0x58) → 0xFC */
    if (!released && (key == 0x57 || key == 0x58)) {
        kb_buf_push(key == 0x57 ? 0xFB : 0xFC);
        pic_eoi(1);
        return;
    }

    if (!released && key < 0x3A) {
        int shifted = shift_state != 0;
        uint8_t ch = shifted ? sc_shifted[key] : sc_table[key];
        /* Ctrl+Shift+letter uses a range separate from navigation keys. */
        if (ctrl_state && shifted && ch >= 'A' && ch <= 'Z') {
            kb_buf_push((uint8_t)(KEY_CTRL_SHIFT_ALPHA_BASE + (ch - 'A')));
            pic_eoi(1);
            return;
        }
        if (ctrl_state && ch >= 'a' && ch <= 'z') ch = (uint8_t)(ch - 'a' + 1); /* Ctrl+A=1 */
        if (ctrl_state && ch >= 'A' && ch <= 'Z') ch = (uint8_t)(ch - 'A' + 1);
        /* Ctrl+digit: Space switching shortcuts. */
        if (ctrl_state && ch >= '1' && ch <= '9') ch = (uint8_t)(KEY_CTRL_DIGIT_BASE + (ch - '0'));
        /* Ctrl+backslash = 0x1C (snake/custom), Ctrl+] = 0x1D (breakout) */
        if (ctrl_state && ch == '\\') ch = 0x1C;
        if (ctrl_state && ch == ']')  ch = 0x1D;
        if (ch) kb_buf_push(ch);
    }

    pic_eoi(1);
}

void keyboard_init(void) {
    irq_install_handler(1, keyboard_irq);
    pic_unmask(1);
}

int keyboard_poll(void) {
    uint8_t c;
    uint32_t flags = keyboard_irq_save();
    if (kb_head == kb_tail) {
        keyboard_irq_restore(flags);
        return KEY_NONE;
    }
    c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    keyboard_irq_restore(flags);
    return (int)c;
}
