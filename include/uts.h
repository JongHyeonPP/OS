#ifndef UTS_H
#define UTS_H

#include <stdint.h>

#define UTS_FIELD_MAX 32

const char *uts_sysname(void);
const char *uts_nodename(void);
const char *uts_release(void);
const char *uts_version(void);
const char *uts_machine(void);
int uts_set_nodename(const char *name);
int uts_copy_nodename(char *out, uint32_t max);

#endif
