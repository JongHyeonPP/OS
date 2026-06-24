#include "uts.h"

static char g_nodename[UTS_FIELD_MAX] = "myos-machine";

static uint32_t uts_len(const char *s, uint32_t max) {
    uint32_t n = 0;
    while (s && n < max && s[n]) n++;
    return n;
}

const char *uts_sysname(void) {
    return "MyOS";
}

const char *uts_nodename(void) {
    return g_nodename;
}

const char *uts_release(void) {
    return "0.1";
}

const char *uts_version(void) {
    return "kernel-dev";
}

const char *uts_machine(void) {
    return "i386";
}

int uts_set_nodename(const char *name) {
    uint32_t len = uts_len(name, UTS_FIELD_MAX);
    uint32_t i;
    if (!name || len == 0 || len >= UTS_FIELD_MAX) return -1;
    for (i = 0; i < len; i++) {
        char ch = name[i];
        if (ch <= ' ' || ch == '/' || ch == '\\') return -1;
    }
    for (i = 0; i <= len; i++) g_nodename[i] = name[i];
    return 0;
}

int uts_copy_nodename(char *out, uint32_t max) {
    uint32_t len = uts_len(g_nodename, UTS_FIELD_MAX);
    uint32_t i;
    if (!out || max <= len) return -1;
    for (i = 0; i <= len; i++) out[i] = g_nodename[i];
    return (int)len;
}
