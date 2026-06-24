#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>

void debug_init(void);
void debug_putc(char c);
void debug_puts(const char *s);
void debug_hex32(uint32_t v);
void debug_dec(uint32_t v);
const char *debug_log_buffer(void);
uint32_t debug_log_size(void);
int debug_log_read(uint32_t *pos, char *buf, uint32_t len);
void panic(const char *msg);
void kassert_fail(const char *expr, const char *file, int line);

#define KASSERT(expr) do { if (!(expr)) kassert_fail(#expr, __FILE__, __LINE__); } while (0)

#endif
