#include "vfs.h"
#include "block.h"
#include "debug.h"
#include "driver.h"
#include "heap.h"
#include "idt.h"
#include "net.h"
#include "paging.h"
#include "pci.h"
#include "pmm.h"
#include "process.h"
#include "simplefs.h"
#include "task.h"
#include "tty.h"
#include "timer.h"
#include "uts.h"

#define RAMFS_MAX_NODES 160
#define RAMFS_FILE_CAP  4096
#define VFS_MAX_PIPES   4
#define VFS_PIPE_CAP    128
#define VFS_BACKEND_UDP4 4
#define VFS_UDP_EPHEMERAL_START 49152U

typedef struct {
    const char *path;
    vfs_node_type_t type;
    const uint8_t *data;
    uint32_t size;
} ram_seed_t;

typedef struct {
    int used;
    char path[VFS_MAX_PATH];
    vfs_node_type_t type;
    char data[RAMFS_FILE_CAP];
    uint32_t size;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t created_ms;
    uint32_t modified_ms;
    uint32_t accessed_ms;
} ram_node_t;

typedef struct {
    int used;
    uint32_t refcount;
    int backend;
    ram_node_t *node;
    int simple_fd;
    int pipe_id;
    int pipe_write;
    int udp_bound;
    int udp_peer_set;
    uint32_t udp_peer_ipv4;
    uint32_t udp_ifindex;
    uint16_t udp_port;
    uint16_t udp_peer_port;
    uint32_t pos;
    int flags;
    char path[VFS_MAX_PATH];
} vfs_fd_t;

typedef struct {
    int used;
    uint32_t head;
    uint32_t len;
    uint32_t readers;
    uint32_t writers;
    char data[VFS_PIPE_CAP];
} vfs_pipe_t;

static uint32_t vfs_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\npop %0\ncli" : "=r"(flags) : : "memory");
    return flags;
}

static void vfs_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0\npopfl" : : "r"(flags) : "memory");
}

static const uint8_t g_user_hello_elf[] = {
    0x7F, 'E',  'L',  'F',  0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x54, 0x80, 0x04, 0x08, 0x34, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x34, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00,
    0x54, 0x80, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x34, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0xB8, 0x04, 0x00, 0x00, 0x00,
    0xBB, 0x01, 0x00, 0x00, 0x00,
    0xB9, 0x73, 0x80, 0x04, 0x08,
    0xBA, 0x15, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xB8, 0x02, 0x00, 0x00, 0x00,
    0x31, 0xDB,
    0xCD, 0x80,
    'h',  'e',  'l',  'l',  'o',  ' ',  'f',  'r',
    'o',  'm',  ' ',  'u',  's',  'e',  'r',  ' ',
    'm',  'o',  'd',  'e',  '\n'
};

static const uint8_t g_user_hostname_elf[] = {
    0x7F, 'E',  'L',  'F',  0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x54, 0x80, 0x04, 0x08, 0x34, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x34, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00,
    0x54, 0x80, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x4E, 0x00, 0x00, 0x00, 0x8E, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0xB8, 0x05, 0x00, 0x00, 0x00,
    0xBB, 0x94, 0x80, 0x04, 0x08,
    0xB9, 0x01, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0x89, 0xC3,
    0xB8, 0x06, 0x00, 0x00, 0x00,
    0xB9, 0xA2, 0x80, 0x04, 0x08,
    0xBA, 0x40, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0x89, 0xC2,
    0xB8, 0x04, 0x00, 0x00, 0x00,
    0xBB, 0x01, 0x00, 0x00, 0x00,
    0xB9, 0xA2, 0x80, 0x04, 0x08,
    0xCD, 0x80,
    0xB8, 0x02, 0x00, 0x00, 0x00,
    0x31, 0xDB,
    0xCD, 0x80,
    '/',  'e',  't',  'c',  '/',  'h',  'o',  's',
    't',  'n',  'a',  'm',  'e',  0x00
};

static const uint8_t g_user_fault_elf[] = {
    0x7F, 'E',  'L',  'F',  0x01, 0x01, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x54, 0x80, 0x04, 0x08, 0x34, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x34, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x54, 0x00, 0x00, 0x00,
    0x54, 0x80, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00,
    0x0E, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0xA1, 0x00, 0x00, 0x00, 0xC0,
    0xB8, 0x02, 0x00, 0x00, 0x00,
    0x31, 0xDB,
    0xCD, 0x80
};

static const ram_seed_t g_seed_nodes[] = {
    { "/", VFS_NODE_DIR, 0, 0 },
    { "/bin", VFS_NODE_DIR, 0, 0 },
    { "/dev", VFS_NODE_DIR, 0, 0 },
    { "/etc", VFS_NODE_DIR, 0, 0 },
    { "/home", VFS_NODE_DIR, 0, 0 },
    { "/home/root", VFS_NODE_DIR, 0, 0 },
    { "/tmp", VFS_NODE_DIR, 0, 0 },
    { "/disk", VFS_NODE_DIR, 0, 0 },
    { "/proc", VFS_NODE_DIR, 0, 0 },
    { "/proc/version", VFS_NODE_FILE, 0, 0 },
    { "/proc/cpuinfo", VFS_NODE_FILE, 0, 0 },
    { "/proc/meminfo", VFS_NODE_FILE, 0, 0 },
    { "/proc/mounts", VFS_NODE_FILE, 0, 0 },
    { "/proc/processes", VFS_NODE_FILE, 0, 0 },
    { "/proc/tasks", VFS_NODE_FILE, 0, 0 },
    { "/proc/fds", VFS_NODE_FILE, 0, 0 },
    { "/proc/env", VFS_NODE_FILE, 0, 0 },
    { "/proc/interrupts", VFS_NODE_FILE, 0, 0 },
    { "/proc/exceptions", VFS_NODE_FILE, 0, 0 },
    { "/proc/uptime", VFS_NODE_FILE, 0, 0 },
    { "/proc/stat", VFS_NODE_FILE, 0, 0 },
    { "/proc/sched", VFS_NODE_FILE, 0, 0 },
    { "/proc/drivers", VFS_NODE_FILE, 0, 0 },
    { "/proc/block", VFS_NODE_FILE, 0, 0 },
    { "/proc/pci", VFS_NODE_FILE, 0, 0 },
    { "/proc/net", VFS_NODE_DIR, 0, 0 },
    { "/proc/net/dev", VFS_NODE_FILE, 0, 0 },
    { "/proc/net/route", VFS_NODE_FILE, 0, 0 },
    { "/proc/net/arp", VFS_NODE_FILE, 0, 0 },
    { "/proc/net/udp", VFS_NODE_FILE, 0, 0 },
    { "/proc/net/snmp", VFS_NODE_FILE, 0, 0 },
    { "/proc/sockets", VFS_NODE_FILE, 0, 0 },
    { "/proc/routes", VFS_NODE_FILE, 0, 0 },
    { "/proc/arp", VFS_NODE_FILE, 0, 0 },
    { "/proc/tty", VFS_NODE_FILE, 0, 0 },
    { "/proc/vm", VFS_NODE_FILE, 0, 0 },
    { "/proc/filesystems", VFS_NODE_FILE, 0, 0 },
    { "/proc/syscalls", VFS_NODE_FILE, 0, 0 },
    { "/bin/init", VFS_NODE_FILE, (const uint8_t *)"builtin:init\n", 0 },
    { "/bin/sh", VFS_NODE_FILE, (const uint8_t *)"builtin:shell\n", 0 },
    { "/bin/hello", VFS_NODE_FILE, g_user_hello_elf, sizeof(g_user_hello_elf) },
    { "/bin/hostname", VFS_NODE_FILE, g_user_hostname_elf, sizeof(g_user_hostname_elf) },
    { "/bin/fault", VFS_NODE_FILE, g_user_fault_elf, sizeof(g_user_fault_elf) },
    { "/etc/hostname", VFS_NODE_FILE, (const uint8_t *)"myos-machine\n", 0 },
    { "/etc/motd", VFS_NODE_FILE, (const uint8_t *)"MyOS bare-metal environment\n", 0 },
    { "/home/root/readme.txt", VFS_NODE_FILE, (const uint8_t *)"Welcome to MyOS.\nThis file lives in ramfs.\n", 0 },
    { "/dev/tty0", VFS_NODE_DEV, 0, 0 },
    { "/dev/tty", VFS_NODE_DEV, 0, 0 },
    { "/dev/console", VFS_NODE_DEV, 0, 0 },
    { "/dev/kmsg", VFS_NODE_DEV, 0, 0 },
    { "/dev/null", VFS_NODE_DEV, 0, 0 },
    { "/dev/zero", VFS_NODE_DEV, 0, 0 },
    { "/dev/full", VFS_NODE_DEV, 0, 0 },
    { "/dev/urandom", VFS_NODE_DEV, 0, 0 },
    { "/dev/random", VFS_NODE_DEV, 0, 0 },
};

static ram_node_t g_nodes[RAMFS_MAX_NODES];
static vfs_fd_t g_fds[VFS_MAX_FDS];
static vfs_pipe_t g_pipes[VFS_MAX_PIPES];
static uint32_t g_urandom_state = 0x4D594F53U;

static uint32_t udp_socket_count(void);

