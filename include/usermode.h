#ifndef USERMODE_H
#define USERMODE_H

#include <stdint.h>

int usermode_enter(uint32_t entry, uint32_t user_stack);

#endif
