#ifndef TTY_H
#define TTY_H

#include <stdint.h>

#define TTY_BUF_SIZE 2048
#define TTY_MODE_ECHO  0x1U
#define TTY_MODE_CANON 0x2U
#define TTY_COLUMNS 80U
#define TTY_ROWS    25U

void tty_init(void);
void tty_putc(char c);
void tty_write(const char *s);
void tty_write_buf(const char *s, uint32_t len);
void tty_input_char(char c);
int  tty_read(char *buf, uint32_t max);
uint32_t tty_get_mode(void);
int tty_set_mode(uint32_t mode);
int tty_read_ready(void);
const char *tty_buffer(void);
uint32_t tty_size(void);
int tty_output_read(uint32_t *pos, char *buf, uint32_t max);

#endif