static int kstrcmp(const char *a, const char *b) {
    if (!a || !b) return a == b ? 0 : (a ? 1 : -1);
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static uint32_t kstrlen(const char *s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static int path_under(const char *path, const char *dir) {
    uint32_t len = kstrlen(dir);
    uint32_t i;
    if (!path || !dir || !dir[0]) return 0;
    for (i = 0; i < len; i++) {
        if (path[i] != dir[i]) return 0;
    }
    return path[len] == 0 || path[len] == '/';
}

static void kcopy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    if (!max) return;
    while (src && src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void kmemcpy(char *dst, const char *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}

static int flags_valid(int flags) {
    int access = flags & VFS_O_RDWR;
    int allowed = VFS_O_RDWR |
                  VFS_O_CREAT |
                  VFS_O_TRUNC |
                  VFS_O_APPEND |
                  VFS_O_CLOEXEC |
                  VFS_O_NONBLOCK;
    if ((flags & ~allowed) != 0) return 0;
    if ((flags & VFS_O_TRUNC) && access == VFS_O_RDONLY) return 0;
    if ((flags & VFS_O_APPEND) && access == VFS_O_RDONLY) return 0;
    return access == VFS_O_RDONLY ||
           access == VFS_O_WRONLY ||
           access == VFS_O_RDWR;
}

static uint32_t mode_type_bits(vfs_node_type_t type) {
    if (type == VFS_NODE_DIR) return VFS_MODE_IFDIR;
    if (type == VFS_NODE_DEV) return VFS_MODE_IFDEV;
    if (type == VFS_NODE_SYMLINK) return VFS_MODE_IFLNK;
    return VFS_MODE_IFREG;
}

static uint32_t default_mode_for_path(vfs_node_type_t type, const char *path) {
    uint32_t perm = 0000644U;
    if (type == VFS_NODE_DIR) perm = path_under(path, "/proc") ? 0000555U : 0000755U;
    else if (type == VFS_NODE_DEV) perm = 0000666U;
    else if (type == VFS_NODE_SYMLINK) perm = 0000777U;
    else if (path_under(path, "/bin")) perm = 0000755U;
    else if (path_under(path, "/proc")) perm = 0000444U;
    return mode_type_bits(type) | perm;
}

static uint32_t creation_mode_for_type(vfs_node_type_t type) {
    uint32_t perm = type == VFS_NODE_DIR ? 0000777U : 0000666U;
    uint32_t mask = process_umask_get(process_current());
    return mode_type_bits(type) | (perm & ~mask & VFS_MODE_PERM_MASK);
}

static uint32_t sanitized_mode(vfs_node_type_t type, uint32_t mode) {
    return mode_type_bits(type) | (mode & VFS_MODE_PERM_MASK);
}

static int mode_allows_user(uint32_t mode,
                            uint32_t owner_uid,
                            uint32_t owner_gid,
                            uint32_t mask) {
    uint32_t uid = process_uid_get(process_current());
    uint32_t gid = process_gid_get(process_current());
    uint32_t shift = 0;
    if (mask & ~(VFS_ACCESS_READ | VFS_ACCESS_WRITE | VFS_ACCESS_EXEC)) return 0;
    if (uid == 0) {
        if ((mask & VFS_ACCESS_EXEC) && (mode & VFS_MODE_EXEC_MASK) == 0) return 0;
        return 1;
    }
    if (uid == owner_uid) shift = 6;
    else if (gid == owner_gid) shift = 3;
    if ((mask & VFS_ACCESS_READ) && (mode & (0004U << shift)) == 0) return 0;
    if ((mask & VFS_ACCESS_WRITE) && (mode & (0002U << shift)) == 0) return 0;
    if ((mask & VFS_ACCESS_EXEC) && (mode & (0001U << shift)) == 0) return 0;
    return 1;
}

static int flags_allowed_by_owner(int flags, uint32_t mode, uint32_t uid, uint32_t gid) {
    int access = flags & VFS_O_RDWR;
    if (access == VFS_O_RDONLY) return mode_allows_user(mode, uid, gid, VFS_ACCESS_READ);
    if (access == VFS_O_WRONLY) return mode_allows_user(mode, uid, gid, VFS_ACCESS_WRITE);
    if (access == VFS_O_RDWR)
        return mode_allows_user(mode, uid, gid, VFS_ACCESS_READ | VFS_ACCESS_WRITE);
    return 0;
}

static int current_can_change_owner(uint32_t owner_uid) {
    uint32_t uid = process_uid_get(process_current());
    return uid == 0 || uid == owner_uid;
}

static int current_can_chown(void) {
    return process_uid_get(process_current()) == 0;
}

static void append_char(char *buf, uint32_t *pos, uint32_t max, char ch) {
    if (*pos + 1 >= max) return;
    buf[*pos] = ch;
    (*pos)++;
    buf[*pos] = 0;
}

static void append_str(char *buf, uint32_t *pos, uint32_t max, const char *s) {
    while (s && *s) append_char(buf, pos, max, *s++);
}

static void append_dec(char *buf, uint32_t *pos, uint32_t max, uint32_t v) {
    char tmp[11];
    uint32_t n = 0;
    if (v == 0) {
        append_char(buf, pos, max, '0');
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n) append_char(buf, pos, max, tmp[--n]);
}

static void append_hex32(char *buf, uint32_t *pos, uint32_t max, uint32_t v) {
    int shift;
    append_str(buf, pos, max, "0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        static const char h[] = "0123456789ABCDEF";
        append_char(buf, pos, max, h[(v >> shift) & 0xF]);
    }
}

static void append_hex8(char *buf, uint32_t *pos, uint32_t max, uint32_t v) {
    static const char h[] = "0123456789ABCDEF";
    append_char(buf, pos, max, h[(v >> 4) & 0xF]);
    append_char(buf, pos, max, h[v & 0xF]);
}

static void append_ipv4(char *buf, uint32_t *pos, uint32_t max, uint32_t ip) {
    append_dec(buf, pos, max, (ip >> 24) & 0xFF);
    append_char(buf, pos, max, '.');
    append_dec(buf, pos, max, (ip >> 16) & 0xFF);
    append_char(buf, pos, max, '.');
    append_dec(buf, pos, max, (ip >> 8) & 0xFF);
    append_char(buf, pos, max, '.');
    append_dec(buf, pos, max, ip & 0xFF);
}

static void append_mac(char *buf, uint32_t *pos, uint32_t max, const uint8_t mac[6]) {
    uint32_t i;
    for (i = 0; i < 6; i++) {
        if (i) append_char(buf, pos, max, ':');
        append_hex8(buf, pos, max, mac ? mac[i] : 0);
    }
}

static uint32_t saturating_mul_u32(uint32_t a, uint32_t b) {
    if (a != 0 && b > 0xFFFFFFFFU / a) return 0xFFFFFFFFU;
    return a * b;
}

static int proc_pid_leaf_valid(const char *leaf) {
    return !leaf ||
           leaf[0] == 0 ||
           kstrcmp(leaf, "status") == 0 ||
           kstrcmp(leaf, "fds") == 0 ||
           kstrcmp(leaf, "environ") == 0 ||
           kstrcmp(leaf, "maps") == 0 ||
           kstrcmp(leaf, "cwd") == 0 ||
           kstrcmp(leaf, "cmdline") == 0 ||
           kstrcmp(leaf, "stat") == 0 ||
           kstrcmp(leaf, "children") == 0 ||
           kstrcmp(leaf, "exe") == 0;
}

static int parse_proc_pid_path_raw(const char *path, uint32_t *pid, const char **leaf) {
    uint32_t value = 0;
    uint32_t i = 6;
    int any = 0;
    if (!path ||
        path[0] != '/' || path[1] != 'p' || path[2] != 'r' ||
        path[3] != 'o' || path[4] != 'c' || path[5] != '/') return 0;
    if (path[i] < '0' || path[i] > '9') return 0;
    while (path[i] >= '0' && path[i] <= '9') {
        uint32_t digit = (uint32_t)(path[i] - '0');
        if (value > (0xFFFFFFFFU - digit) / 10U) return 0;
        value = value * 10U + digit;
        i++;
        any = 1;
    }
    if (!any) return 0;
    if (path[i] == 0) {
        if (pid) *pid = value;
        if (leaf) *leaf = "";
        return 1;
    }
    if (path[i] != '/') return 0;
    if (path[i + 1] == 0) return 0;
    {
        uint32_t j = i + 1;
        while (path[j]) {
            if (path[j] == '/') return 0;
            j++;
        }
    }
    if (pid) *pid = value;
    if (leaf) *leaf = path + i + 1;
    return 1;
}

static int parse_proc_self_path_raw(const char *path, uint32_t *pid, const char **leaf) {
    process_t *proc;
    uint32_t i;
    static const char prefix[] = "/proc/self";
    if (!path) return 0;
    for (i = 0; prefix[i]; i++) {
        if (path[i] != prefix[i]) return 0;
    }
    if (path[i] == 0) {
        proc = process_current();
        if (pid) *pid = proc ? proc->pid : 0;
        if (leaf) *leaf = "";
        return 1;
    }
    if (path[i] != '/' || path[i + 1] == 0) return 0;
    {
        uint32_t j = i + 1;
        while (path[j]) {
            if (path[j] == '/') return 0;
            j++;
        }
    }
    proc = process_current();
    if (pid) *pid = proc ? proc->pid : 0;
    if (leaf) *leaf = path + i + 1;
    return 1;
}

static int parse_proc_process_path_raw(const char *path, uint32_t *pid, const char **leaf) {
    if (parse_proc_pid_path_raw(path, pid, leaf)) return 1;
    return parse_proc_self_path_raw(path, pid, leaf);
}

static int proc_pid_path_valid(const char *path) {
    uint32_t pid;
    const char *leaf;
    if (!parse_proc_process_path_raw(path, &pid, &leaf)) return 0;
    return process_find(pid) && proc_pid_leaf_valid(leaf);
}

static int make_proc_pid_path(uint32_t pid, char *out, uint32_t max) {
    uint32_t pos = 0;
    if (!out || max < 8) return -1;
    append_str(out, &pos, max, "/proc/");
    append_dec(out, &pos, max, pid);
    return out[pos] == 0 ? 0 : -1;
}

static void kclear_node(ram_node_t *node) {
    uint32_t i;
    node->used = 0;
    node->type = VFS_NODE_FILE;
    node->size = 0;
    node->mode = 0;
    node->uid = 0;
    node->gid = 0;
    node->created_ms = 0;
    node->modified_ms = 0;
    node->accessed_ms = 0;
    for (i = 0; i < VFS_MAX_PATH; i++) node->path[i] = 0;
    for (i = 0; i < RAMFS_FILE_CAP; i++) node->data[i] = 0;
}

static void kclear_fd(vfs_fd_t *fd) {
    fd->used = 0;
    fd->refcount = 0;
    fd->backend = 0;
    fd->node = 0;
    fd->simple_fd = -1;
    fd->pipe_id = -1;
    fd->pipe_write = 0;
    fd->udp_bound = 0;
    fd->udp_peer_set = 0;
    fd->udp_peer_ipv4 = 0;
    fd->udp_ifindex = 0;
    fd->udp_port = 0;
    fd->udp_peer_port = 0;
    fd->pos = 0;
    fd->flags = 0;
    kcopy(fd->path, "", VFS_MAX_PATH);
}

static void proc_reset(ram_node_t *node, uint32_t *pos) {
    uint32_t i;
    node->size = 0;
    for (i = 0; i < RAMFS_FILE_CAP; i++) node->data[i] = 0;
    *pos = 0;
}

static void proc_line(char *buf, uint32_t *pos, const char *key, uint32_t value, const char *suffix) {
    append_str(buf, pos, RAMFS_FILE_CAP, key);
    append_dec(buf, pos, RAMFS_FILE_CAP, value);
    if (suffix) append_str(buf, pos, RAMFS_FILE_CAP, suffix);
    append_char(buf, pos, RAMFS_FILE_CAP, '\n');
}

static void proc_hex_line(char *buf, uint32_t *pos, const char *key, uint32_t value) {
    append_str(buf, pos, RAMFS_FILE_CAP, key);
    append_hex32(buf, pos, RAMFS_FILE_CAP, value);
    append_char(buf, pos, RAMFS_FILE_CAP, '\n');
}

static void proc_env_lines(char *buf, uint32_t *pos, const process_t *proc) {
    uint32_t i;
    for (i = 0; i < (uint32_t)process_env_count(proc); i++) {
        const char *entry = process_env_entry(proc, i);
        if (!entry) continue;
        append_str(buf, pos, RAMFS_FILE_CAP, entry);
        append_char(buf, pos, RAMFS_FILE_CAP, '\n');
    }
}

static int proc_read_user_u8(const process_t *proc, uint32_t virt, uint8_t *out) {
    uint32_t phys;
    if (!proc || !proc->page_dir || !out || virt < USER_BASE || virt >= USER_STACK_TOP) return -1;
    phys = paging_get_phys_in_directory((uint32_t *)proc->page_dir, virt);
    if (!phys) return -1;
    *out = *(const uint8_t *)phys;
    return 0;
}

static int proc_read_user_u32(const process_t *proc, uint32_t virt, uint32_t *out) {
    uint32_t i;
    uint32_t value = 0;
    if (!out || virt > 0xFFFFFFFFU - 3U) return -1;
    for (i = 0; i < 4; i++) {
        uint8_t byte;
        if (proc_read_user_u8(proc, virt + i, &byte) < 0) return -1;
        value |= (uint32_t)byte << (i * 8U);
    }
    *out = value;
    return 0;
}

static int proc_append_user_string(char *buf,
                                   uint32_t *pos,
                                   const process_t *proc,
                                   uint32_t virt) {
    uint32_t i;
    for (i = 0; i < PROCESS_ARG_MAX; i++) {
        uint8_t byte;
        if (proc_read_user_u8(proc, virt + i, &byte) < 0) return -1;
        if (byte == 0) return 0;
        append_char(buf, pos, RAMFS_FILE_CAP, (char)byte);
    }
    return -1;
}

static int proc_cmdline_from_stack(char *buf, uint32_t *pos, const process_t *proc) {
    uint32_t argc;
    uint32_t argv;
    uint32_t i;
    if (!proc ||
        !proc->page_dir ||
        proc->user_stack < USER_BASE ||
        proc->user_stack > USER_STACK_TOP - 12U ||
        proc_read_user_u32(proc, proc->user_stack, &argc) < 0 ||
        proc_read_user_u32(proc, proc->user_stack + 4U, &argv) < 0 ||
        argc == 0 ||
        argc > PROCESS_MAX_ARGS) return -1;
    for (i = 0; i < argc; i++) {
        uint32_t arg_ptr;
        if (proc_read_user_u32(proc, argv + i * 4U, &arg_ptr) < 0) return -1;
        if (i) append_char(buf, pos, RAMFS_FILE_CAP, ' ');
        if (proc_append_user_string(buf, pos, proc, arg_ptr) < 0) return -1;
    }
    append_char(buf, pos, RAMFS_FILE_CAP, '\n');
    return 0;
}

static void proc_cmdline(char *buf, uint32_t *pos, const process_t *proc) {
    if (proc_cmdline_from_stack(buf, pos, proc) == 0) return;
    append_str(buf, pos, RAMFS_FILE_CAP, proc ? proc->name : "");
    append_char(buf, pos, RAMFS_FILE_CAP, '\n');
}

static void proc_children(char *buf, uint32_t *pos, const process_t *proc) {
    uint32_t i;
    int any = 0;
    if (!proc) {
        append_char(buf, pos, RAMFS_FILE_CAP, '\n');
        return;
    }
    for (i = 0; i < PROCESS_MAX; i++) {
        const process_t *child = process_at(i);
        if (!child || child->ppid != proc->pid) continue;
        if (any) append_char(buf, pos, RAMFS_FILE_CAP, ' ');
        append_dec(buf, pos, RAMFS_FILE_CAP, child->pid);
        any = 1;
    }
    append_char(buf, pos, RAMFS_FILE_CAP, '\n');
}

static uint32_t proc_page_round_up(uint32_t value) {
    return (value + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
}

static uint32_t proc_map_kind(const process_t *proc, uint32_t page) {
    uint32_t heap_top = proc_page_round_up(proc ? proc->heap_break : 0);
    if (page >= USER_STACK_TOP - USER_STACK_SIZE && page < USER_STACK_TOP) return 4;
    if (page >= USER_MMAP_BASE && page < USER_MMAP_TOP) return 3;
    if (proc && proc->heap_start && page >= proc->heap_start && page < heap_top) return 2;
    return 1;
}

static const char *proc_map_name(uint32_t kind) {
    switch (kind) {
        case 2: return "heap";
        case 3: return "mmap";
        case 4: return "stack";
        default: return "image";
    }
}

static void proc_map_line(char *buf,
                          uint32_t *pos,
                          uint32_t start,
                          uint32_t end,
                          uint32_t writable,
                          uint32_t kind) {
    append_hex32(buf, pos, RAMFS_FILE_CAP, start);
    append_char(buf, pos, RAMFS_FILE_CAP, ' ');
    append_hex32(buf, pos, RAMFS_FILE_CAP, end);
    append_char(buf, pos, RAMFS_FILE_CAP, ' ');
    append_char(buf, pos, RAMFS_FILE_CAP, 'r');
    append_char(buf, pos, RAMFS_FILE_CAP, writable ? 'w' : '-');
    append_char(buf, pos, RAMFS_FILE_CAP, '-');
    append_char(buf, pos, RAMFS_FILE_CAP, ' ');
    append_str(buf, pos, RAMFS_FILE_CAP, proc_map_name(kind));
    append_char(buf, pos, RAMFS_FILE_CAP, '\n');
}

static void proc_user_maps(char *buf, uint32_t *pos, const process_t *proc) {
    uint32_t page;
    uint32_t run_start = 0;
    uint32_t run_writable = 0;
    uint32_t run_kind = 0;
    int in_run = 0;
    if (!proc || !proc->page_dir) return;
    for (page = USER_BASE; page < USER_STACK_TOP; page += PAGE_SIZE) {
        uint32_t flags = paging_get_flags_in_directory((uint32_t *)proc->page_dir, page);
        if ((flags & (PAGE_PRESENT | PAGE_USER)) == (PAGE_PRESENT | PAGE_USER)) {
            uint32_t writable = (flags & PAGE_WRITE) ? 1U : 0U;
            uint32_t kind = proc_map_kind(proc, page);
            if (!in_run) {
                in_run = 1;
                run_start = page;
                run_writable = writable;
                run_kind = kind;
            } else if (writable != run_writable || kind != run_kind) {
                proc_map_line(buf, pos, run_start, page, run_writable, run_kind);
                run_start = page;
                run_writable = writable;
                run_kind = kind;
            }
        } else if (in_run) {
            proc_map_line(buf, pos, run_start, page, run_writable, run_kind);
            in_run = 0;
        }
        if (page > 0xFFFFFFFFU - PAGE_SIZE) break;
    }
    if (in_run) proc_map_line(buf, pos, run_start, USER_STACK_TOP, run_writable, run_kind);
}

static void sync_proc_node(ram_node_t *node) {
    uint32_t pos;
    uint32_t i;
    if (!node || node->type != VFS_NODE_FILE) return;
    if (kstrcmp(node->path, "/proc/env") == 0) {
        proc_reset(node, &pos);
        proc_env_lines(node->data, &pos, process_current());
    } else if (kstrcmp(node->path, "/proc/cpuinfo") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "processor: 0\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "arch: ");
        append_str(node->data, &pos, RAMFS_FILE_CAP, uts_machine());
        append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        append_str(node->data, &pos, RAMFS_FILE_CAP, "mode: protected\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "paging: on\n");
    } else if (kstrcmp(node->path, "/proc/version") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, uts_sysname());
        append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
        append_str(node->data, &pos, RAMFS_FILE_CAP, uts_release());
        append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
        append_str(node->data, &pos, RAMFS_FILE_CAP, uts_version());
        append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
        append_str(node->data, &pos, RAMFS_FILE_CAP, uts_machine());
        append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        append_str(node->data, &pos, RAMFS_FILE_CAP, "SysName: ");
        append_str(node->data, &pos, RAMFS_FILE_CAP, uts_sysname());
        append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        append_str(node->data, &pos, RAMFS_FILE_CAP, "NodeName: ");
        append_str(node->data, &pos, RAMFS_FILE_CAP, uts_nodename());
        append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        append_str(node->data, &pos, RAMFS_FILE_CAP, "Machine: ");
        append_str(node->data, &pos, RAMFS_FILE_CAP, uts_machine());
        append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
    } else if (kstrcmp(node->path, "/proc/meminfo") == 0) {
        proc_reset(node, &pos);
        proc_line(node->data, &pos, "MemFreePages: ", pmm_free_pages(), "");
        proc_line(node->data, &pos, "MemManagedPages: ", pmm_managed_pages(), "");
        proc_hex_line(node->data, &pos, "MemManagedStart: ", pmm_managed_start());
        proc_hex_line(node->data, &pos, "MemManagedEnd: ", pmm_managed_end());
        proc_line(node->data, &pos, "HeapFreeBytes: ", (uint32_t)heap_free_bytes(), "");
        proc_line(node->data, &pos, "HeapUsedBytes: ", (uint32_t)heap_used_bytes(), "");
        proc_line(node->data, &pos, "HeapBlocks: ", heap_block_count(), "");
        proc_line(node->data, &pos, "HeapFreeBlocks: ", heap_free_block_count(), "");
        proc_line(node->data, &pos, "Processes: ", process_count(), "");
        proc_line(node->data, &pos, "TasksFree: ", task_free_slots(), "");
    } else if (kstrcmp(node->path, "/proc/mounts") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "ramfs / ramfs rw\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "devfs /dev devfs rw\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "procfs /proc procfs ro\n");
        if (simplefs_mounted()) {
            append_str(node->data, &pos, RAMFS_FILE_CAP, "simplefs ");
            append_str(node->data, &pos, RAMFS_FILE_CAP, simplefs_mount_path());
            append_str(node->data, &pos, RAMFS_FILE_CAP, " simplefs rw\n");
        }
    } else if (kstrcmp(node->path, "/proc/processes") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "PID PPID PGID SID UID GID STATE HEAP ARGC ENVC NAME\n");
        for (i = 0; i < PROCESS_MAX; i++) {
            const process_t *p = process_at(i);
            if (!p) continue;
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->pid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->ppid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->pgid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->sid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->uid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->gid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, process_state_name(p->state));
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_hex32(node->data, &pos, RAMFS_FILE_CAP, p->heap_break);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->argc);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, (uint32_t)process_env_count(p));
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, p->name);
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
    } else if (kstrcmp(node->path, "/proc/tasks") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "TID PID STATE PRIO NAME\n");
        for (i = 0; i < task_table_size(); i++) {
            const task_t *t = task_at(i);
            if (!t) continue;
            append_dec(node->data, &pos, RAMFS_FILE_CAP, t->id);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, t->process_id);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, task_state_name(t->state));
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, t->priority);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, t->name ? t->name : "-");
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
    } else if (kstrcmp(node->path, "/proc/fds") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "PID FD VFSFD FLAGS NAME\n");
        for (i = 0; i < PROCESS_MAX; i++) {
            const process_t *p = process_at(i);
            uint32_t fd;
            if (!p) continue;
            for (fd = 0; fd < PROCESS_MAX_FDS; fd++) {
                if (p->fds[fd] < 0) continue;
                append_dec(node->data, &pos, RAMFS_FILE_CAP, p->pid);
                append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
                append_dec(node->data, &pos, RAMFS_FILE_CAP, fd);
                append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
                append_dec(node->data, &pos, RAMFS_FILE_CAP, (uint32_t)p->fds[fd]);
                append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
                append_str(node->data, &pos, RAMFS_FILE_CAP,
                           (p->fd_flags[fd] & PROCESS_FD_CLOEXEC) ? "cloexec" : "-");
                append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
                append_str(node->data, &pos, RAMFS_FILE_CAP, p->name);
                append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
            }
        }
    } else if (kstrcmp(node->path, "/proc/interrupts") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "IRQ COUNT\n");
        for (i = 0; i < 16; i++) {
            append_dec(node->data, &pos, RAMFS_FILE_CAP, i);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, irq_count(i));
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
    } else if (kstrcmp(node->path, "/proc/exceptions") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "VECTOR COUNT\n");
        for (i = 0; i < 32; i++) {
            append_dec(node->data, &pos, RAMFS_FILE_CAP, i);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, exception_count(i));
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
        proc_line(node->data, &pos, "LastVector: ", exception_last_vector(), "");
        proc_hex_line(node->data, &pos, "LastError: ", exception_last_error());
        proc_hex_line(node->data, &pos, "LastEIP: ", exception_last_eip());
        proc_hex_line(node->data, &pos, "LastCS: ", exception_last_cs());
        proc_hex_line(node->data, &pos, "LastCR2: ", exception_last_cr2());
    } else if (kstrcmp(node->path, "/proc/uptime") == 0) {
        proc_reset(node, &pos);
        proc_line(node->data, &pos, "Ticks: ", timer_ticks(), "");
        proc_line(node->data, &pos, "TimerHz: ", timer_hz(), "");
        proc_line(node->data, &pos, "UptimeMs: ", timer_ms(), "");
        proc_line(node->data, &pos, "PreemptEnabled: ", scheduler_preemption_enabled() ? 1U : 0U, "");
        proc_line(node->data, &pos, "PreemptPending: ", scheduler_preempt_pending() ? 1U : 0U, "");
        proc_line(node->data, &pos, "QuantumTicks: ", scheduler_quantum_ticks(), "");
        proc_line(node->data, &pos, "CurrentTicks: ", scheduler_current_ticks(), "");
        proc_line(node->data, &pos, "IRQSwitches: ", scheduler_irq_switches(), "");
        proc_line(node->data, &pos, "CoopSwitches: ", scheduler_coop_switches(), "");
        proc_line(node->data, &pos, "Drivers: ", driver_count(), "");
        proc_line(node->data, &pos, "PCI: ", pci_count(), "");
        proc_line(node->data, &pos, "Block: ", block_count(), "");
        proc_line(node->data, &pos, "NetIf: ", netif_count(), "");
    } else if (kstrcmp(node->path, "/proc/stat") == 0) {
        uint32_t irq_total = 0;
        uint32_t proc_ready = 0;
        uint32_t proc_running = 0;
        uint32_t proc_sleeping = 0;
        uint32_t proc_zombie = 0;
        uint32_t task_ready = 0;
        uint32_t task_running = 0;
        uint32_t task_sleeping = 0;
        uint32_t task_dead = 0;
        proc_reset(node, &pos);
        for (i = 0; i < 16; i++) irq_total += irq_count(i);
        for (i = 0; i < PROCESS_MAX; i++) {
            const process_t *p = process_at(i);
            if (!p) continue;
            if (p->state == PROC_READY) proc_ready++;
            else if (p->state == PROC_RUNNING) proc_running++;
            else if (p->state == PROC_SLEEPING) proc_sleeping++;
            else if (p->state == PROC_ZOMBIE) proc_zombie++;
        }
        for (i = 0; i < task_table_size(); i++) {
            const task_t *task = task_at(i);
            if (!task) continue;
            if (task->state == TASK_READY) task_ready++;
            else if (task->state == TASK_RUNNING) task_running++;
            else if (task->state == TASK_SLEEPING) task_sleeping++;
            else if (task->state == TASK_DEAD) task_dead++;
        }
        append_str(node->data, &pos, RAMFS_FILE_CAP, "cpu ");
        append_dec(node->data, &pos, RAMFS_FILE_CAP, timer_ticks());
        append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
        append_dec(node->data, &pos, RAMFS_FILE_CAP, scheduler_irq_switches());
        append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
        append_dec(node->data, &pos, RAMFS_FILE_CAP, scheduler_coop_switches());
        append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        append_str(node->data, &pos, RAMFS_FILE_CAP, "intr ");
        append_dec(node->data, &pos, RAMFS_FILE_CAP, irq_total);
        for (i = 0; i < 16; i++) {
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, irq_count(i));
        }
        append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        proc_line(node->data, &pos, "ctxt ", scheduler_irq_switches() + scheduler_coop_switches(), "");
        proc_line(node->data, &pos, "processes ", process_count(), "");
        proc_line(node->data, &pos, "procs_running ", proc_ready + proc_running, "");
        proc_line(node->data, &pos, "procs_blocked ", proc_sleeping, "");
        proc_line(node->data, &pos, "procs_zombie ", proc_zombie, "");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "tasks ");
        append_dec(node->data, &pos, RAMFS_FILE_CAP, task_ready + task_running + task_sleeping + task_dead);
        append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
        append_dec(node->data, &pos, RAMFS_FILE_CAP, task_ready);
        append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
        append_dec(node->data, &pos, RAMFS_FILE_CAP, task_running);
        append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
        append_dec(node->data, &pos, RAMFS_FILE_CAP, task_sleeping);
        append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
        append_dec(node->data, &pos, RAMFS_FILE_CAP, task_dead);
        append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
    } else if (kstrcmp(node->path, "/proc/sched") == 0) {
        proc_reset(node, &pos);
        proc_line(node->data, &pos, "CurrentTask: ", (uint32_t)task_current_id(), "");
        proc_line(node->data, &pos, "CurrentPID: ", task_current_process_id(), "");
        proc_line(node->data, &pos, "TaskSlots: ", task_table_size(), "");
        proc_line(node->data, &pos, "TaskFreeSlots: ", task_free_slots(), "");
        proc_line(node->data, &pos, "PreemptEnabled: ", scheduler_preemption_enabled() ? 1U : 0U, "");
        proc_line(node->data, &pos, "PreemptPending: ", scheduler_preempt_pending() ? 1U : 0U, "");
        proc_line(node->data, &pos, "QuantumTicks: ", scheduler_quantum_ticks(), "");
        proc_line(node->data, &pos, "CurrentTicks: ", scheduler_current_ticks(), "");
        proc_line(node->data, &pos, "IRQSwitches: ", scheduler_irq_switches(), "");
        proc_line(node->data, &pos, "CoopSwitches: ", scheduler_coop_switches(), "");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "TID PID STATE PRIO CR3 SLEEP NAME\n");
        for (i = 0; i < task_table_size(); i++) {
            const task_t *t = task_at(i);
            if (!t) continue;
            append_dec(node->data, &pos, RAMFS_FILE_CAP, t->id);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, t->process_id);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, task_state_name(t->state));
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, t->priority);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_hex32(node->data, &pos, RAMFS_FILE_CAP, t->regs.cr3);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, t->sleep_until);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, t->name ? t->name : "-");
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
    } else if (kstrcmp(node->path, "/proc/drivers") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "IDX BUS ID0 ID1 STATE NAME\n");
        for (i = 0; i < driver_count(); i++) {
            const driver_t *d = driver_at(i);
            if (!d) continue;
            append_dec(node->data, &pos, RAMFS_FILE_CAP, i);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, driver_bus_name(d->bus));
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, d->id0);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, d->id1);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, d->loaded ? "loaded " : "off ");
            append_str(node->data, &pos, RAMFS_FILE_CAP, d->name ? d->name : "-");
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
    } else if (kstrcmp(node->path, "/proc/block") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "IDX NAME SECTORS SIZE RW ROPS WOPS RSECT WSECT\n");
        for (i = 0; i < block_count(); i++) {
            const block_device_t *b = block_at(i);
            if (!b) continue;
            append_dec(node->data, &pos, RAMFS_FILE_CAP, i);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, b->name ? b->name : "-");
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, b->sector_count);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP,
                       saturating_mul_u32(b->sector_count, b->sector_size));
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, b->writable ? "rw" : "ro");
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, b->read_ops);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, b->write_ops);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, b->read_sectors);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, b->write_sectors);
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
    } else if (kstrcmp(node->path, "/proc/pci") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "BUS SLOT FN VENDOR DEVICE CLASS SUB\n");
        for (i = 0; i < pci_count(); i++) {
            const pci_device_t *p = pci_at(i);
            if (!p) continue;
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->bus);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->slot);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->function);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->vendor_id);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->device_id);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->class_code);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->subclass);
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
    } else if (kstrcmp(node->path, "/proc/net/dev") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "IDX NAME IPV4 STATE MTU TX RX\n");
        for (i = 0; i < netif_count(); i++) {
            const netif_t *n = netif_at(i);
            if (!n) continue;
            append_dec(node->data, &pos, RAMFS_FILE_CAP, i);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, n->name ? n->name : "-");
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_ipv4(node->data, &pos, RAMFS_FILE_CAP, n->ipv4);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, n->up ? "up" : "down");
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, n->mtu);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, n->tx_packets);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, n->rx_packets);
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
    } else if (kstrcmp(node->path, "/proc/net/snmp") == 0) {
        proc_reset(node, &pos);
        proc_line(node->data, &pos, "RXQueue: ", net_rx_queue_count(), "");
        proc_line(node->data, &pos, "UDPQueue: ", net_udp_queue_count(), "");
        proc_line(node->data, &pos, "UDPSockets: ", udp_socket_count(), "");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "UDPChecksum: required\n");
    } else if (kstrcmp(node->path, "/proc/sockets") == 0 ||
               kstrcmp(node->path, "/proc/net/udp") == 0) {
        int fd;
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "VFD PROTO LOCAL PEER\n");
        for (fd = 0; fd < VFS_MAX_FDS; fd++) {
            if (!g_fds[fd].used || g_fds[fd].backend != VFS_BACKEND_UDP4) continue;
            append_dec(node->data, &pos, RAMFS_FILE_CAP, (uint32_t)fd);
            append_str(node->data, &pos, RAMFS_FILE_CAP, " udp4 ");
            if (g_fds[fd].udp_bound) append_dec(node->data, &pos, RAMFS_FILE_CAP, g_fds[fd].udp_port);
            else append_char(node->data, &pos, RAMFS_FILE_CAP, '-');
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            if (g_fds[fd].udp_peer_set) {
                append_ipv4(node->data, &pos, RAMFS_FILE_CAP, g_fds[fd].udp_peer_ipv4);
                append_char(node->data, &pos, RAMFS_FILE_CAP, ':');
                append_dec(node->data, &pos, RAMFS_FILE_CAP, g_fds[fd].udp_peer_port);
            } else {
                append_char(node->data, &pos, RAMFS_FILE_CAP, '-');
            }
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
    } else if (kstrcmp(node->path, "/proc/tty") == 0) {
        uint32_t mode = tty_get_mode();
        proc_reset(node, &pos);
        proc_line(node->data, &pos, "Mode: ", mode, "");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "Echo: ");
        append_str(node->data, &pos, RAMFS_FILE_CAP, (mode & TTY_MODE_ECHO) ? "on\n" : "off\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "Canonical: ");
        append_str(node->data, &pos, RAMFS_FILE_CAP, (mode & TTY_MODE_CANON) ? "on\n" : "off\n");
        proc_line(node->data, &pos, "Rows: ", TTY_ROWS, "");
        proc_line(node->data, &pos, "Columns: ", TTY_COLUMNS, "");
        proc_line(node->data, &pos, "ReadReady: ", tty_read_ready() ? 1U : 0U, "");
        proc_line(node->data, &pos, "OutputBytes: ", tty_size(), "");
    } else if (kstrcmp(node->path, "/proc/routes") == 0 ||
               kstrcmp(node->path, "/proc/net/route") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "IDX DEST MASK GATEWAY IFACE\n");
        for (i = 0; i < net_route_count(); i++) {
            const net_route_t *route = net_route_at(i);
            const netif_t *iface;
            if (!route) continue;
            iface = netif_at(route->ifindex);
            append_dec(node->data, &pos, RAMFS_FILE_CAP, i);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_ipv4(node->data, &pos, RAMFS_FILE_CAP, route->dest);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_ipv4(node->data, &pos, RAMFS_FILE_CAP, route->mask);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_ipv4(node->data, &pos, RAMFS_FILE_CAP, route->gateway);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, iface && iface->name ? iface->name : "-");
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
    } else if (kstrcmp(node->path, "/proc/arp") == 0 ||
               kstrcmp(node->path, "/proc/net/arp") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "IDX IFACE IPV4 MAC\n");
        for (i = 0; i < net_arp_count(); i++) {
            const net_arp_entry_t *entry = net_arp_at(i);
            const netif_t *iface;
            if (!entry) continue;
            iface = netif_at(entry->ifindex);
            append_dec(node->data, &pos, RAMFS_FILE_CAP, i);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, iface && iface->name ? iface->name : "-");
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_ipv4(node->data, &pos, RAMFS_FILE_CAP, entry->ipv4);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_mac(node->data, &pos, RAMFS_FILE_CAP, entry->mac);
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        }
    } else if (kstrcmp(node->path, "/proc/vm") == 0) {
        proc_reset(node, &pos);
        proc_hex_line(node->data, &pos, "KernelDirectory: ", (uint32_t)paging_kernel_directory());
        proc_hex_line(node->data, &pos, "CurrentDirectory: ", (uint32_t)paging_current_directory());
        proc_hex_line(node->data, &pos, "UserBase: ", USER_BASE);
        proc_hex_line(node->data, &pos, "UserTop: ", USER_STACK_TOP);
        proc_hex_line(node->data, &pos, "UserMmapBase: ", USER_MMAP_BASE);
        proc_hex_line(node->data, &pos, "UserMmapTop: ", USER_MMAP_TOP);
        proc_hex_line(node->data, &pos, "UserStackTop: ", USER_STACK_TOP);
        proc_hex_line(node->data, &pos, "UserStackSize: ", USER_STACK_SIZE);
        proc_line(node->data, &pos, "PageSize: ", PAGE_SIZE, "");
        proc_line(node->data, &pos, "FreePages: ", pmm_free_pages(), "");
        proc_line(node->data, &pos, "HeapFreeBytes: ", (uint32_t)heap_free_bytes(), "");
        proc_line(node->data, &pos, "HeapUsedBytes: ", (uint32_t)heap_used_bytes(), "");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "UserWritable: yes\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "KernelUserAccess: no\n");
    } else if (kstrcmp(node->path, "/proc/filesystems") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "nodev ramfs\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "nodev devfs\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "nodev procfs\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "block simplefs\n");
    } else if (kstrcmp(node->path, "/proc/syscalls") == 0) {
        proc_reset(node, &pos);
        append_str(node->data, &pos, RAMFS_FILE_CAP, "0 yield\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "1 sleep\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "2 exit\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "3 getpid\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "4 write\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "5 open\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "6 read\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "7 close\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "8 seek\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "9 stat\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "10 readdir\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "11 spawn\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "12 wait\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "13 mkdir\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "14 unlink\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "15 kill\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "16 getppid\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "17 dup\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "18 chdir\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "19 getcwd\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "20 rename\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "21 pipe\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "22 sbrk\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "23 uptime\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "24 spawn_args\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "25 netif_count\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "26 netif_info\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "27 net_ping4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "28 udp_send4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "29 udp_recv4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "30 sysinfo\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "31 process_count\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "32 process_info\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "33 block_count\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "34 block_info\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "35 driver_count\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "36 driver_info\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "37 task_count\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "38 task_info\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "39 pci_count\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "40 pci_info\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "41 block_read\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "42 block_write\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "43 simplefs_format\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "44 simplefs_mount\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "45 netif_set_up\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "46 netif_set_ipv4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "47 route_add4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "48 arp_add4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "49 route_count\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "50 route_info\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "51 arp_count\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "52 arp_info\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "53 fstat\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "54 mmap\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "55 munmap\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "56 mprotect\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "57 truncate\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "58 ftruncate\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "59 task_set_priority\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "60 task_priority\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "61 statfs\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "62 poll\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "63 dup2\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "64 tty_get_mode\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "65 tty_set_mode\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "66 ioctl\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "67 clock_gettime\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "68 clock_ms\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "69 chmod\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "70 access\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "71 uname\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "72 sysconf\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "73 wait_any\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "74 getenv\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "75 setenv\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "76 unsetenv\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "77 env_count\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "78 env_info\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "79 poll_many\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "80 gethostname\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "81 sethostname\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "82 umask\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "83 rmdir\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "84 fchmod\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "85 symlink\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "86 readlink\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "87 lstat\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "88 pread\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "89 pwrite\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "90 readv\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "91 writev\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "92 fcntl\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "93 utime\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "94 futime\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "95 simplefs_unmount\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "96 fsync\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "97 fdatasync\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "98 sync\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "99 fchdir\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "100 freaddir\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "101 time\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "102 gettimeofday\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "103 nanosleep\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "104 getrandom\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "105 openat\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "106 fstatat\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "107 mkdirat\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "108 unlinkat\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "109 renameat\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "110 accessat\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "111 route_del4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "112 arp_del4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "113 socket_udp4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "114 bind_udp4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "115 sendto_udp4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "116 recvfrom_udp4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "117 connect_udp4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "118 getsockname_udp4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "119 getpeername_udp4\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "120 select\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "121 getuid\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "122 getgid\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "123 setuid\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "124 setgid\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "125 chown\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "126 fchown\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "127 exec\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "128 exec_args\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "129 fork\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "130 pipe2\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "131 kill_signal\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "132 getpgid\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "133 setpgid\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "134 getsid\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "135 setsid\n");
        append_str(node->data, &pos, RAMFS_FILE_CAP, "136 kill_group\n");
    } else {
        uint32_t pid;
        const char *leaf;
        process_t *p;
        if (!parse_proc_process_path_raw(node->path, &pid, &leaf) || !leaf[0]) return;
        p = process_find(pid);
        if (!p) return;
        if (kstrcmp(leaf, "status") == 0) {
            proc_reset(node, &pos);
            append_str(node->data, &pos, RAMFS_FILE_CAP, "Name: ");
            append_str(node->data, &pos, RAMFS_FILE_CAP, p->name);
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
            proc_line(node->data, &pos, "Pid: ", p->pid, "");
            proc_line(node->data, &pos, "PPid: ", p->ppid, "");
            proc_line(node->data, &pos, "PGid: ", p->pgid, "");
            proc_line(node->data, &pos, "Sid: ", p->sid, "");
            proc_line(node->data, &pos, "Uid: ", p->uid, "");
            proc_line(node->data, &pos, "Gid: ", p->gid, "");
            append_str(node->data, &pos, RAMFS_FILE_CAP, "State: ");
            append_str(node->data, &pos, RAMFS_FILE_CAP, process_state_name(p->state));
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
            proc_line(node->data, &pos, "Argc: ", p->argc, "");
            append_str(node->data, &pos, RAMFS_FILE_CAP, "Cwd: ");
            append_str(node->data, &pos, RAMFS_FILE_CAP, p->cwd);
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
            proc_hex_line(node->data, &pos, "Entry: ", p->entry);
            proc_hex_line(node->data, &pos, "UserStack: ", p->user_stack);
            proc_hex_line(node->data, &pos, "PageDirectory: ", p->page_dir);
            proc_hex_line(node->data, &pos, "HeapStart: ", p->heap_start);
            proc_hex_line(node->data, &pos, "HeapBreak: ", p->heap_break);
            proc_line(node->data, &pos, "EnvCount: ", (uint32_t)process_env_count(p), "");
            proc_line(node->data, &pos, "Umask: ", process_umask_get(p), "");
        } else if (kstrcmp(leaf, "fds") == 0) {
            uint32_t fd;
            proc_reset(node, &pos);
            append_str(node->data, &pos, RAMFS_FILE_CAP, "FD VFSFD FLAGS\n");
            for (fd = 0; fd < PROCESS_MAX_FDS; fd++) {
                if (p->fds[fd] < 0) continue;
                append_dec(node->data, &pos, RAMFS_FILE_CAP, fd);
                append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
                append_dec(node->data, &pos, RAMFS_FILE_CAP, (uint32_t)p->fds[fd]);
                append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
                append_str(node->data, &pos, RAMFS_FILE_CAP,
                           (p->fd_flags[fd] & PROCESS_FD_CLOEXEC) ? "cloexec" : "-");
                append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
            }
        } else if (kstrcmp(leaf, "environ") == 0) {
            proc_reset(node, &pos);
            proc_env_lines(node->data, &pos, p);
        } else if (kstrcmp(leaf, "maps") == 0) {
            proc_reset(node, &pos);
            append_str(node->data, &pos, RAMFS_FILE_CAP, "START END FLAGS NAME\n");
            proc_hex_line(node->data, &pos, "UserBase: ", USER_BASE);
            proc_hex_line(node->data, &pos, "UserTop: ", USER_STACK_TOP);
            proc_hex_line(node->data, &pos, "UserMmapBase: ", USER_MMAP_BASE);
            proc_hex_line(node->data, &pos, "UserMmapTop: ", USER_MMAP_TOP);
            proc_hex_line(node->data, &pos, "UserStackBase: ", USER_STACK_TOP - USER_STACK_SIZE);
            proc_hex_line(node->data, &pos, "UserStackTop: ", USER_STACK_TOP);
            proc_hex_line(node->data, &pos, "HeapStart: ", p->heap_start);
            proc_hex_line(node->data, &pos, "HeapBreak: ", p->heap_break);
            append_str(node->data, &pos, RAMFS_FILE_CAP, "Kernel: inaccessible\n");
            proc_user_maps(node->data, &pos, p);
        } else if (kstrcmp(leaf, "cwd") == 0) {
            proc_reset(node, &pos);
            append_str(node->data, &pos, RAMFS_FILE_CAP, p->cwd);
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        } else if (kstrcmp(leaf, "cmdline") == 0) {
            proc_reset(node, &pos);
            proc_cmdline(node->data, &pos, p);
        } else if (kstrcmp(leaf, "children") == 0) {
            proc_reset(node, &pos);
            proc_children(node->data, &pos, p);
        } else if (kstrcmp(leaf, "exe") == 0) {
            proc_reset(node, &pos);
            append_str(node->data, &pos, RAMFS_FILE_CAP, p->name);
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        } else if (kstrcmp(leaf, "stat") == 0) {
            uint32_t fd_count = 0;
            uint32_t fd;
            proc_reset(node, &pos);
            for (fd = 0; fd < PROCESS_MAX_FDS; fd++) {
                if (p->fds[fd] >= 0) fd_count++;
            }
            append_str(node->data, &pos, RAMFS_FILE_CAP, "PID PPID PGID SID UID GID STATE HEAP ARGC ENVC FD NAME\n");
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->pid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->ppid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->pgid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->sid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->uid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->gid);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, process_state_name(p->state));
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_hex32(node->data, &pos, RAMFS_FILE_CAP, p->heap_break);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, p->argc);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, (uint32_t)process_env_count(p));
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_dec(node->data, &pos, RAMFS_FILE_CAP, fd_count);
            append_char(node->data, &pos, RAMFS_FILE_CAP, ' ');
            append_str(node->data, &pos, RAMFS_FILE_CAP, p->name);
            append_char(node->data, &pos, RAMFS_FILE_CAP, '\n');
        } else {
            return;
        }
    }
    node->size = pos;
}

static int path_parent_exists(const char *path) {
    char parent[VFS_MAX_PATH];
    uint32_t len = kstrlen(path);
    uint32_t i;
    if (!path || path[0] != '/' || len == 0 || len >= VFS_MAX_PATH) return 0;
    if (len == 1) return 1;
    for (i = 0; i < len && i + 1 < VFS_MAX_PATH; i++) parent[i] = path[i];
    parent[i] = 0;
    while (i > 1 && parent[i - 1] != '/') i--;
    if (i == 1) parent[1] = 0;
    else parent[i - 1] = 0;
    for (i = 0; i < RAMFS_MAX_NODES; i++) {
        if (g_nodes[i].used &&
            g_nodes[i].type == VFS_NODE_DIR &&
            kstrcmp(g_nodes[i].path, parent) == 0) return 1;
    }
    return 0;
}

static int path_valid_for_create(const char *path) {
    uint32_t len;
    uint32_t i;
    uint32_t component_len = 0;
    uint32_t component_start = 1;
    if (!path || path[0] != '/') return 0;
    len = kstrlen(path);
    if (len <= 1 || len >= VFS_MAX_PATH) return 0;
    for (i = 1; i < len; i++) {
        if (path[i] == '/') {
            if (component_len == 0 || component_len >= VFS_MAX_NAME) return 0;
            if ((component_len == 1 && path[component_start] == '.') ||
                (component_len == 2 && path[component_start] == '.' && path[component_start + 1] == '.')) return 0;
            component_len = 0;
            component_start = i + 1;
        } else {
            component_len++;
        }
    }
    if (component_len == 0 || component_len >= VFS_MAX_NAME) return 0;
    if ((component_len == 1 && path[component_start] == '.') ||
        (component_len == 2 && path[component_start] == '.' && path[component_start + 1] == '.')) return 0;
    return 1;
}

static int path_is_child(const char *dir, const char *path, const char **name) {
    uint32_t dl = kstrlen(dir);
    const char *p;
    if (dl == 1 && dir[0] == '/') {
        if (path[0] != '/' || path[1] == 0) return 0;
        p = path + 1;
    } else {
        if (kstrcmp(dir, path) == 0) return 0;
        if (kstrlen(path) <= dl || path[dl] != '/') return 0;
        {
            uint32_t i;
            for (i = 0; i < dl; i++) if (dir[i] != path[i]) return 0;
        }
        p = path + dl + 1;
    }
    if (!*p) return 0;
    {
        const char *q = p;
        while (*q) {
            if (*q == '/') return 0;
            q++;
        }
    }
    *name = p;
    return 1;
}

static int path_is_descendant(const char *dir, const char *path) {
    uint32_t dir_len = kstrlen(dir);
    uint32_t i;
    if (!dir || !path || dir_len <= 1) return 0;
    if (kstrlen(path) <= dir_len || path[dir_len] != '/') return 0;
    for (i = 0; i < dir_len; i++) {
        if (dir[i] != path[i]) return 0;
    }
    return 1;
}

static int renamed_descendant_path(const char *old_dir,
                                   const char *new_dir,
                                   const char *old_path,
                                   char *out,
                                   uint32_t max) {
    uint32_t old_len = kstrlen(old_dir);
    uint32_t new_len = kstrlen(new_dir);
    const char *suffix = old_path + old_len;
    uint32_t suffix_len = kstrlen(suffix);
    uint32_t i;
    uint32_t pos = 0;
    if (!path_is_descendant(old_dir, old_path) || new_len + suffix_len >= max) return -1;
    for (i = 0; i < new_len; i++) out[pos++] = new_dir[i];
    for (i = 0; i <= suffix_len; i++) out[pos++] = suffix[i];
    return 0;
}

static ram_node_t *find_node(const char *path) {
    uint32_t i;
    if (!path) return 0;
    for (i = 0; i < RAMFS_MAX_NODES; i++) {
        if (g_nodes[i].used && kstrcmp(path, g_nodes[i].path) == 0) return &g_nodes[i];
    }
    return 0;
}

static int component_equals(const char *s, uint32_t len, const char *lit) {
    uint32_t i = 0;
    while (i < len && lit[i] && s[i] == lit[i]) i++;
    return i == len && lit[i] == 0;
}

static void pop_normalized_component(char *buf, uint32_t *pos) {
    if (*pos <= 1) {
        *pos = 1;
        buf[0] = '/';
        buf[1] = 0;
        return;
    }
    while (*pos > 1 && buf[*pos - 1] != '/') (*pos)--;
    if (*pos > 1) (*pos)--;
    buf[*pos] = 0;
}

static int append_normalized_component(char *buf,
                                       uint32_t *pos,
                                       uint32_t max,
                                       const char *s,
                                       uint32_t len) {
    uint32_t i;
    if (len == 0 || len >= VFS_MAX_NAME) return -1;
    if (*pos > 1) {
        if (*pos + 1 >= max) return -1;
        append_char(buf, pos, max, '/');
    }
    if (*pos + len >= max) return -1;
    for (i = 0; i < len; i++) append_char(buf, pos, max, s[i]);
    return 0;
}

static int parent_path_of(const char *path, char *out, uint32_t max) {
    uint32_t len = kstrlen(path);
    uint32_t i;
    if (!path || !out || max < 2 || path[0] != '/' || len == 0 || len >= max) return -1;
    if (len == 1) {
        out[0] = '/';
        out[1] = 0;
        return 0;
    }
    for (i = 0; i < len && i + 1 < max; i++) out[i] = path[i];
    out[i] = 0;
    while (i > 1 && out[i - 1] != '/') i--;
    if (i == 1) out[1] = 0;
    else out[i - 1] = 0;
    return 0;
}

static int normalize_path_candidate(const char *base_dir,
                                    const char *target,
                                    char *out,
                                    uint32_t max) {
    char tmp[VFS_MAX_PATH];
    uint32_t tmp_pos = 0;
    uint32_t out_pos = 1;
    uint32_t i = 0;
    if (!target || !target[0] || !out || max < 2) return -1;
    if (target[0] == '/') {
        if (kstrlen(target) >= sizeof(tmp)) return -1;
        kcopy(tmp, target, sizeof(tmp));
    } else {
        uint32_t base_len;
        if (!base_dir || base_dir[0] != '/') return -1;
        base_len = kstrlen(base_dir);
        if (base_len + 1U + kstrlen(target) >= sizeof(tmp)) return -1;
        kcopy(tmp, base_dir, sizeof(tmp));
        tmp_pos = kstrlen(tmp);
        if (tmp_pos > 1) append_char(tmp, &tmp_pos, sizeof(tmp), '/');
        append_str(tmp, &tmp_pos, sizeof(tmp), target);
    }
    out[0] = '/';
    out[1] = 0;
    while (tmp[i]) {
        uint32_t start;
        uint32_t len;
        while (tmp[i] == '/') i++;
        start = i;
        while (tmp[i] && tmp[i] != '/') i++;
        len = i - start;
        if (len == 0 || component_equals(tmp + start, len, ".")) continue;
        if (component_equals(tmp + start, len, "..")) {
            pop_normalized_component(out, &out_pos);
            continue;
        }
        if (append_normalized_component(out, &out_pos, max, tmp + start, len) < 0)
            return -1;
    }
    return 0;
}

static int symlink_target_path(const ram_node_t *node, char *out, uint32_t max) {
    char parent[VFS_MAX_PATH];
    if (!node || node->type != VFS_NODE_SYMLINK || !out) return -1;
    if (node->size == 0 || node->size >= RAMFS_FILE_CAP) return -1;
    if (parent_path_of(node->path, parent, sizeof(parent)) < 0) return -1;
    return normalize_path_candidate(parent, node->data, out, max);
}

static int resolve_final_symlink_path(const char *path, char *out, uint32_t max) {
    char current[VFS_MAX_PATH];
    uint32_t depth;
    int followed = 0;
    if (!path || !out || max == 0 || kstrlen(path) >= sizeof(current)) return -1;
    kcopy(current, path, sizeof(current));
    for (depth = 0; depth < 8; depth++) {
        ram_node_t *node = find_node(current);
        char next[VFS_MAX_PATH];
        if (!node || node->type != VFS_NODE_SYMLINK) {
            if (followed) kcopy(out, current, max);
            return followed;
        }
        if (symlink_target_path(node, next, sizeof(next)) < 0) return -1;
        if (kstrcmp(next, current) == 0) return -1;
        kcopy(current, next, sizeof(current));
        followed = 1;
    }
    return -1;
}

static ram_node_t *alloc_node_with_mode(const char *path, vfs_node_type_t type, uint32_t mode) {
    uint32_t i;
    uint32_t now;
    if (!path_valid_for_create(path)) return 0;
    if (find_node(path) || !path_parent_exists(path)) return 0;
    now = timer_ms();
    for (i = 0; i < RAMFS_MAX_NODES; i++) {
        if (!g_nodes[i].used) {
            kclear_node(&g_nodes[i]);
            g_nodes[i].used = 1;
            g_nodes[i].type = type;
            kcopy(g_nodes[i].path, path, VFS_MAX_PATH);
            g_nodes[i].mode = sanitized_mode(type, mode);
            g_nodes[i].uid = process_uid_get(process_current());
            g_nodes[i].gid = process_gid_get(process_current());
            g_nodes[i].created_ms = now;
            g_nodes[i].modified_ms = now;
            g_nodes[i].accessed_ms = now;
            return &g_nodes[i];
        }
    }
    return 0;
}

static ram_node_t *alloc_node(const char *path, vfs_node_type_t type) {
    return alloc_node_with_mode(path, type, default_mode_for_path(type, path));
}

static int reserve_free_fd(void) {
    int fd;
    uint32_t flags = vfs_irq_save();
    for (fd = 0; fd < VFS_MAX_FDS; fd++) {
        if (!g_fds[fd].used) {
            kclear_fd(&g_fds[fd]);
            g_fds[fd].used = 1;
            g_fds[fd].refcount = 0;
            vfs_irq_restore(flags);
            return fd;
        }
    }
    vfs_irq_restore(flags);
    return -1;
}

static void release_reserved_fd(int fd) {
    uint32_t flags;
    if (fd < 0 || fd >= VFS_MAX_FDS) return;
    flags = vfs_irq_save();
    if (g_fds[fd].used && g_fds[fd].refcount == 0) kclear_fd(&g_fds[fd]);
    vfs_irq_restore(flags);
}

static int udp_port_in_use_locked(uint16_t port, int except_fd) {
    int i;
    if (port == 0) return 1;
    for (i = 0; i < VFS_MAX_FDS; i++) {
        if (i == except_fd) continue;
        if (g_fds[i].used &&
            g_fds[i].backend == VFS_BACKEND_UDP4 &&
            g_fds[i].udp_bound &&
            g_fds[i].udp_port == port) return 1;
    }
    return 0;
}

static int udp_bind_locked(int fd, uint16_t port) {
    uint32_t candidate;
    if (fd < 0 || fd >= VFS_MAX_FDS ||
        !g_fds[fd].used ||
        g_fds[fd].backend != VFS_BACKEND_UDP4 ||
        g_fds[fd].udp_bound) return -1;
    if (port) {
        if (udp_port_in_use_locked(port, fd)) return -1;
        g_fds[fd].udp_bound = 1;
        g_fds[fd].udp_port = port;
        return 0;
    }
    for (candidate = VFS_UDP_EPHEMERAL_START; candidate <= 65535U; candidate++) {
        if (!udp_port_in_use_locked((uint16_t)candidate, fd)) {
            g_fds[fd].udp_bound = 1;
            g_fds[fd].udp_port = (uint16_t)candidate;
            return 0;
        }
    }
    return -1;
}

static uint32_t udp_socket_count(void) {
    uint32_t flags;
    uint32_t count = 0;
    int i;
    flags = vfs_irq_save();
    for (i = 0; i < VFS_MAX_FDS; i++) {
        if (g_fds[i].used && g_fds[i].backend == VFS_BACKEND_UDP4) count++;
    }
    vfs_irq_restore(flags);
    return count;
}

static int is_simplefs_path(const char *path) {
    const char *mount = simplefs_mount_path();
    uint32_t ml;
    uint32_t i;
    if (!path || !simplefs_mounted() || !mount || mount[0] != '/') return 0;
    ml = kstrlen(mount);
    if (ml == 1) return path[0] == '/' && path[1] != 0;
    for (i = 0; i < ml; i++) {
        if (path[i] != mount[i]) return 0;
    }
    return path[ml] == 0 || path[ml] == '/';
}

static int is_proc_path(const char *path) {
    return path &&
           path[0] == '/' && path[1] == 'p' && path[2] == 'r' &&
           path[3] == 'o' && path[4] == 'c' &&
           (path[5] == 0 || path[5] == '/');
}

static int is_dev_path(const char *path) {
    return path &&
           path[0] == '/' && path[1] == 'd' && path[2] == 'e' &&
           path[3] == 'v' &&
           (path[4] == 0 || path[4] == '/');
}

static int device_block_index(const char *path) {
    if (!path || path[0] != '/' || path[1] != 'd' || path[2] != 'e' ||
        path[3] != 'v' || path[4] != '/') return -1;
    return block_find(path + 5);
}

static int device_net_index(const char *path) {
    uint32_t index = 0;
    uint32_t i = 8;
    if (!path || path[0] != '/' || path[1] != 'd' || path[2] != 'e' ||
        path[3] != 'v' || path[4] != '/' ||
        path[5] != 'n' || path[6] != 'e' || path[7] != 't') return -1;
    if (path[i] < '0' || path[i] > '9') return -1;
    while (path[i] >= '0' && path[i] <= '9') {
        uint32_t digit = (uint32_t)(path[i] - '0');
        if (index > (0xFFFFFFFFU - digit) / 10U) return -1;
        index = index * 10U + digit;
        i++;
    }
    if (path[i] != 0 || index >= netif_count()) return -1;
    return (int)index;
}

static int device_is_kmsg(const char *path) {
    return kstrcmp(path, "/dev/kmsg") == 0;
}

static int device_is_tty(const char *path) {
    return kstrcmp(path, "/dev/tty0") == 0 ||
           kstrcmp(path, "/dev/tty") == 0 ||
           kstrcmp(path, "/dev/console") == 0;
}

static int device_is_null(const char *path) {
    return kstrcmp(path, "/dev/null") == 0;
}

static int device_is_zero(const char *path) {
    return kstrcmp(path, "/dev/zero") == 0;
}

static int device_is_full(const char *path) {
    return kstrcmp(path, "/dev/full") == 0;
}

static int device_is_urandom(const char *path) {
    return kstrcmp(path, "/dev/urandom") == 0 ||
           kstrcmp(path, "/dev/random") == 0;
}

static uint8_t urandom_byte(void) {
    if (g_urandom_state == 0) g_urandom_state = 0x4D594F53U ^ timer_ticks();
    g_urandom_state = g_urandom_state * 1664525U + 1013904223U + timer_ticks();
    return (uint8_t)(g_urandom_state >> 24);
}

static int read_zero_stream(void *buf, uint32_t len) {
    uint32_t i;
    if (!buf) return -1;
    for (i = 0; i < len; i++) ((uint8_t *)buf)[i] = 0;
    return (int)len;
}

static int read_urandom_stream(void *buf, uint32_t len) {
    uint32_t i;
    if (!buf) return -1;
    for (i = 0; i < len; i++) ((uint8_t *)buf)[i] = urandom_byte();
    return (int)len;
}

int vfs_getrandom(void *buf, uint32_t len) {
    return read_urandom_stream(buf, len);
}

static int write_urandom_stream(const void *buf, uint32_t len) {
    uint32_t i;
    if (!buf) return -1;
    for (i = 0; i < len; i++) {
        g_urandom_state ^= (uint32_t)((const uint8_t *)buf)[i] << ((i & 3U) * 8U);
        g_urandom_state = g_urandom_state * 1103515245U + 12345U;
    }
    return (int)len;
}

static int make_dev_net_path(uint32_t index, char *out, uint32_t max) {
    uint32_t pos = 0;
    if (!out || max < 10) return -1;
    append_str(out, &pos, max, "/dev/net");
    append_dec(out, &pos, max, index);
    return out[pos] == 0 ? 0 : -1;
}

static int make_net_name(uint32_t index, char *out, uint32_t max) {
    uint32_t pos = 0;
    if (!out || max < 5) return -1;
    append_str(out, &pos, max, "net");
    append_dec(out, &pos, max, index);
    return out[pos] == 0 ? 0 : -1;
}

static int device_path_exists(const char *path) {
    return device_block_index(path) >= 0 ||
           device_net_index(path) >= 0 ||
           device_is_kmsg(path) ||
           device_is_tty(path) ||
           device_is_null(path) ||
           device_is_zero(path) ||
           device_is_full(path) ||
           device_is_urandom(path);
}

static ram_node_t *materialize_dev_node(const char *path) {
    ram_node_t *node = find_node(path);
    if (node) return node;
    if (!device_path_exists(path)) return 0;
    return alloc_node(path, VFS_NODE_DEV);
}

static ram_node_t *materialize_proc_node(const char *path) {
    uint32_t pid;
    const char *leaf;
    char parent[VFS_MAX_PATH];
    ram_node_t *node = find_node(path);
    ram_node_t *parent_node;
    if (node) return node;
    if (!proc_pid_path_valid(path)) return 0;
    if (!parse_proc_process_path_raw(path, &pid, &leaf)) return 0;
    if (!leaf[0]) return alloc_node(path, VFS_NODE_DIR);
    if (parent_path_of(path, parent, sizeof(parent)) < 0) return 0;
    parent_node = find_node(parent);
    if (!parent_node) parent_node = alloc_node(parent, VFS_NODE_DIR);
    if (!parent_node || parent_node->type != VFS_NODE_DIR) return 0;
    return alloc_node(path, VFS_NODE_FILE);
}

static void fill_dirent(vfs_dirent_t *out,
                        const char *name,
                        vfs_node_type_t type,
                        uint32_t size,
                        uint32_t mode,
                        uint32_t created_ms,
                        uint32_t modified_ms,
                        uint32_t accessed_ms) {
    kcopy(out->name, name ? name : "", VFS_MAX_NAME);
    out->type = type;
    out->size = size;
    out->mode = mode;
    out->uid = 0;
    out->gid = 0;
    out->created_ms = created_ms;
    out->modified_ms = modified_ms;
    out->accessed_ms = accessed_ms;
}

static int fill_dynamic_proc_root_dirent(uint32_t index, vfs_dirent_t *out) {
    uint32_t seen = 0;
    uint32_t i;
    if (!out) return -1;
    if (!find_node("/proc/self")) {
        if (index == 0) {
            fill_dirent(out,
                        "self",
                        VFS_NODE_DIR,
                        0,
                        default_mode_for_path(VFS_NODE_DIR, "/proc/self"),
                        0,
                        0,
                        0);
            return 0;
        }
        index--;
    }
    for (i = 0; i < PROCESS_MAX; i++) {
        const process_t *proc = process_at(i);
        char path[VFS_MAX_PATH];
        uint32_t pos = 0;
        if (!proc) continue;
        if (make_proc_pid_path(proc->pid, path, sizeof(path)) == 0 && find_node(path)) continue;
        if (seen == index) {
            char name[VFS_MAX_NAME];
            name[0] = 0;
            append_dec(name, &pos, VFS_MAX_NAME, proc->pid);
            fill_dirent(out,
                        name,
                        VFS_NODE_DIR,
                        0,
                        default_mode_for_path(VFS_NODE_DIR, "/proc/0"),
                        0,
                        0,
                        0);
            return 0;
        }
        seen++;
    }
    return -1;
}

static int fill_proc_pid_dirent(const char *path, uint32_t index, vfs_dirent_t *out) {
    uint32_t pid;
    const char *leaf;
    static const char *names[] = {
        "status", "fds", "environ", "maps", "cwd", "cmdline", "stat", "children", "exe"
    };
    if (!out || index >= sizeof(names) / sizeof(names[0])) return -1;
    if (!parse_proc_process_path_raw(path, &pid, &leaf) || leaf[0] || !process_find(pid)) return -1;
    fill_dirent(out,
                names[index],
                VFS_NODE_FILE,
                0,
                default_mode_for_path(VFS_NODE_FILE, "/proc/0/status"),
                0,
                0,
                0);
    return 0;
}

static int fill_dynamic_dev_dirent(uint32_t index, vfs_dirent_t *out) {
    uint32_t seen = 0;
    uint32_t i;
    char path[VFS_MAX_PATH];
    ram_node_t *node;
    if (!out) return -1;
    for (i = 0; i < block_count(); i++) {
        const block_device_t *dev = block_at(i);
        if (!dev || !dev->name || !dev->name[0]) continue;
        path[0] = '/';
        path[1] = 'd';
        path[2] = 'e';
        path[3] = 'v';
        path[4] = '/';
        kcopy(path + 5, dev->name, VFS_MAX_PATH - 5);
        node = find_node(path);
        if (node && node->type == VFS_NODE_DEV) continue;
        if (seen == index) {
            fill_dirent(out,
                        dev->name,
                        VFS_NODE_DEV,
                        saturating_mul_u32(dev->sector_count, dev->sector_size),
                        default_mode_for_path(VFS_NODE_DEV, path),
                        0,
                        0,
                        0);
            return 0;
        }
        seen++;
    }
    for (i = 0; i < netif_count(); i++) {
        const netif_t *dev = netif_at(i);
        if (make_dev_net_path(i, path, sizeof(path)) < 0) continue;
        node = find_node(path);
        if (node && node->type == VFS_NODE_DEV) continue;
        if (seen == index) {
            char name[VFS_MAX_NAME];
            make_net_name(i, name, VFS_MAX_NAME);
            fill_dirent(out,
                        name,
                        VFS_NODE_DEV,
                        dev ? dev->mtu : 0,
                        default_mode_for_path(VFS_NODE_DEV, path),
                        0,
                        0,
                        0);
            return 0;
        }
        seen++;
    }
    return -1;
}

static int flags_allow_directory_open(int flags) {
    return (flags & VFS_O_RDWR) == VFS_O_RDONLY &&
           (flags & ~(VFS_O_RDWR | VFS_O_CLOEXEC | VFS_O_NONBLOCK)) == 0;
}

static int read_block_stream(ram_node_t *node, uint32_t *pos, void *buf, uint32_t len) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint32_t done = 0;
    int dev = device_block_index(node->path);
    if (dev < 0) return -1;
    while (done < len) {
        uint32_t lba = *pos / BLOCK_SECTOR_SIZE;
        uint32_t off = *pos % BLOCK_SECTOR_SIZE;
        uint32_t n = BLOCK_SECTOR_SIZE - off;
        uint32_t i;
        if (n > len - done) n = len - done;
        if (block_read((uint32_t)dev, lba, sector, 1) != 1) break;
        for (i = 0; i < n; i++) ((uint8_t *)buf)[done + i] = sector[off + i];
        *pos += n;
        done += n;
    }
    return (int)done;
}

static int write_block_stream(ram_node_t *node, uint32_t *pos, const void *buf, uint32_t len) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    uint32_t done = 0;
    int dev = device_block_index(node->path);
    if (dev < 0) return -1;
    while (done < len) {
        uint32_t lba = *pos / BLOCK_SECTOR_SIZE;
        uint32_t off = *pos % BLOCK_SECTOR_SIZE;
        uint32_t n = BLOCK_SECTOR_SIZE - off;
        uint32_t i;
        if (n > len - done) n = len - done;
        if (block_read((uint32_t)dev, lba, sector, 1) != 1)
            return done ? (int)done : -1;
        for (i = 0; i < n; i++) sector[off + i] = ((const uint8_t *)buf)[done + i];
        if (block_write((uint32_t)dev, lba, sector, 1) != 1)
            return done ? (int)done : -1;
        *pos += n;
        done += n;
    }
    return (int)done;
}

static int read_kmsg_stream(uint32_t *pos, void *buf, uint32_t len) {
    return debug_log_read(pos, (char *)buf, len);
}

static int write_kmsg_stream(const void *buf, uint32_t len) {
    uint32_t i;
    if (!buf) return -1;
    for (i = 0; i < len; i++) debug_putc(((const char *)buf)[i]);
    return (int)len;
}

static int ramfs_resize_node(ram_node_t *node, uint32_t size) {
    uint32_t i;
    uint32_t old_size;
    if (!node || node->type != VFS_NODE_FILE || is_proc_path(node->path) || is_dev_path(node->path))
        return -1;
    if (size > RAMFS_FILE_CAP) return -1;
    old_size = node->size;
    if (size > old_size) {
        for (i = old_size; i < size; i++) node->data[i] = 0;
    } else {
        for (i = size; i < old_size && i < RAMFS_FILE_CAP; i++) node->data[i] = 0;
    }
    node->size = size;
    if (node->size < RAMFS_FILE_CAP) node->data[node->size] = 0;
    node->modified_ms = timer_ms();
    node->accessed_ms = node->modified_ms;
    return 0;
}

static void fill_static_fsinfo(vfs_fsinfo_t *out, const char *name) {
    if (!out) return;
    kcopy(out->name, name, VFS_FS_NAME_MAX);
    out->block_size = 1;
    out->total_blocks = 0;
    out->free_blocks = 0;
    out->total_files = 0;
    out->free_files = 0;
}

static void fill_ramfs_fsinfo(vfs_fsinfo_t *out) {
    uint32_t used = 0;
    uint32_t i;
    if (!out) return;
    for (i = 0; i < RAMFS_MAX_NODES; i++) {
        if (g_nodes[i].used) used++;
    }
    kcopy(out->name, "ramfs", VFS_FS_NAME_MAX);
    out->block_size = RAMFS_FILE_CAP;
    out->total_blocks = RAMFS_MAX_NODES;
    out->free_blocks = RAMFS_MAX_NODES - used;
    out->total_files = RAMFS_MAX_NODES;
    out->free_files = RAMFS_MAX_NODES - used;
}

static int pipe_read_stream(vfs_fd_t *fd, void *buf, uint32_t len) {
    vfs_pipe_t *pipe;
    uint32_t n = 0;
    uint32_t flags;
    if (!fd || !buf || fd->pipe_id < 0 || fd->pipe_id >= VFS_MAX_PIPES) return -1;
    flags = vfs_irq_save();
    pipe = &g_pipes[fd->pipe_id];
    if (!pipe->used || fd->pipe_write) {
        vfs_irq_restore(flags);
        return -1;
    }
    if (pipe->len == 0 && pipe->writers > 0 && (fd->flags & VFS_O_NONBLOCK)) {
        vfs_irq_restore(flags);
        return -1;
    }
    while (n < len && pipe->len > 0) {
        ((char *)buf)[n++] = pipe->data[pipe->head];
        pipe->head = (pipe->head + 1U) % VFS_PIPE_CAP;
        pipe->len--;
    }
    vfs_irq_restore(flags);
    return (int)n;
}

static int pipe_write_stream(vfs_fd_t *fd, const void *buf, uint32_t len) {
    vfs_pipe_t *pipe;
    uint32_t n = 0;
    uint32_t flags;
    if (!fd || !buf || fd->pipe_id < 0 || fd->pipe_id >= VFS_MAX_PIPES) return -1;
    flags = vfs_irq_save();
    pipe = &g_pipes[fd->pipe_id];
    if (!pipe->used || !fd->pipe_write || pipe->readers == 0) {
        vfs_irq_restore(flags);
        return -1;
    }
    if (pipe->len >= VFS_PIPE_CAP && (fd->flags & VFS_O_NONBLOCK)) {
        vfs_irq_restore(flags);
        return -1;
    }
    while (n < len && pipe->len < VFS_PIPE_CAP) {
        uint32_t tail = (pipe->head + pipe->len) % VFS_PIPE_CAP;
        pipe->data[tail] = ((const char *)buf)[n++];
        pipe->len++;
    }
    vfs_irq_restore(flags);
    return (int)n;
}

static void pipe_close_fd_locked(vfs_fd_t *fd) {
    vfs_pipe_t *pipe;
    if (!fd || fd->pipe_id < 0 || fd->pipe_id >= VFS_MAX_PIPES) return;
    pipe = &g_pipes[fd->pipe_id];
    if (!pipe->used) return;
    if (fd->pipe_write) {
        if (pipe->writers > 0) pipe->writers--;
    } else {
        if (pipe->readers > 0) pipe->readers--;
    }
    if (pipe->readers == 0 && pipe->writers == 0) pipe->used = 0;
}

static void pipe_close_fd(vfs_fd_t *fd) {
    uint32_t flags;
    if (!fd || fd->pipe_id < 0 || fd->pipe_id >= VFS_MAX_PIPES) return;
    flags = vfs_irq_save();
    pipe_close_fd_locked(fd);
    vfs_irq_restore(flags);
}

void vfs_init(void) {
    uint32_t i;
    uint32_t j;
    uint32_t now = timer_ms();
    for (i = 0; i < RAMFS_MAX_NODES; i++) kclear_node(&g_nodes[i]);
    for (i = 0; i < VFS_MAX_FDS; i++) kclear_fd(&g_fds[i]);
    for (i = 0; i < VFS_MAX_PIPES; i++) g_pipes[i].used = 0;
    for (i = 0; i < sizeof(g_seed_nodes) / sizeof(g_seed_nodes[0]); i++) {
        g_nodes[i].used = 1;
        g_nodes[i].type = g_seed_nodes[i].type;
        kcopy(g_nodes[i].path, g_seed_nodes[i].path, VFS_MAX_PATH);
        g_nodes[i].mode = default_mode_for_path(g_nodes[i].type, g_nodes[i].path);
        g_nodes[i].uid = 0;
        g_nodes[i].gid = 0;
        g_nodes[i].created_ms = now;
        g_nodes[i].modified_ms = now;
        g_nodes[i].accessed_ms = now;
        if (g_seed_nodes[i].data) {
            g_nodes[i].size = g_seed_nodes[i].size ? g_seed_nodes[i].size :
                kstrlen((const char *)g_seed_nodes[i].data);
            if (g_nodes[i].size >= RAMFS_FILE_CAP) g_nodes[i].size = RAMFS_FILE_CAP - 1;
            kmemcpy(g_nodes[i].data, (const char *)g_seed_nodes[i].data, g_nodes[i].size);
            g_nodes[i].data[g_nodes[i].size] = 0;
        }
        for (j = g_nodes[i].size + 1; j < RAMFS_FILE_CAP; j++) g_nodes[i].data[j] = 0;
    }
}

int vfs_mount_ramfs(void) {
    debug_puts("[vfs] ramfs mounted\n");
    return 0;
}

int vfs_create(const char *path) {
    if (is_proc_path(path) || is_dev_path(path)) return -1;
    if (is_simplefs_path(path)) {
        if (simplefs_create(path) < 0) return -1;
        return simplefs_chmod(path, creation_mode_for_type(VFS_NODE_FILE));
    }
    return alloc_node_with_mode(path, VFS_NODE_FILE, creation_mode_for_type(VFS_NODE_FILE)) ? 0 : -1;
}

int vfs_mkdir(const char *path) {
    if (is_proc_path(path) || is_dev_path(path)) return -1;
    if (is_simplefs_path(path)) {
        if (simplefs_mkdir(path) < 0) return -1;
        return simplefs_chmod(path, creation_mode_for_type(VFS_NODE_DIR));
    }
    return alloc_node_with_mode(path, VFS_NODE_DIR, creation_mode_for_type(VFS_NODE_DIR)) ? 0 : -1;
}

int vfs_symlink(const char *target, const char *link_path) {
    ram_node_t *node;
    uint32_t len;
    uint32_t i;
    if (!target || !target[0] || !link_path) return -1;
    len = kstrlen(target);
    if (len == 0 || len >= RAMFS_FILE_CAP) return -1;
    if (is_proc_path(link_path) || is_dev_path(link_path) || is_simplefs_path(link_path)) return -1;
    node = alloc_node_with_mode(link_path,
                                VFS_NODE_SYMLINK,
                                default_mode_for_path(VFS_NODE_SYMLINK, link_path));
    if (!node) return -1;
    for (i = 0; i < len; i++) node->data[i] = target[i];
    node->data[len] = 0;
    node->size = len;
    node->modified_ms = timer_ms();
    node->accessed_ms = node->modified_ms;
    return 0;
}

int vfs_readlink(const char *path, char *out, uint32_t max) {
    ram_node_t *node;
    uint32_t i;
    if (!path || !out || max == 0) return -1;
    node = find_node(path);
    if (!node || node->type != VFS_NODE_SYMLINK || max <= node->size) return -1;
    for (i = 0; i < node->size; i++) out[i] = node->data[i];
    out[node->size] = 0;
    node->accessed_ms = timer_ms();
    return (int)node->size;
}

int vfs_rmdir(const char *path) {
    ram_node_t *node;
    vfs_dirent_t ent;
    if (!path || is_proc_path(path) || is_dev_path(path) || kstrcmp(path, "/") == 0) return -1;
    if (is_simplefs_path(path)) {
        if (simplefs_stat(path, &ent) < 0 || ent.type != VFS_NODE_DIR) return -1;
        return simplefs_unlink(path);
    }
    node = find_node(path);
    if (!node || node->type != VFS_NODE_DIR) return -1;
    return vfs_unlink(path);
}

int vfs_unlink(const char *path) {
    ram_node_t *node;
    uint32_t i;
    if (!path) return -1;
    node = find_node(path);
    if (is_proc_path(path)) return -1;
    if (is_simplefs_path(path)) return simplefs_unlink(path);
    if (!node || kstrcmp(path, "/") == 0 || node->type == VFS_NODE_DEV) return -1;
    if (node->type == VFS_NODE_DIR) {
        for (i = 0; i < RAMFS_MAX_NODES; i++) {
            const char *name = 0;
            if (g_nodes[i].used && path_is_child(path, g_nodes[i].path, &name)) return -1;
        }
    }
    for (i = 0; i < VFS_MAX_FDS; i++) {
        if (g_fds[i].used && g_fds[i].node == node) return -1;
    }
    kclear_node(node);
    return 0;
}

int vfs_rename(const char *old_path, const char *new_path) {
    ram_node_t *old_node;
    uint32_t i;
    if (!path_valid_for_create(old_path) || !path_valid_for_create(new_path)) return -1;
    if (is_proc_path(old_path) || is_proc_path(new_path) ||
        is_dev_path(old_path) || is_dev_path(new_path)) return -1;
    if (is_simplefs_path(old_path) || is_simplefs_path(new_path)) {
        if (!is_simplefs_path(old_path) || !is_simplefs_path(new_path)) return -1;
        return simplefs_rename(old_path, new_path);
    }
    old_node = find_node(old_path);
    if (!old_node || find_node(new_path) || !path_parent_exists(new_path)) return -1;
    if (kstrcmp(old_path, "/") == 0 || old_node->type == VFS_NODE_DEV) return -1;
    if (old_node->type == VFS_NODE_DIR) {
        for (i = 0; i < RAMFS_MAX_NODES; i++) {
            char renamed[VFS_MAX_PATH];
            if (!g_nodes[i].used || !path_is_descendant(old_path, g_nodes[i].path)) continue;
            if (renamed_descendant_path(old_path, new_path, g_nodes[i].path, renamed, sizeof(renamed)) < 0)
                return -1;
            if (find_node(renamed)) return -1;
        }
        if (path_is_descendant(old_path, new_path)) return -1;
        for (i = 0; i < RAMFS_MAX_NODES; i++) {
            char renamed[VFS_MAX_PATH];
            if (!g_nodes[i].used || !path_is_descendant(old_path, g_nodes[i].path)) continue;
            if (renamed_descendant_path(old_path, new_path, g_nodes[i].path, renamed, sizeof(renamed)) < 0)
                return -1;
            kcopy(g_nodes[i].path, renamed, VFS_MAX_PATH);
        }
    }
    kcopy(old_node->path, new_path, VFS_MAX_PATH);
    return 0;
}

int vfs_open(const char *path, int flags) {
    ram_node_t *node;
    vfs_dirent_t dir_ent;
    char resolved[VFS_MAX_PATH];
    int open_flags = flags & ~VFS_O_CLOEXEC;
    int followed;
    int fd;
    uint32_t proc_pid;
    const char *proc_leaf;
    if (!path) return -1;
    followed = resolve_final_symlink_path(path, resolved, sizeof(resolved));
    if (followed < 0) return -1;
    if (followed > 0) path = resolved;
    if (parse_proc_process_path_raw(path, &proc_pid, &proc_leaf) && !proc_pid_path_valid(path)) return -1;
    node = find_node(path);
    if (!flags_valid(flags)) return -1;
    if (!node && is_dev_path(path)) node = materialize_dev_node(path);
    if (!node && is_proc_path(path)) node = materialize_proc_node(path);
    fd = reserve_free_fd();
    if (fd < 0) return -1;
    if ((node && node->type == VFS_NODE_DIR) ||
        (is_simplefs_path(path) && simplefs_stat(path, &dir_ent) == 0 && dir_ent.type == VFS_NODE_DIR)) {
        if (!flags_allow_directory_open(flags)) {
            release_reserved_fd(fd);
            return -1;
        }
        g_fds[fd].refcount = 1;
        g_fds[fd].backend = 3;
        g_fds[fd].node = (node && node->type == VFS_NODE_DIR) ? node : 0;
        g_fds[fd].simple_fd = -1;
        g_fds[fd].pipe_id = -1;
        g_fds[fd].pipe_write = 0;
        g_fds[fd].pos = 0;
        g_fds[fd].flags = open_flags;
        kcopy(g_fds[fd].path, path, VFS_MAX_PATH);
        return fd;
    }
    if (is_simplefs_path(path) && !(node && node->type == VFS_NODE_DIR)) {
        vfs_dirent_t existing;
        int existed = simplefs_stat(path, &existing) == 0;
        int sfd = simplefs_open(path, open_flags);
        if (sfd < 0) {
            release_reserved_fd(fd);
            return -1;
        }
        if (!existed && (open_flags & VFS_O_CREAT))
            (void)simplefs_chmod(path, creation_mode_for_type(VFS_NODE_FILE));
        g_fds[fd].refcount = 1;
        g_fds[fd].backend = 1;
        g_fds[fd].node = 0;
        g_fds[fd].simple_fd = sfd;
        g_fds[fd].pipe_id = -1;
        g_fds[fd].pipe_write = 0;
        g_fds[fd].pos = 0;
        g_fds[fd].flags = open_flags;
        kcopy(g_fds[fd].path, path, VFS_MAX_PATH);
        return fd;
    }
    if (!node && (open_flags & VFS_O_CREAT)) {
        if (is_proc_path(path) || is_dev_path(path)) {
            release_reserved_fd(fd);
            return -1;
        }
        node = alloc_node_with_mode(path, VFS_NODE_FILE, creation_mode_for_type(VFS_NODE_FILE));
    }
    if (!node || node->type == VFS_NODE_DIR) {
        release_reserved_fd(fd);
        return -1;
    }
    if (!flags_allowed_by_owner(open_flags, node->mode, node->uid, node->gid)) {
        release_reserved_fd(fd);
        return -1;
    }
    if (is_proc_path(path) && (open_flags & VFS_O_WRONLY)) {
        release_reserved_fd(fd);
        return -1;
    }
    if ((open_flags & VFS_O_TRUNC) && node->type == VFS_NODE_FILE && !is_proc_path(path)) {
        node->size = 0;
        node->data[0] = 0;
        node->modified_ms = timer_ms();
        node->accessed_ms = node->modified_ms;
    }
    sync_proc_node(node);
    g_fds[fd].refcount = 1;
    g_fds[fd].backend = 0;
    g_fds[fd].node = node;
    g_fds[fd].simple_fd = -1;
    g_fds[fd].pipe_id = -1;
    g_fds[fd].pipe_write = 0;
    g_fds[fd].pos = (open_flags & VFS_O_APPEND) ? node->size : 0;
    g_fds[fd].flags = open_flags;
    kcopy(g_fds[fd].path, path, VFS_MAX_PATH);
    return fd;
}

int vfs_pipe(int out_fds[2]) {
    int pipe_id = -1;
    int read_fd = -1;
    int write_fd = -1;
    int i;
    uint32_t flags;
    if (!out_fds) return -1;
    flags = vfs_irq_save();
    for (i = 0; i < VFS_MAX_PIPES; i++) {
        if (!g_pipes[i].used) {
            pipe_id = i;
            break;
        }
    }
    if (pipe_id < 0) {
        vfs_irq_restore(flags);
        return -1;
    }
    for (i = 0; i < VFS_MAX_FDS; i++) {
        if (!g_fds[i].used) {
            if (read_fd < 0) read_fd = i;
            else {
                write_fd = i;
                break;
            }
        }
    }
    if (read_fd < 0 || write_fd < 0) {
        vfs_irq_restore(flags);
        return -1;
    }
    g_pipes[pipe_id].used = 1;
    g_pipes[pipe_id].head = 0;
    g_pipes[pipe_id].len = 0;
    g_pipes[pipe_id].readers = 1;
    g_pipes[pipe_id].writers = 1;
    g_fds[read_fd].used = 1;
    g_fds[read_fd].refcount = 1;
    g_fds[read_fd].backend = 2;
    g_fds[read_fd].node = 0;
    g_fds[read_fd].simple_fd = -1;
    g_fds[read_fd].pipe_id = pipe_id;
    g_fds[read_fd].pipe_write = 0;
    g_fds[read_fd].udp_bound = 0;
    g_fds[read_fd].udp_peer_set = 0;
    g_fds[read_fd].udp_peer_ipv4 = 0;
    g_fds[read_fd].udp_ifindex = 0;
    g_fds[read_fd].udp_port = 0;
    g_fds[read_fd].udp_peer_port = 0;
    g_fds[read_fd].pos = 0;
    g_fds[read_fd].flags = VFS_O_RDONLY;
    kcopy(g_fds[read_fd].path, "pipe", VFS_MAX_PATH);
    g_fds[write_fd].used = 1;
    g_fds[write_fd].refcount = 1;
    g_fds[write_fd].backend = 2;
    g_fds[write_fd].node = 0;
    g_fds[write_fd].simple_fd = -1;
    g_fds[write_fd].pipe_id = pipe_id;
    g_fds[write_fd].pipe_write = 1;
    g_fds[write_fd].udp_bound = 0;
    g_fds[write_fd].udp_peer_set = 0;
    g_fds[write_fd].udp_peer_ipv4 = 0;
    g_fds[write_fd].udp_ifindex = 0;
    g_fds[write_fd].udp_port = 0;
    g_fds[write_fd].udp_peer_port = 0;
    g_fds[write_fd].pos = 0;
    g_fds[write_fd].flags = VFS_O_WRONLY;
    kcopy(g_fds[write_fd].path, "pipe", VFS_MAX_PATH);
    out_fds[0] = read_fd;
    out_fds[1] = write_fd;
    vfs_irq_restore(flags);
    return 0;
}

int vfs_socket_udp4(void) {
    int fd = reserve_free_fd();
    if (fd < 0) return -1;
    g_fds[fd].refcount = 1;
    g_fds[fd].backend = VFS_BACKEND_UDP4;
    g_fds[fd].node = 0;
    g_fds[fd].simple_fd = -1;
    g_fds[fd].pipe_id = -1;
    g_fds[fd].pipe_write = 0;
    g_fds[fd].udp_bound = 0;
    g_fds[fd].udp_peer_set = 0;
    g_fds[fd].udp_peer_ipv4 = 0;
    g_fds[fd].udp_ifindex = 0;
    g_fds[fd].udp_port = 0;
    g_fds[fd].udp_peer_port = 0;
    g_fds[fd].pos = 0;
    g_fds[fd].flags = VFS_O_RDWR;
    kcopy(g_fds[fd].path, "socket:udp4", VFS_MAX_PATH);
    return fd;
}

int vfs_socket_bind_udp4(int fd, uint16_t port) {
    uint32_t flags = vfs_irq_save();
    int ret = udp_bind_locked(fd, port);
    vfs_irq_restore(flags);
    return ret;
}

int vfs_socket_connect_udp4(int fd, uint32_t dst_ipv4, uint16_t dst_port) {
    uint32_t flags;
    uint32_t ifindex = 0;
    if (dst_ipv4 == 0 || dst_port == 0) return -1;
    if (net_route_lookup4(dst_ipv4, &ifindex, 0) < 0) return -1;
    flags = vfs_irq_save();
    if (fd < 0 || fd >= VFS_MAX_FDS ||
        !g_fds[fd].used ||
        g_fds[fd].backend != VFS_BACKEND_UDP4) {
        vfs_irq_restore(flags);
        return -1;
    }
    if (!g_fds[fd].udp_bound && udp_bind_locked(fd, 0) < 0) {
        vfs_irq_restore(flags);
        return -1;
    }
    g_fds[fd].udp_peer_set = 1;
    g_fds[fd].udp_peer_ipv4 = dst_ipv4;
    g_fds[fd].udp_peer_port = dst_port;
    g_fds[fd].udp_ifindex = ifindex;
    vfs_irq_restore(flags);
    return 0;
}

int vfs_socket_getname_udp4(int fd, uint32_t *local_ipv4, uint16_t *local_port) {
    uint32_t flags;
    uint32_t ifindex;
    uint16_t port;
    const netif_t *iface;
    if (!local_ipv4 || !local_port) return -1;
    flags = vfs_irq_save();
    if (fd < 0 || fd >= VFS_MAX_FDS ||
        !g_fds[fd].used ||
        g_fds[fd].backend != VFS_BACKEND_UDP4 ||
        !g_fds[fd].udp_bound) {
        vfs_irq_restore(flags);
        return -1;
    }
    ifindex = g_fds[fd].udp_ifindex;
    port = g_fds[fd].udp_port;
    vfs_irq_restore(flags);
    iface = netif_at(ifindex);
    if (!iface) return -1;
    *local_ipv4 = iface->ipv4;
    *local_port = port;
    return 0;
}

int vfs_socket_getpeer_udp4(int fd, uint32_t *dst_ipv4, uint16_t *dst_port) {
    uint32_t flags;
    if (!dst_ipv4 || !dst_port) return -1;
    flags = vfs_irq_save();
    if (fd < 0 || fd >= VFS_MAX_FDS ||
        !g_fds[fd].used ||
        g_fds[fd].backend != VFS_BACKEND_UDP4 ||
        !g_fds[fd].udp_peer_set) {
        vfs_irq_restore(flags);
        return -1;
    }
    *dst_ipv4 = g_fds[fd].udp_peer_ipv4;
    *dst_port = g_fds[fd].udp_peer_port;
    vfs_irq_restore(flags);
    return 0;
}

int vfs_socket_sendto_udp4(int fd,
                           uint32_t dst_ipv4,
                           uint16_t dst_port,
                           const void *payload,
                           uint32_t len) {
    uint32_t flags;
    uint16_t src_port;
    uint32_t ifindex = 0;
    if (dst_ipv4 == 0 || dst_port == 0 || len > NET_UDP_PAYLOAD_MAX || (len > 0 && !payload))
        return -1;
    flags = vfs_irq_save();
    if (fd < 0 || fd >= VFS_MAX_FDS ||
        !g_fds[fd].used ||
        g_fds[fd].backend != VFS_BACKEND_UDP4) {
        vfs_irq_restore(flags);
        return -1;
    }
    if (!g_fds[fd].udp_bound && udp_bind_locked(fd, 0) < 0) {
        vfs_irq_restore(flags);
        return -1;
    }
    src_port = g_fds[fd].udp_port;
    vfs_irq_restore(flags);
    if (net_route_lookup4(dst_ipv4, &ifindex, 0) < 0) return -1;
    return net_udp_send4(ifindex, dst_ipv4, src_port, dst_port, payload, len);
}

int vfs_socket_recvfrom_udp4(int fd,
                             uint32_t *src_ipv4,
                             uint16_t *src_port,
                             void *payload,
                             uint32_t max) {
    uint32_t flags;
    uint16_t port;
    int nonblock;
    if (max > NET_UDP_PAYLOAD_MAX || (max > 0 && !payload)) return -1;
    flags = vfs_irq_save();
    if (fd < 0 || fd >= VFS_MAX_FDS ||
        !g_fds[fd].used ||
        g_fds[fd].backend != VFS_BACKEND_UDP4 ||
        !g_fds[fd].udp_bound) {
        vfs_irq_restore(flags);
        return -1;
    }
    port = g_fds[fd].udp_port;
    nonblock = (g_fds[fd].flags & VFS_O_NONBLOCK) != 0;
    vfs_irq_restore(flags);
    if (nonblock && net_udp_pending4(port) == 0) return -1;
    return net_udp_recv4(port, payload, max, src_ipv4, src_port);
}

static int vfs_socket_write_udp4(int fd, const void *payload, uint32_t len) {
    uint32_t flags;
    uint32_t dst_ipv4;
    uint16_t dst_port;
    if (len > NET_UDP_PAYLOAD_MAX || (len > 0 && !payload)) return -1;
    flags = vfs_irq_save();
    if (fd < 0 || fd >= VFS_MAX_FDS ||
        !g_fds[fd].used ||
        g_fds[fd].backend != VFS_BACKEND_UDP4 ||
        !g_fds[fd].udp_peer_set) {
        vfs_irq_restore(flags);
        return -1;
    }
    dst_ipv4 = g_fds[fd].udp_peer_ipv4;
    dst_port = g_fds[fd].udp_peer_port;
    vfs_irq_restore(flags);
    return vfs_socket_sendto_udp4(fd, dst_ipv4, dst_port, payload, len);
}

int vfs_dup(int fd) {
    uint32_t flags = vfs_irq_save();
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) {
        vfs_irq_restore(flags);
        return -1;
    }
    if (g_fds[fd].refcount == 0 || g_fds[fd].refcount >= 0xFFFFU) {
        vfs_irq_restore(flags);
        return -1;
    }
    g_fds[fd].refcount++;
    vfs_irq_restore(flags);
    return fd;
}

int vfs_fd_flags(int fd) {
    int result;
    uint32_t irq_flags = vfs_irq_save();
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) {
        vfs_irq_restore(irq_flags);
        return -1;
    }
    if (g_fds[fd].backend == 1) {
        int simple_fd = g_fds[fd].simple_fd;
        vfs_irq_restore(irq_flags);
        return simplefs_fd_flags(simple_fd);
    }
    result = g_fds[fd].flags & (VFS_O_RDWR | VFS_O_APPEND | VFS_O_NONBLOCK);
    vfs_irq_restore(irq_flags);
    return result;
}

int vfs_set_fd_flags(int fd, int flags) {
    int access;
    uint32_t irq_flags;
    if (flags & ~(VFS_O_RDWR | VFS_O_APPEND | VFS_O_NONBLOCK)) return -1;
    irq_flags = vfs_irq_save();
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) {
        vfs_irq_restore(irq_flags);
        return -1;
    }
    access = g_fds[fd].flags & VFS_O_RDWR;
    if ((flags & VFS_O_RDWR) != access) {
        vfs_irq_restore(irq_flags);
        return -1;
    }
    if (g_fds[fd].backend == 1) {
        int simple_fd = g_fds[fd].simple_fd;
        vfs_irq_restore(irq_flags);
        if (simplefs_set_fd_flags(simple_fd, flags) < 0) return -1;
        irq_flags = vfs_irq_save();
        if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) {
            vfs_irq_restore(irq_flags);
            return -1;
        }
    }
    g_fds[fd].flags = (g_fds[fd].flags & ~(VFS_O_APPEND | VFS_O_NONBLOCK)) |
                       (flags & (VFS_O_APPEND | VFS_O_NONBLOCK));
    vfs_irq_restore(irq_flags);
    return 0;
}

int vfs_close(int fd) {
    uint32_t flags;
    int backend;
    int simple_fd;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    flags = vfs_irq_save();
    if (!g_fds[fd].used) {
        vfs_irq_restore(flags);
        return -1;
    }
    if (g_fds[fd].refcount > 1) {
        g_fds[fd].refcount--;
        vfs_irq_restore(flags);
        return 0;
    }
    backend = g_fds[fd].backend;
    simple_fd = g_fds[fd].simple_fd;
    if (backend == 2) pipe_close_fd_locked(&g_fds[fd]);
    kclear_fd(&g_fds[fd]);
    vfs_irq_restore(flags);
    if (backend == 1) simplefs_close(simple_fd);
    return 0;
}

int vfs_fsync(int fd) {
    vfs_fd_t *f;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if (f->backend == 2) return -1;
    if (f->backend == 1) return simplefs_fsync(f->simple_fd);
    if (f->backend == 3) return is_proc_path(f->path) ? -1 : 0;
    if (f->backend == VFS_BACKEND_UDP4) return -1;
    if (!f->node || is_proc_path(f->node->path)) return -1;
    return 0;
}

int vfs_fdatasync(int fd) {
    return vfs_fsync(fd);
}

int vfs_sync(void) {
    return simplefs_sync();
}

int vfs_read(int fd, void *buf, uint32_t len) {
    vfs_fd_t *f;
    uint32_t n, i;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if ((f->flags & VFS_O_RDONLY) == 0) return -1;
    if (len == 0) return 0;
    if (!buf) return -1;
    if (f->backend == 2) return pipe_read_stream(f, buf, len);
    if (f->backend == 1) return simplefs_read(f->simple_fd, buf, len);
    if (f->backend == 3) return -1;
    if (f->backend == VFS_BACKEND_UDP4) return vfs_socket_recvfrom_udp4(fd, 0, 0, buf, len);
    sync_proc_node(f->node);
    if (f->node->type == VFS_NODE_DEV) {
        int net = device_net_index(f->node->path);
        if (device_is_tty(f->node->path)) {
            if ((f->flags & VFS_O_NONBLOCK) && !tty_read_ready()) return -1;
            return tty_read((char *)buf, len);
        }
        if (device_is_kmsg(f->node->path)) return read_kmsg_stream(&f->pos, buf, len);
        if (device_is_null(f->node->path)) return 0;
        if (device_is_zero(f->node->path) ||
            device_is_full(f->node->path)) return read_zero_stream(buf, len);
        if (device_is_urandom(f->node->path)) return read_urandom_stream(buf, len);
        if (net >= 0) return net_recv((uint32_t)net, buf, len);
        return read_block_stream(f->node, &f->pos, buf, len);
    }
    if (f->pos >= f->node->size) return 0;
    n = f->node->size - f->pos;
    if (n > len) n = len;
    for (i = 0; i < n; i++) ((char *)buf)[i] = f->node->data[f->pos + i];
    f->pos += n;
    if (n > 0) f->node->accessed_ms = timer_ms();
    return (int)n;
}

int vfs_write(int fd, const void *buf, uint32_t len) {
    vfs_fd_t *f;
    uint32_t n, i;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if ((f->flags & VFS_O_WRONLY) == 0) return -1;
    if (len == 0) return 0;
    if (!buf) return -1;
    if (f->backend == 2) return pipe_write_stream(f, buf, len);
    if (f->backend == 1) return simplefs_write(f->simple_fd, buf, len);
    if (f->backend == 3) return -1;
    if (f->backend == VFS_BACKEND_UDP4) return vfs_socket_write_udp4(fd, buf, len);
    if (is_proc_path(f->node->path)) return -1;
    if (f->node->type == VFS_NODE_DEV) {
        int net = device_net_index(f->node->path);
        if (device_is_tty(f->node->path)) {
            tty_write_buf((const char *)buf, len);
            return (int)len;
        }
        if (device_is_kmsg(f->node->path)) return write_kmsg_stream(buf, len);
        if (device_is_full(f->node->path)) return -1;
        if (device_is_null(f->node->path) ||
            device_is_zero(f->node->path)) return (int)len;
        if (device_is_urandom(f->node->path)) return write_urandom_stream(buf, len);
        if (net >= 0) return net_send((uint32_t)net, buf, len);
        return write_block_stream(f->node, &f->pos, buf, len);
    }
    if (f->flags & VFS_O_APPEND) f->pos = f->node->size;
    if (f->pos >= RAMFS_FILE_CAP) return 0;
    n = RAMFS_FILE_CAP - f->pos;
    if (n > len) n = len;
    for (i = 0; i < n; i++) f->node->data[f->pos + i] = ((const char *)buf)[i];
    f->pos += n;
    if (f->pos > f->node->size) f->node->size = f->pos;
    if (f->node->size < RAMFS_FILE_CAP) f->node->data[f->node->size] = 0;
    if (n > 0) {
        f->node->modified_ms = timer_ms();
        f->node->accessed_ms = f->node->modified_ms;
    }
    return (int)n;
}

int vfs_pread(int fd, void *buf, uint32_t len, uint32_t offset) {
    vfs_fd_t *f;
    uint32_t n;
    uint32_t i;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if ((f->flags & VFS_O_RDONLY) == 0) return -1;
    if (len == 0) return 0;
    if (!buf) return -1;
    if (f->backend == 2) return -1;
    if (f->backend == 3) return -1;
    if (f->backend == VFS_BACKEND_UDP4) return -1;
    if (f->backend == 1) {
        int old_pos;
        int ret;
        if (offset > 0x7FFFFFFFU) return -1;
        old_pos = simplefs_seek(f->simple_fd, 0, VFS_SEEK_CUR);
        if (old_pos < 0 || simplefs_seek(f->simple_fd, (int32_t)offset, VFS_SEEK_SET) < 0)
            return -1;
        ret = simplefs_read(f->simple_fd, buf, len);
        if (simplefs_seek(f->simple_fd, old_pos, VFS_SEEK_SET) < 0) return -1;
        return ret;
    }
    if (!f->node) return -1;
    sync_proc_node(f->node);
    if (f->node->type == VFS_NODE_DEV) {
        uint32_t pos = offset;
        if (device_block_index(f->node->path) < 0) return -1;
        return read_block_stream(f->node, &pos, buf, len);
    }
    if (f->node->type != VFS_NODE_FILE) return -1;
    if (offset >= f->node->size) return 0;
    n = f->node->size - offset;
    if (n > len) n = len;
    for (i = 0; i < n; i++) ((char *)buf)[i] = f->node->data[offset + i];
    if (n > 0) f->node->accessed_ms = timer_ms();
    return (int)n;
}

int vfs_pwrite(int fd, const void *buf, uint32_t len, uint32_t offset) {
    vfs_fd_t *f;
    uint32_t n;
    uint32_t i;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if ((f->flags & VFS_O_WRONLY) == 0) return -1;
    if (len == 0) return 0;
    if (!buf) return -1;
    if (f->backend == 2) return -1;
    if (f->backend == 3) return -1;
    if (f->backend == VFS_BACKEND_UDP4) return -1;
    if (f->backend == 1) {
        int old_pos;
        int ret;
        if (offset > 0x7FFFFFFFU) return -1;
        old_pos = simplefs_seek(f->simple_fd, 0, VFS_SEEK_CUR);
        if (old_pos < 0 || simplefs_seek(f->simple_fd, (int32_t)offset, VFS_SEEK_SET) < 0)
            return -1;
        ret = simplefs_write(f->simple_fd, buf, len);
        if (simplefs_seek(f->simple_fd, old_pos, VFS_SEEK_SET) < 0) return -1;
        return ret;
    }
    if (!f->node) return -1;
    if (is_proc_path(f->node->path)) return -1;
    if (f->node->type == VFS_NODE_DEV) {
        uint32_t pos = offset;
        if (device_block_index(f->node->path) < 0) return -1;
        return write_block_stream(f->node, &pos, buf, len);
    }
    if (f->node->type != VFS_NODE_FILE) return -1;
    if (offset >= RAMFS_FILE_CAP) return 0;
    n = RAMFS_FILE_CAP - offset;
    if (n > len) n = len;
    if (offset > f->node->size) {
        for (i = f->node->size; i < offset; i++) f->node->data[i] = 0;
    }
    for (i = 0; i < n; i++) f->node->data[offset + i] = ((const char *)buf)[i];
    if (offset + n > f->node->size) f->node->size = offset + n;
    if (f->node->size < RAMFS_FILE_CAP) f->node->data[f->node->size] = 0;
    if (n > 0) {
        f->node->modified_ms = timer_ms();
        f->node->accessed_ms = f->node->modified_ms;
    }
    return (int)n;
}

int vfs_seek(int fd, int32_t off, int whence) {
    vfs_fd_t *f;
    int32_t base = 0;
    int64_t np;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if (f->backend == 2) return -1;
    if (f->backend == 3) return -1;
    if (f->backend == VFS_BACKEND_UDP4) return -1;
    if (f->backend == 1) return simplefs_seek(f->simple_fd, off, whence);
    if (whence == VFS_SEEK_SET) base = 0;
    else if (whence == VFS_SEEK_CUR) base = (int32_t)f->pos;
    else if (whence == VFS_SEEK_END) base = (int32_t)f->node->size;
    else return -1;
    np = (int64_t)base + (int64_t)off;
    if (np < 0 || np > 0x7FFFFFFFLL) return -1;
    f->pos = (uint32_t)np;
    return (int)f->pos;
}


int vfs_fstat(int fd, vfs_dirent_t *out) {
    vfs_fd_t *f;
    const char *p;
    if (!out || fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if (f->backend == 1) return simplefs_stat(f->path, out);
    if (f->backend == 3) return vfs_stat(f->path, out);
    if (f->backend == VFS_BACKEND_UDP4) {
        fill_dirent(out,
                    "udp4",
                    VFS_NODE_DEV,
                    f->udp_bound ? (uint32_t)net_udp_pending4(f->udp_port) : 0,
                    VFS_MODE_IFDEV | 0000600U,
                    0,
                    0,
                    0);
        return 0;
    }
    if (f->backend == 2) {
        uint32_t flags;
        fill_dirent(out,
                    f->pipe_write ? "pipew" : "piper",
                    VFS_NODE_DEV,
                    0,
                    VFS_MODE_IFDEV | 0000600U,
                    0,
                    0,
                    0);
        flags = vfs_irq_save();
        if (f->pipe_id >= 0 && f->pipe_id < VFS_MAX_PIPES && g_pipes[f->pipe_id].used)
            out->size = g_pipes[f->pipe_id].len;
        vfs_irq_restore(flags);
        return 0;
    }
    if (!f->node) return -1;
    sync_proc_node(f->node);
    p = f->node->path;
    while (*p) p++;
    while (p > f->node->path && p[-1] != '/') p--;
    if (!*p) p = "/";
    fill_dirent(out,
                p,
                f->node->type,
                f->node->size,
                f->node->mode,
                f->node->created_ms,
                f->node->modified_ms,
                f->node->accessed_ms);
    out->uid = f->node->uid;
    out->gid = f->node->gid;
    return 0;
}

int vfs_ftruncate(int fd, uint32_t size) {
    vfs_fd_t *f;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if ((f->flags & VFS_O_WRONLY) == 0) return -1;
    if (f->backend == 1) return simplefs_ftruncate(f->simple_fd, size);
    if (f->backend == 2 || f->backend == 3 || f->backend == VFS_BACKEND_UDP4) return -1;
    return ramfs_resize_node(f->node, size);
}

int vfs_fchmod(int fd, uint32_t mode) {
    vfs_fd_t *f;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    if (mode & ~VFS_MODE_PERM_MASK) return -1;
    f = &g_fds[fd];
    if (f->backend == 1) return simplefs_chmod(f->path, mode);
    if (f->backend == 3) return vfs_chmod(f->path, mode);
    if (f->backend == 2 ||
        f->backend == VFS_BACKEND_UDP4 ||
        !f->node ||
        is_proc_path(f->node->path) ||
        is_dev_path(f->node->path))
        return -1;
    if (!current_can_change_owner(f->node->uid)) return -1;
    f->node->mode = sanitized_mode(f->node->type, mode);
    f->node->modified_ms = timer_ms();
    return 0;
}

int vfs_fchown(int fd, uint32_t uid, uint32_t gid) {
    vfs_fd_t *f;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    if (!current_can_chown()) return -1;
    f = &g_fds[fd];
    if (f->backend == 1) return simplefs_chown(f->path, uid, gid);
    if (f->backend == 3) return vfs_chown(f->path, uid, gid);
    if (f->backend == 2 || f->backend == VFS_BACKEND_UDP4 || !f->node ||
        is_proc_path(f->node->path) || is_dev_path(f->node->path))
        return -1;
    f->node->uid = uid;
    f->node->gid = gid;
    f->node->modified_ms = timer_ms();
    return 0;
}

int vfs_futime(int fd, uint32_t accessed_ms, uint32_t modified_ms) {
    vfs_fd_t *f;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if (f->backend == 1) return simplefs_utime(f->path, accessed_ms, modified_ms);
    if (f->backend == 3) return vfs_utime(f->path, accessed_ms, modified_ms);
    if (f->backend == 2 ||
        f->backend == VFS_BACKEND_UDP4 ||
        !f->node ||
        is_proc_path(f->node->path) ||
        is_dev_path(f->node->path))
        return -1;
    f->node->accessed_ms = accessed_ms;
    f->node->modified_ms = modified_ms;
    return 0;
}

int vfs_poll(int fd, uint32_t events) {
    vfs_fd_t *f;
    uint32_t ready = 0;
    if (events & ~(VFS_POLL_READ | VFS_POLL_WRITE)) return -1;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    if (events == 0) return 0;
    f = &g_fds[fd];
    if (f->backend == 1) return simplefs_poll(f->simple_fd, events);
    if (f->backend == 3) return -1;
    if (f->backend == VFS_BACKEND_UDP4) {
        if ((events & VFS_POLL_READ) &&
            f->udp_bound &&
            net_udp_pending4(f->udp_port) > 0)
            ready |= VFS_POLL_READ;
        if ((events & VFS_POLL_WRITE) && (f->flags & VFS_O_WRONLY))
            ready |= VFS_POLL_WRITE;
        return (int)ready;
    }
    if (f->backend == 2) {
        uint32_t flags = vfs_irq_save();
        if (f->pipe_id < 0 || f->pipe_id >= VFS_MAX_PIPES || !g_pipes[f->pipe_id].used) {
            vfs_irq_restore(flags);
            return -1;
        }
        if ((events & VFS_POLL_READ) &&
            !f->pipe_write &&
            (g_pipes[f->pipe_id].len > 0 || g_pipes[f->pipe_id].writers == 0))
            ready |= VFS_POLL_READ;
        if ((events & VFS_POLL_WRITE) &&
            f->pipe_write &&
            g_pipes[f->pipe_id].readers > 0 &&
            g_pipes[f->pipe_id].len < VFS_PIPE_CAP)
            ready |= VFS_POLL_WRITE;
        vfs_irq_restore(flags);
        return (int)ready;
    }
    if (!f->node) return -1;
    if (f->node->type == VFS_NODE_DEV && device_is_tty(f->node->path)) {
        if ((events & VFS_POLL_READ) && (f->flags & VFS_O_RDONLY) && tty_read_ready())
            ready |= VFS_POLL_READ;
        if ((events & VFS_POLL_WRITE) && (f->flags & VFS_O_WRONLY))
            ready |= VFS_POLL_WRITE;
        return (int)ready;
    }
    if ((events & VFS_POLL_READ) && (f->flags & VFS_O_RDONLY)) ready |= VFS_POLL_READ;
    if ((events & VFS_POLL_WRITE) &&
        (f->flags & VFS_O_WRONLY) &&
        !is_proc_path(f->node->path)) ready |= VFS_POLL_WRITE;
    return (int)ready;
}

int vfs_ioctl(int fd, uint32_t request, uint32_t arg) {
    vfs_fd_t *f;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if (f->backend != 0 || !f->node || f->node->type != VFS_NODE_DEV) return -1;
    if (!device_is_tty(f->node->path)) return -1;
    if (request == VFS_IOCTL_TTY_GET_MODE) return (int)tty_get_mode();
    if (request == VFS_IOCTL_TTY_SET_MODE) return tty_set_mode(arg);
    if (request == VFS_IOCTL_TTY_GET_SIZE) return (int)((TTY_ROWS << 16) | TTY_COLUMNS);
    return -1;
}

int vfs_readdir(const char *path, uint32_t index, vfs_dirent_t *out) {
    ram_node_t *dir;
    char resolved[VFS_MAX_PATH];
    int followed;
    uint32_t i, seen = 0;
    uint32_t proc_pid;
    const char *proc_leaf;
    if (!path || !out) return -1;
    followed = resolve_final_symlink_path(path, resolved, sizeof(resolved));
    if (followed < 0) return -1;
    if (followed > 0) path = resolved;
    dir = find_node(path);
    if (is_simplefs_path(path)) {
        return simplefs_readdir(path, index, out);
    }
    if (parse_proc_process_path_raw(path, &proc_pid, &proc_leaf)) {
        if (!proc_pid_path_valid(path) || proc_leaf[0]) return -1;
        return fill_proc_pid_dirent(path, index, out);
    }
    if (!dir || dir->type != VFS_NODE_DIR) return -1;
    for (i = 0; i < RAMFS_MAX_NODES; i++) {
        const char *name = 0;
        if (g_nodes[i].used && path_is_child(path, g_nodes[i].path, &name)) {
            uint32_t node_pid;
            const char *node_leaf;
            if (parse_proc_process_path_raw(g_nodes[i].path, &node_pid, &node_leaf) &&
                !proc_pid_path_valid(g_nodes[i].path)) continue;
            if (seen == index) {
                fill_dirent(out,
                            name,
                            g_nodes[i].type,
                            g_nodes[i].size,
                            g_nodes[i].mode,
                            g_nodes[i].created_ms,
                            g_nodes[i].modified_ms,
                            g_nodes[i].accessed_ms);
                out->uid = g_nodes[i].uid;
                out->gid = g_nodes[i].gid;
                return 0;
            }
            seen++;
        }
    }
    if (kstrcmp(path, "/proc") == 0) return fill_dynamic_proc_root_dirent(index - seen, out);
    if (kstrcmp(path, "/dev") == 0) return fill_dynamic_dev_dirent(index - seen, out);
    return -1;
}

int vfs_readdir_fd(int fd, uint32_t index, vfs_dirent_t *out) {
    vfs_fd_t *f;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used || !out) return -1;
    f = &g_fds[fd];
    if (f->backend == 3) return vfs_readdir(f->path, index, out);
    if (f->backend == 0 && f->node && f->node->type == VFS_NODE_DIR)
        return vfs_readdir(f->node->path, index, out);
    return -1;
}

int vfs_fd_path(int fd, char *out, uint32_t max) {
    vfs_fd_t *f;
    if (fd < 0 || fd >= VFS_MAX_FDS || !g_fds[fd].used || !out || max == 0) return -1;
    f = &g_fds[fd];
    if (f->backend == 2 || f->backend == VFS_BACKEND_UDP4 || !f->path[0]) return -1;
    kcopy(out, f->path, max);
    return out[0] ? 0 : -1;
}

int vfs_stat(const char *path, vfs_dirent_t *out) {
    ram_node_t *node;
    char resolved[VFS_MAX_PATH];
    int followed;
    const char *p;
    uint32_t proc_pid;
    const char *proc_leaf;
    if (!path || !out) return -1;
    followed = resolve_final_symlink_path(path, resolved, sizeof(resolved));
    if (followed < 0) return -1;
    if (followed > 0) path = resolved;
    if (parse_proc_process_path_raw(path, &proc_pid, &proc_leaf) && !proc_pid_path_valid(path)) return -1;
    node = find_node(path);
    if (is_simplefs_path(path) && !(node && node->type == VFS_NODE_DIR)) {
        return simplefs_stat(path, out);
    }
    if (!node && is_dev_path(path)) node = materialize_dev_node(path);
    if (!node && is_proc_path(path)) node = materialize_proc_node(path);
    if (!node) return -1;
    sync_proc_node(node);
    p = node->path;
    while (*p) p++;
    while (p > node->path && p[-1] != '/') p--;
    if (!*p) p = "/";
    fill_dirent(out,
                p,
                node->type,
                node->size,
                node->mode,
                node->created_ms,
                node->modified_ms,
                node->accessed_ms);
    out->uid = node->uid;
    out->gid = node->gid;
    if (node->type == VFS_NODE_DEV) {
        int block_index = device_block_index(node->path);
        int net_index = device_net_index(node->path);
        if (block_index >= 0) {
            const block_device_t *block_dev = block_at((uint32_t)block_index);
            if (block_dev) out->size = saturating_mul_u32(block_dev->sector_count,
                                                          block_dev->sector_size);
        } else if (net_index >= 0) {
            const netif_t *net_dev = netif_at((uint32_t)net_index);
            if (net_dev) out->size = net_dev->mtu;
        } else if (device_is_kmsg(node->path)) {
            out->size = debug_log_size();
        }
    }
    return 0;
}

int vfs_lstat(const char *path, vfs_dirent_t *out) {
    ram_node_t *node;
    const char *p;
    uint32_t proc_pid;
    const char *proc_leaf;
    if (!path || !out) return -1;
    if (parse_proc_process_path_raw(path, &proc_pid, &proc_leaf) && !proc_pid_path_valid(path)) return -1;
    node = find_node(path);
    if (is_simplefs_path(path) && !(node && node->type == VFS_NODE_DIR)) {
        return simplefs_stat(path, out);
    }
    if (!node && is_dev_path(path)) node = materialize_dev_node(path);
    if (!node && is_proc_path(path)) node = materialize_proc_node(path);
    if (!node) return -1;
    sync_proc_node(node);
    p = node->path;
    while (*p) p++;
    while (p > node->path && p[-1] != '/') p--;
    if (!*p) p = "/";
    fill_dirent(out,
                p,
                node->type,
                node->size,
                node->mode,
                node->created_ms,
                node->modified_ms,
                node->accessed_ms);
    out->uid = node->uid;
    out->gid = node->gid;
    if (node->type == VFS_NODE_DEV) {
        int block_index = device_block_index(node->path);
        int net_index = device_net_index(node->path);
        if (block_index >= 0) {
            const block_device_t *block_dev = block_at((uint32_t)block_index);
            if (block_dev) out->size = saturating_mul_u32(block_dev->sector_count,
                                                          block_dev->sector_size);
        } else if (net_index >= 0) {
            const netif_t *net_dev = netif_at((uint32_t)net_index);
            if (net_dev) out->size = net_dev->mtu;
        } else if (device_is_kmsg(node->path)) {
            out->size = debug_log_size();
        }
    }
    return 0;
}

int vfs_statfs(const char *path, vfs_fsinfo_t *out) {
    ram_node_t *node;
    char resolved[VFS_MAX_PATH];
    int followed;
    if (!path || !out) return -1;
    followed = resolve_final_symlink_path(path, resolved, sizeof(resolved));
    if (followed < 0) return -1;
    if (followed > 0) path = resolved;
    if (is_simplefs_path(path)) return simplefs_statfs(path, out);
    if (is_proc_path(path)) {
        fill_static_fsinfo(out, "procfs");
        return 0;
    }
    if (is_dev_path(path)) {
        fill_static_fsinfo(out, "devfs");
        return 0;
    }
    node = find_node(path);
    if (!node) return -1;
    fill_ramfs_fsinfo(out);
    return 0;
}

int vfs_access(const char *path, uint32_t mask) {
    ram_node_t *node;
    char resolved[VFS_MAX_PATH];
    int followed;
    uint32_t proc_pid;
    const char *proc_leaf;
    if (!path) return -1;
    if (mask & ~(VFS_ACCESS_READ | VFS_ACCESS_WRITE | VFS_ACCESS_EXEC)) return -1;
    followed = resolve_final_symlink_path(path, resolved, sizeof(resolved));
    if (followed < 0) return -1;
    if (followed > 0) path = resolved;
    if (parse_proc_process_path_raw(path, &proc_pid, &proc_leaf) && !proc_pid_path_valid(path)) return -1;
    node = find_node(path);
    if (is_simplefs_path(path) && !(node && node->type == VFS_NODE_DIR))
        return simplefs_access(path, mask);
    if (!node && is_dev_path(path)) node = materialize_dev_node(path);
    if (!node && is_proc_path(path)) node = materialize_proc_node(path);
    if (!node) return -1;
    return mode_allows_user(node->mode, node->uid, node->gid, mask) ? 0 : -1;
}

int vfs_chmod(const char *path, uint32_t mode) {
    ram_node_t *node;
    char resolved[VFS_MAX_PATH];
    int followed;
    uint32_t proc_pid;
    const char *proc_leaf;
    if (!path || (mode & ~VFS_MODE_PERM_MASK)) return -1;
    followed = resolve_final_symlink_path(path, resolved, sizeof(resolved));
    if (followed < 0) return -1;
    if (followed > 0) path = resolved;
    if (parse_proc_process_path_raw(path, &proc_pid, &proc_leaf) && !proc_pid_path_valid(path)) return -1;
    node = find_node(path);
    if (is_simplefs_path(path) && !(node && node->type == VFS_NODE_DIR))
        return simplefs_chmod(path, mode);
    if (!node && is_dev_path(path)) node = materialize_dev_node(path);
    if (!node && is_proc_path(path)) node = materialize_proc_node(path);
    if (!node || is_proc_path(path)) return -1;
    if (!current_can_change_owner(node->uid)) return -1;
    node->mode = sanitized_mode(node->type, mode);
    node->modified_ms = timer_ms();
    return 0;
}

int vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
    ram_node_t *node;
    char resolved[VFS_MAX_PATH];
    int followed;
    uint32_t proc_pid;
    const char *proc_leaf;
    if (!path || !current_can_chown()) return -1;
    followed = resolve_final_symlink_path(path, resolved, sizeof(resolved));
    if (followed < 0) return -1;
    if (followed > 0) path = resolved;
    if (parse_proc_process_path_raw(path, &proc_pid, &proc_leaf) && !proc_pid_path_valid(path)) return -1;
    node = find_node(path);
    if (is_simplefs_path(path) && !(node && node->type == VFS_NODE_DIR))
        return simplefs_chown(path, uid, gid);
    if (!node && is_dev_path(path)) node = materialize_dev_node(path);
    if (!node && is_proc_path(path)) node = materialize_proc_node(path);
    if (!node || is_proc_path(path)) return -1;
    node->uid = uid;
    node->gid = gid;
    node->modified_ms = timer_ms();
    return 0;
}

int vfs_utime(const char *path, uint32_t accessed_ms, uint32_t modified_ms) {
    ram_node_t *node;
    char resolved[VFS_MAX_PATH];
    int followed;
    uint32_t proc_pid;
    const char *proc_leaf;
    if (!path) return -1;
    followed = resolve_final_symlink_path(path, resolved, sizeof(resolved));
    if (followed < 0) return -1;
    if (followed > 0) path = resolved;
    if (parse_proc_process_path_raw(path, &proc_pid, &proc_leaf) && !proc_pid_path_valid(path)) return -1;
    node = find_node(path);
    if (is_simplefs_path(path) && !(node && node->type == VFS_NODE_DIR))
        return simplefs_utime(path, accessed_ms, modified_ms);
    if (!node && is_dev_path(path)) node = materialize_dev_node(path);
    if (!node && is_proc_path(path)) node = materialize_proc_node(path);
    if (!node || is_proc_path(path) || is_dev_path(path)) return -1;
    node->accessed_ms = accessed_ms;
    node->modified_ms = modified_ms;
    return 0;
}

int vfs_truncate(const char *path, uint32_t size) {
    ram_node_t *node;
    char resolved[VFS_MAX_PATH];
    int followed;
    if (!path) return -1;
    followed = resolve_final_symlink_path(path, resolved, sizeof(resolved));
    if (followed < 0) return -1;
    if (followed > 0) path = resolved;
    node = find_node(path);
    if (is_simplefs_path(path) && !(node && node->type == VFS_NODE_DIR)) {
        return simplefs_truncate(path, size);
    }
    if (!node) return -1;
    if (!mode_allows_user(node->mode, node->uid, node->gid, VFS_ACCESS_WRITE)) return -1;
    return ramfs_resize_node(node, size);
}
