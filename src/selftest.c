#include "selftest.h"
#include "block.h"
#include "debug.h"
#include "driver.h"
#include "elf.h"
#include "gdt.h"
#include "heap.h"
#include "idt.h"
#include "net.h"
#include "paging.h"
#include "pci.h"
#include "pmm.h"
#include "process.h"
#include "shell.h"
#include "simplefs.h"
#include "syscall.h"
#include "task.h"
#include "timer.h"
#include "tty.h"
#include "uts.h"
#include "usermode.h"
#include "vfs.h"
#include <stdint.h>

static volatile uint32_t g_selftest_task_runs;
static volatile uint32_t g_selftest_preempt_runs;
static volatile uint32_t g_selftest_return_runs;
static char g_shell_capture[512];
static uint32_t g_shell_capture_pos;

enum {
    SELFTEST_SFS_SECTORS = 128,
    SELFTEST_SFS_DIR_START = 1,
    SELFTEST_SFS_DIR_SECTORS = 3,
    SELFTEST_SFS_MAX_OPEN = 8
};

typedef struct {
    uint8_t data[SELFTEST_SFS_SECTORS * BLOCK_SECTOR_SIZE];
    int fail_dir_writes;
} selftest_fault_disk_t;

static selftest_fault_disk_t g_sfs_fault_disk;
static selftest_fault_disk_t g_syscall_disk;

extern uint32_t syscall_handler_c(uint32_t num,
                                  uint32_t a,
                                  uint32_t b,
                                  uint32_t c,
                                  uint32_t d);
extern uint32_t syscall_handler_c_frame(uint32_t num,
                                        uint32_t a,
                                        uint32_t b,
                                        uint32_t c,
                                        uint32_t d,
                                        void *frame);

typedef struct {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t original_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp;
    uint32_t ss;
} selftest_syscall_frame_t;

static uint32_t slen(const char *s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
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

static int memeq(const char *a, const char *b, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static int streq(const char *a, const char *b) {
    while (a && b && *a && *b && *a == *b) {
        a++;
        b++;
    }
    return a && b && *a == 0 && *b == 0;
}

static int contains_text(const char *haystack, const char *needle) {
    uint32_t i;
    if (!haystack || !needle) return 0;
    if (!needle[0]) return 1;
    for (i = 0; haystack[i]; i++) {
        uint32_t j = 0;
        while (haystack[i + j] && needle[j] && haystack[i + j] == needle[j]) j++;
        if (needle[j] == 0) return 1;
    }
    return 0;
}

static int dir_has_entry(const char *path, const char *name, vfs_node_type_t type) {
    vfs_dirent_t ent;
    uint32_t i;
    for (i = 0; i < 128; i++) {
        if (vfs_readdir(path, i, &ent) < 0) return 0;
        if (ent.type == type && streq(ent.name, name)) return 1;
    }
    return 0;
}

static int make_selftest_net_path(uint32_t index, char *out, uint32_t max) {
    uint32_t pos = 0;
    if (!out || max < 10) return -1;
    append_str(out, &pos, max, "/dev/net");
    append_dec(out, &pos, max, index);
    return out[pos] == 0 ? 0 : -1;
}

static int make_selftest_net_name(uint32_t index, char *out, uint32_t max) {
    uint32_t pos = 0;
    if (!out || max < 5) return -1;
    append_str(out, &pos, max, "net");
    append_dec(out, &pos, max, index);
    return out[pos] == 0 ? 0 : -1;
}

static int find_netif_by_name(const char *name) {
    uint32_t i;
    for (i = 0; i < netif_count(); i++) {
        const netif_t *net = netif_at(i);
        if (net && streq(net->name, name)) return (int)i;
    }
    return -1;
}

static int read_process_u8(const process_t *proc, uint32_t virt, uint8_t *out) {
    uint32_t phys;
    if (!proc || !proc->page_dir || !out) return 0;
    phys = paging_get_phys_in_directory((uint32_t *)proc->page_dir, virt);
    if (!phys) return 0;
    *out = *(uint8_t *)phys;
    return 1;
}

static int read_process_u32(const process_t *proc, uint32_t virt, uint32_t *out) {
    uint32_t i;
    uint32_t value = 0;
    uint8_t byte;
    if (!out) return 0;
    for (i = 0; i < 4; i++) {
        if (!read_process_u8(proc, virt + i, &byte)) return 0;
        value |= ((uint32_t)byte) << (i * 8U);
    }
    *out = value;
    return 1;
}

static int user_string_eq(const process_t *proc, uint32_t virt, const char *expected) {
    uint32_t i;
    for (i = 0; i < PROCESS_ARG_MAX; i++) {
        uint8_t byte;
        if (!read_process_u8(proc, virt + i, &byte)) return 0;
        if (byte != (uint8_t)expected[i]) return 0;
        if (byte == 0) return 1;
    }
    return 0;
}

static int user_env_contains(const process_t *proc, const char *expected) {
    uint32_t envp = 0;
    uint32_t i;
    if (!proc || !read_process_u32(proc, proc->user_stack + 8U, &envp)) return 0;
    for (i = 0; i <= PROCESS_MAX_ENV; i++) {
        uint32_t ptr = 0;
        if (!read_process_u32(proc, envp + i * 4U, &ptr)) return 0;
        if (ptr == 0) return 0;
        if (user_string_eq(proc, ptr, expected)) return 1;
    }
    return 0;
}

static void strcopy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    if (!max) return;
    while (src && src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void shell_capture_reset(void) {
    g_shell_capture_pos = 0;
    g_shell_capture[0] = 0;
}

static void shell_capture_line(const char *line) {
    append_str(g_shell_capture, &g_shell_capture_pos, sizeof(g_shell_capture), line);
    append_char(g_shell_capture, &g_shell_capture_pos, sizeof(g_shell_capture), '\n');
}

static void store16(uint8_t *buf, uint32_t off, uint16_t v) {
    buf[off] = (uint8_t)(v & 0xFF);
    buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static void store32(uint8_t *buf, uint32_t off, uint32_t v) {
    buf[off] = (uint8_t)(v & 0xFF);
    buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    buf[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    buf[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint16_t selftest_checksum16(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint16_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len) sum += (uint16_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static int elf_segment_should_not_run(void *ctx,
                                      uint32_t vaddr,
                                      uint32_t memsz,
                                      uint32_t flags,
                                      const uint8_t *data,
                                      uint32_t filesz) {
    (void)ctx;
    (void)vaddr;
    (void)memsz;
    (void)flags;
    (void)data;
    (void)filesz;
    return 0;
}

static void emit_result(selftest_write_fn out, const char *name, int ok) {
    char line[96];
    uint32_t pos = 0;
    append_str(line, &pos, sizeof(line), ok ? "PASS " : "FAIL ");
    append_str(line, &pos, sizeof(line), name);
    out(line);
}

static int write_read_file(const char *path, const char *payload) {
    char buf[64];
    uint32_t len = slen(payload);
    int fd;
    int n;
    (void)vfs_unlink(path);
    fd = vfs_open(path, VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    n = vfs_write(fd, payload, len);
    vfs_close(fd);
    if (n != (int)len) {
        (void)vfs_unlink(path);
        return 0;
    }
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        (void)vfs_unlink(path);
        return 0;
    }
    n = vfs_read(fd, buf, sizeof(buf));
    vfs_close(fd);
    (void)vfs_unlink(path);
    return n == (int)len && memeq(buf, payload, len);
}

static int file_text_contains(const char *path, const char *needle) {
    char buf[384];
    int fd;
    int n;
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return 0;
    n = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    return contains_text(buf, needle);
}

static int selftest_block_read(void *ctx, uint32_t lba, void *buf, uint32_t sectors) {
    uint32_t i;
    (void)lba;
    (void)ctx;
    if (!buf && sectors > 0) return -1;
    for (i = 0; i < sectors * BLOCK_SECTOR_SIZE; i++) ((uint8_t *)buf)[i] = 0;
    return (int)sectors;
}

static int selftest_block_write(void *ctx, uint32_t lba, const void *buf, uint32_t sectors) {
    (void)ctx;
    (void)lba;
    (void)buf;
    return (int)sectors;
}

static void selftest_memcopy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}

static void selftest_memzero(uint8_t *dst, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = 0;
}

static int selftest_lba_range_overlaps(uint32_t lba,
                                       uint32_t sectors,
                                       uint32_t target_lba,
                                       uint32_t target_sectors) {
    uint32_t end = lba + sectors;
    uint32_t target_end = target_lba + target_sectors;
    return lba < target_end && target_lba < end;
}

static int selftest_fault_read(void *ctx, uint32_t lba, void *buf, uint32_t sectors) {
    selftest_fault_disk_t *disk = (selftest_fault_disk_t *)ctx;
    selftest_memcopy((uint8_t *)buf,
                     disk->data + lba * BLOCK_SECTOR_SIZE,
                     sectors * BLOCK_SECTOR_SIZE);
    return (int)sectors;
}

static int selftest_fault_write(void *ctx, uint32_t lba, const void *buf, uint32_t sectors) {
    selftest_fault_disk_t *disk = (selftest_fault_disk_t *)ctx;
    if (disk->fail_dir_writes &&
        selftest_lba_range_overlaps(lba,
                                    sectors,
                                    SELFTEST_SFS_DIR_START,
                                    SELFTEST_SFS_DIR_SECTORS)) return -1;
    selftest_memcopy(disk->data + lba * BLOCK_SECTOR_SIZE,
                     (const uint8_t *)buf,
                     sectors * BLOCK_SECTOR_SIZE);
    return (int)sectors;
}

static int test_open_flags_for_path(const char *path) {
    char buf[16];
    vfs_dirent_t ent;
    int held[VFS_MAX_FDS];
    int fd;
    int n;
    uint32_t i;
    (void)vfs_unlink(path);
    for (i = 0; i < VFS_MAX_FDS; i++) held[i] = -1;
    if (vfs_open("/missing/reserved.txt", VFS_O_CREAT | VFS_O_RDWR) >= 0) return 0;
    for (i = 0; i < VFS_MAX_FDS; i++) {
        held[i] = vfs_open("/etc/hostname", VFS_O_RDONLY);
        if (held[i] < 0) {
            for (i = 0; i < VFS_MAX_FDS; i++) if (held[i] >= 0) vfs_close(held[i]);
            return 0;
        }
    }
    fd = vfs_open("/etc/hostname", VFS_O_RDONLY);
    if (fd >= 0) {
        vfs_close(fd);
        for (i = 0; i < VFS_MAX_FDS; i++) if (held[i] >= 0) vfs_close(held[i]);
        return 0;
    }
    for (i = 0; i < VFS_MAX_FDS; i++) {
        vfs_close(held[i]);
        held[i] = -1;
    }
    fd = vfs_open(path, VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_read(fd, 0, 0) != 0 ||
        vfs_write(fd, 0, 0) != 0 ||
        vfs_read(fd, 0, 1) >= 0 ||
        vfs_write(fd, 0, 1) >= 0) {
        vfs_close(fd);
        (void)vfs_unlink(path);
        return 0;
    }
    if (vfs_write(fd, "one", 3) != 3) {
        vfs_close(fd);
        (void)vfs_unlink(path);
        return 0;
    }
    vfs_close(fd);
    fd = vfs_open(path, VFS_O_RDONLY | VFS_O_APPEND);
    if (fd >= 0) {
        vfs_close(fd);
        (void)vfs_unlink(path);
        return 0;
    }
    fd = vfs_open(path, VFS_O_WRONLY | VFS_O_APPEND);
    if (fd < 0) {
        (void)vfs_unlink(path);
        return 0;
    }
    if (vfs_seek(fd, 0, VFS_SEEK_SET) < 0) {
        vfs_close(fd);
        (void)vfs_unlink(path);
        return 0;
    }
    if (vfs_write(fd, "two", 3) != 3) {
        vfs_close(fd);
        (void)vfs_unlink(path);
        return 0;
    }
    vfs_close(fd);
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        (void)vfs_unlink(path);
        return 0;
    }
    n = vfs_read(fd, buf, sizeof(buf));
    if (vfs_seek(fd, (int32_t)0x7FFFFFF0, VFS_SEEK_SET) < 0 ||
        vfs_seek(fd, 0x100, VFS_SEEK_CUR) >= 0) {
        vfs_close(fd);
        (void)vfs_unlink(path);
        return 0;
    }
    vfs_close(fd);
    if (n != 6 || !memeq(buf, "onetwo", 6)) {
        (void)vfs_unlink(path);
        return 0;
    }
    fd = vfs_open(path, VFS_O_RDWR);
    if (fd < 0) {
        (void)vfs_unlink(path);
        return 0;
    }
    if (vfs_ftruncate(fd, 8) < 0 ||
        vfs_seek(fd, 0, VFS_SEEK_SET) < 0 ||
        vfs_read(fd, buf, 8) != 8 ||
        !memeq(buf, "onetwo", 6) ||
        buf[6] != 0 ||
        buf[7] != 0 ||
        vfs_ftruncate(fd, 3) < 0) {
        vfs_close(fd);
        (void)vfs_unlink(path);
        return 0;
    }
    vfs_close(fd);
    if (vfs_stat(path, &ent) < 0 || ent.size != 3) {
        (void)vfs_unlink(path);
        return 0;
    }
    if (vfs_truncate(path, 6) < 0) {
        (void)vfs_unlink(path);
        return 0;
    }
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        (void)vfs_unlink(path);
        return 0;
    }
    n = vfs_read(fd, buf, 6);
    vfs_close(fd);
    if (n != 6 ||
        !memeq(buf, "one", 3) ||
        buf[3] != 0 ||
        buf[4] != 0 ||
        buf[5] != 0) {
        (void)vfs_unlink(path);
        return 0;
    }
    for (i = 0; i < VFS_MAX_FDS; i++) {
        held[i] = vfs_open("/etc/hostname", VFS_O_RDONLY);
        if (held[i] < 0) break;
    }
    fd = vfs_open(path, VFS_O_WRONLY | VFS_O_TRUNC);
    if (fd >= 0) {
        vfs_close(fd);
        for (i = 0; i < VFS_MAX_FDS; i++) if (held[i] >= 0) vfs_close(held[i]);
        (void)vfs_unlink(path);
        return 0;
    }
    for (i = 0; i < VFS_MAX_FDS; i++) if (held[i] >= 0) vfs_close(held[i]);
    if (vfs_stat(path, &ent) < 0 || ent.size != 6) {
        (void)vfs_unlink(path);
        return 0;
    }
    fd = vfs_open(path, VFS_O_WRONLY | VFS_O_TRUNC);
    if (fd < 0) {
        (void)vfs_unlink(path);
        return 0;
    }
    vfs_close(fd);
    n = vfs_stat(path, &ent);
    (void)vfs_unlink(path);
    return n == 0 && ent.size == 0;
}

static int test_vfs_ramfs(void) {
    vfs_dirent_t ent;
    vfs_fsinfo_t fs;
    int fd;
    int ro_fd;
    int wo_fd;
    int bad_fd;
    int n;
    char buf[8];
    const char *long_path = "/tmp/abcdefghijklmnopqrstuvwxyz0123456789";
    if (vfs_open(0, VFS_O_RDONLY) >= 0 ||
        vfs_stat(0, &ent) >= 0 ||
        vfs_readdir(0, 0, &ent) >= 0 ||
        vfs_unlink(0) >= 0) return 0;
    if (vfs_stat("/etc/hostname", &ent) < 0 || ent.type != VFS_NODE_FILE || ent.size == 0) return 0;
    if (vfs_statfs("/", &fs) < 0 ||
        !streq(fs.name, "ramfs") ||
        fs.total_files == 0 ||
        fs.free_files >= fs.total_files) return 0;
    if (vfs_statfs("/proc", &fs) < 0 || !streq(fs.name, "procfs")) return 0;
    if (vfs_statfs("/dev", &fs) < 0 || !streq(fs.name, "devfs")) return 0;
    if (vfs_stat("/bin/hello", &ent) < 0 ||
        (ent.mode & VFS_MODE_EXEC_MASK) == 0 ||
        vfs_access("/bin/hello", VFS_ACCESS_EXEC) < 0 ||
        vfs_chmod("/proc/meminfo", 0000600U) == 0) return 0;
    bad_fd = vfs_open("/etc/hostname", VFS_O_RDONLY | 0x4000);
    if (bad_fd >= 0) {
        vfs_close(bad_fd);
        return 0;
    }
    if (vfs_create("/tmp/") == 0 ||
        vfs_create("/tmp//bad") == 0 ||
        vfs_create("/tmp/.") == 0 ||
        vfs_create("/tmp/..") == 0 ||
        vfs_create(long_path) == 0 ||
        vfs_create("/dev/not-a-device") == 0) return 0;
    fd = vfs_open("/etc/hostname", VFS_O_RDONLY);
    if (fd < 0) return 0;
    if (vfs_read(fd, buf, sizeof(buf)) <= 0) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    ro_fd = vfs_open("/tmp/selftest_ro.txt", VFS_O_CREAT | VFS_O_RDONLY);
    if (ro_fd < 0) return 0;
    if (vfs_read(ro_fd, 0, 0) != 0 || vfs_write(ro_fd, 0, 0) >= 0) {
        vfs_close(ro_fd);
        (void)vfs_unlink("/tmp/selftest_ro.txt");
        return 0;
    }
    if (vfs_write(ro_fd, "x", 1) >= 0) {
        vfs_close(ro_fd);
        (void)vfs_unlink("/tmp/selftest_ro.txt");
        return 0;
    }
    vfs_close(ro_fd);
    (void)vfs_unlink("/tmp/selftest_ro.txt");
    wo_fd = vfs_open("/etc/hostname", VFS_O_WRONLY);
    if (wo_fd < 0) return 0;
    if (vfs_read(wo_fd, buf, sizeof(buf)) >= 0) {
        vfs_close(wo_fd);
        return 0;
    }
    vfs_close(wo_fd);
    if (!write_read_file("/tmp/selftest.txt", "ramfs ok\n")) return 0;
    if (vfs_chmod("/tmp/selftest.txt", 0000400U) < 0 ||
        vfs_stat("/tmp/selftest.txt", &ent) < 0 ||
        (ent.mode & VFS_MODE_PERM_MASK) != 0000400U ||
        vfs_access("/tmp/selftest.txt", VFS_ACCESS_READ) < 0 ||
        vfs_access("/tmp/selftest.txt", VFS_ACCESS_WRITE) == 0) return 0;
    fd = vfs_open("/tmp/selftest.txt", VFS_O_WRONLY);
    if (fd >= 0) {
        vfs_close(fd);
        return 0;
    }
    fd = vfs_open("/tmp/selftest.txt", VFS_O_RDONLY);
    if (fd < 0) return 0;
    vfs_close(fd);
    if (vfs_chmod("/tmp/selftest.txt", 0000600U) < 0) return 0;
    (void)vfs_unlink("/tmp/selftest-open-unlink.txt");
    fd = vfs_open("/tmp/selftest-open-unlink.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_write(fd, "live", 4) != 4 ||
        vfs_unlink("/tmp/selftest-open-unlink.txt") == 0 ||
        vfs_seek(fd, 0, VFS_SEEK_SET) != 0 ||
        vfs_read(fd, buf, 4) != 4 ||
        !memeq(buf, "live", 4)) {
        vfs_close(fd);
        (void)vfs_unlink("/tmp/selftest-open-unlink.txt");
        return 0;
    }
    vfs_close(fd);
    if (vfs_unlink("/tmp/selftest-open-unlink.txt") < 0) return 0;
    if (!test_open_flags_for_path("/tmp/selftest-flags.txt")) return 0;
    (void)vfs_unlink("/tmp/selftest-rename.txt");
    (void)vfs_unlink("/tmp/selftest-renamed.txt");
    fd = vfs_open("/tmp/selftest-rename.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    n = vfs_write(fd, "r", 1);
    vfs_close(fd);
    if (n != 1) return 0;
    if (vfs_rename("/tmp/selftest-rename.txt", "/tmp/selftest-renamed.txt") < 0) return 0;
    if (vfs_stat("/tmp/selftest-rename.txt", &ent) == 0) return 0;
    if (vfs_stat("/tmp/selftest-renamed.txt", &ent) < 0 || ent.size != 1) return 0;
    if (vfs_rename("/tmp/selftest-renamed.txt", "/dev/selftest-renamed.txt") == 0) return 0;
    if (vfs_rename("/tmp/selftest-renamed.txt", "/missing/selftest-renamed.txt") == 0) return 0;
    (void)vfs_unlink("/tmp/selftest-renamed.txt");
    (void)vfs_unlink("/tmp/selftest-dir/file.txt");
    (void)vfs_unlink("/tmp/selftest-dir/sub");
    (void)vfs_unlink("/tmp/selftest-dir");
    (void)vfs_unlink("/tmp/selftest-renamed-dir/file.txt");
    (void)vfs_unlink("/tmp/selftest-renamed-dir/sub");
    (void)vfs_unlink("/tmp/selftest-renamed-dir");
    if (vfs_mkdir("/tmp/selftest-dir") < 0) return 0;
    if (vfs_mkdir("/tmp/selftest-dir/sub") < 0) return 0;
    fd = vfs_open("/tmp/selftest-dir/file.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    n = vfs_write(fd, "d", 1);
    vfs_close(fd);
    if (n != 1) return 0;
    if (vfs_rename("/tmp/selftest-dir", "/tmp/selftest-dir/sub/moved") == 0) return 0;
    if (vfs_rename("/tmp/selftest-dir", "/tmp/selftest-renamed-dir") < 0) return 0;
    if (vfs_stat("/tmp/selftest-dir/file.txt", &ent) == 0) return 0;
    if (vfs_stat("/tmp/selftest-renamed-dir/file.txt", &ent) < 0 || ent.size != 1) return 0;
    if (vfs_stat("/tmp/selftest-renamed-dir/sub", &ent) < 0 || ent.type != VFS_NODE_DIR) return 0;
    if (vfs_unlink("/tmp/selftest-renamed-dir") == 0) return 0;
    if (vfs_unlink("/tmp/selftest-renamed-dir/file.txt") < 0) return 0;
    if (vfs_unlink("/tmp/selftest-renamed-dir/sub") < 0) return 0;
    if (vfs_unlink("/tmp/selftest-renamed-dir") < 0) return 0;
    return 1;
}

static int test_vfs_umask(void) {
    process_t *proc = process_current();
    uint32_t old_mask = process_umask_get(proc);
    vfs_dirent_t ent;
    int fd;
    int ok = 0;
    (void)vfs_unlink("/tmp/umask-file.txt");
    (void)vfs_unlink("/tmp/umask-open.txt");
    (void)vfs_unlink("/tmp/umask-dir");
    if (process_umask_set(proc, 0000077U) < 0) return 0;
    ok = vfs_create("/tmp/umask-file.txt") == 0 &&
         vfs_stat("/tmp/umask-file.txt", &ent) == 0 &&
         (ent.mode & VFS_MODE_PERM_MASK) == 0000600U &&
         vfs_mkdir("/tmp/umask-dir") == 0 &&
         vfs_stat("/tmp/umask-dir", &ent) == 0 &&
         (ent.mode & VFS_MODE_PERM_MASK) == 0000700U;
    if (process_umask_set(proc, 0000002U) < 0) ok = 0;
    fd = vfs_open("/tmp/umask-open.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd >= 0) vfs_close(fd);
    ok = ok &&
         fd >= 0 &&
         vfs_stat("/tmp/umask-open.txt", &ent) == 0 &&
         (ent.mode & VFS_MODE_PERM_MASK) == 0000664U;
    if (simplefs_mounted()) {
        (void)vfs_unlink("/disk/umask-file.txt");
        (void)vfs_unlink("/disk/umask-dir");
        ok = ok &&
             vfs_create("/disk/umask-file.txt") == 0 &&
             vfs_stat("/disk/umask-file.txt", &ent) == 0 &&
             (ent.mode & VFS_MODE_PERM_MASK) == 0000664U &&
             vfs_mkdir("/disk/umask-dir") == 0 &&
             vfs_stat("/disk/umask-dir", &ent) == 0 &&
             (ent.mode & VFS_MODE_PERM_MASK) == 0000775U;
        (void)vfs_unlink("/disk/umask-file.txt");
        (void)vfs_unlink("/disk/umask-dir");
    }
    (void)vfs_unlink("/tmp/umask-file.txt");
    (void)vfs_unlink("/tmp/umask-open.txt");
    (void)vfs_unlink("/tmp/umask-dir");
    if (process_umask_set(proc, old_mask) < 0) ok = 0;
    return ok && process_umask_get(proc) == old_mask;
}

static int test_vfs_rmdir(void) {
    int fd;
    int ok;
    (void)vfs_unlink("/tmp/rmdir-dir/file.txt");
    (void)vfs_unlink("/tmp/rmdir-dir");
    (void)vfs_unlink("/tmp/rmdir-file.txt");
    fd = vfs_open("/tmp/rmdir-file.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    vfs_close(fd);
    ok = vfs_rmdir("/tmp/rmdir-file.txt") < 0 &&
         vfs_mkdir("/tmp/rmdir-dir") == 0;
    fd = vfs_open("/tmp/rmdir-dir/file.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd >= 0) vfs_close(fd);
    ok = ok &&
         fd >= 0 &&
         vfs_rmdir("/tmp/rmdir-dir") < 0 &&
         vfs_unlink("/tmp/rmdir-dir/file.txt") == 0 &&
         vfs_rmdir("/tmp/rmdir-dir") == 0 &&
         vfs_stat("/tmp/rmdir-dir", &(vfs_dirent_t){0}) < 0;
    (void)vfs_unlink("/tmp/rmdir-file.txt");
    if (simplefs_mounted()) {
        (void)vfs_unlink("/disk/rmdir-dir/file.txt");
        (void)vfs_unlink("/disk/rmdir-dir");
        ok = ok &&
             vfs_mkdir("/disk/rmdir-dir") == 0;
        fd = vfs_open("/disk/rmdir-dir/file.txt", VFS_O_CREAT | VFS_O_RDWR);
        if (fd >= 0) vfs_close(fd);
        ok = ok &&
             fd >= 0 &&
             vfs_rmdir("/disk/rmdir-dir") < 0 &&
             vfs_unlink("/disk/rmdir-dir/file.txt") == 0 &&
             vfs_rmdir("/disk/rmdir-dir") == 0;
    }
    return ok;
}

static int test_vfs_fchmod(void) {
    vfs_dirent_t ent;
    int fd;
    int ok;
    (void)vfs_unlink("/tmp/fchmod.txt");
    fd = vfs_open("/tmp/fchmod.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    ok = vfs_fchmod(fd, 0000400U) == 0 &&
         vfs_fstat(fd, &ent) == 0 &&
         (ent.mode & VFS_MODE_PERM_MASK) == 0000400U &&
         vfs_fchmod(fd, 0001000U) < 0;
    vfs_close(fd);
    fd = vfs_open("/tmp/fchmod.txt", VFS_O_WRONLY);
    if (fd >= 0) {
        vfs_close(fd);
        ok = 0;
    }
    (void)vfs_unlink("/tmp/fchmod.txt");
    if (simplefs_mounted()) {
        (void)vfs_unlink("/disk/fchmod.txt");
        fd = vfs_open("/disk/fchmod.txt", VFS_O_CREAT | VFS_O_RDWR);
        if (fd < 0) return 0;
        ok = ok &&
             vfs_fchmod(fd, 0000400U) == 0 &&
             vfs_fstat(fd, &ent) == 0 &&
             (ent.mode & VFS_MODE_PERM_MASK) == 0000400U;
        vfs_close(fd);
        (void)vfs_unlink("/disk/fchmod.txt");
    }
    return ok;
}

static int test_vfs_ownership(void) {
    process_t *cur = process_current();
    uint32_t old_uid = process_uid_get(cur);
    uint32_t old_gid = process_gid_get(cur);
    vfs_dirent_t ent;
    int fd;
    int ok = 0;
    if (!cur) return 0;
    (void)process_uid_set(cur, 0);
    (void)process_gid_set(cur, 0);
    (void)vfs_unlink("/tmp/own.txt");
    fd = vfs_open("/tmp/own.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) goto done;
    if (vfs_fchown(fd, 42, 7) < 0 ||
        vfs_fstat(fd, &ent) < 0 ||
        ent.uid != 42 ||
        ent.gid != 7 ||
        vfs_fchmod(fd, 0000640U) < 0) {
        vfs_close(fd);
        goto done;
    }
    vfs_close(fd);
    if (process_uid_set(cur, 43) < 0 || process_gid_set(cur, 7) < 0) goto done;
    if (vfs_access("/tmp/own.txt", VFS_ACCESS_READ) < 0 ||
        vfs_access("/tmp/own.txt", VFS_ACCESS_WRITE) == 0 ||
        vfs_chmod("/tmp/own.txt", 0000600U) == 0 ||
        vfs_chown("/tmp/own.txt", 0, 0) == 0) goto done;
    if (process_uid_set(cur, 42) < 0) goto done;
    if (vfs_access("/tmp/own.txt", VFS_ACCESS_READ | VFS_ACCESS_WRITE) < 0 ||
        vfs_chmod("/tmp/own.txt", 0000600U) < 0 ||
        vfs_chown("/tmp/own.txt", 0, 0) == 0) goto done;
    if (process_uid_set(cur, 0) < 0 || process_gid_set(cur, 0) < 0) goto done;
    if (vfs_chown("/tmp/own.txt", 0, 0) < 0 ||
        vfs_stat("/tmp/own.txt", &ent) < 0 ||
        ent.uid != 0 ||
        ent.gid != 0) goto done;
    ok = 1;
    if (simplefs_mounted()) {
        (void)vfs_unlink("/disk/own.txt");
        fd = vfs_open("/disk/own.txt", VFS_O_CREAT | VFS_O_RDWR);
        if (fd < 0) {
            ok = 0;
        } else {
            ok = vfs_fchown(fd, 42, 7) == 0 &&
                 vfs_fstat(fd, &ent) == 0 &&
                 ent.uid == 42 &&
                 ent.gid == 7 &&
                 vfs_fchmod(fd, 0000640U) == 0;
            vfs_close(fd);
            if (process_uid_set(cur, 43) < 0 || process_gid_set(cur, 7) < 0) ok = 0;
            if (ok && (vfs_access("/disk/own.txt", VFS_ACCESS_READ) < 0 ||
                       vfs_access("/disk/own.txt", VFS_ACCESS_WRITE) == 0))
                ok = 0;
            (void)process_uid_set(cur, 0);
            (void)process_gid_set(cur, 0);
            (void)vfs_unlink("/disk/own.txt");
        }
    }
done:
    (void)process_uid_set(cur, 0);
    (void)process_gid_set(cur, 0);
    (void)vfs_unlink("/tmp/own.txt");
    (void)process_uid_set(cur, old_uid);
    (void)process_gid_set(cur, old_gid);
    return ok;
}

static int test_vfs_symlink(void) {
    vfs_dirent_t ent;
    vfs_fsinfo_t fs;
    char target[VFS_MAX_PATH];
    char buf[16];
    int fd;
    int n;
    int ok;
    (void)vfs_unlink("/tmp/symlink-link.txt");
    (void)vfs_unlink("/tmp/symlink-rel-link.txt");
    (void)vfs_unlink("/tmp/symlink-dangling.txt");
    (void)vfs_unlink("/tmp/symlink-loop-a");
    (void)vfs_unlink("/tmp/symlink-loop-b");
    (void)vfs_unlink("/tmp/symlink-dir-link");
    (void)vfs_unlink("/tmp/symlink-dir/file.txt");
    (void)vfs_unlink("/tmp/symlink-dir");
    (void)vfs_unlink("/tmp/symlink-target.txt");
    fd = vfs_open("/tmp/symlink-target.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_write(fd, "link-ok", 7) != 7) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    fd = vfs_open("/tmp/symlink-target.txt", VFS_O_WRONLY);
    if (fd < 0) return 0;
    vfs_close(fd);
    ok = vfs_symlink("/tmp/symlink-target.txt", "/tmp/symlink-link.txt") == 0 &&
         vfs_readlink("/tmp/symlink-link.txt", target, sizeof(target)) == 23 &&
         streq(target, "/tmp/symlink-target.txt") &&
         dir_has_entry("/tmp", "symlink-link.txt", VFS_NODE_SYMLINK) &&
         vfs_lstat("/tmp/symlink-link.txt", &ent) == 0 &&
         ent.type == VFS_NODE_SYMLINK &&
         ent.size == 23 &&
         vfs_stat("/tmp/symlink-link.txt", &ent) == 0 &&
         ent.type == VFS_NODE_FILE &&
         ent.size == 7;
    fd = vfs_open("/tmp/symlink-link.txt", VFS_O_RDONLY);
    n = fd >= 0 ? vfs_read(fd, buf, sizeof(buf)) : -1;
    if (fd >= 0) vfs_close(fd);
    ok = ok &&
         n == 7 &&
         memeq(buf, "link-ok", 7) &&
         vfs_symlink("symlink-target.txt", "/tmp/symlink-rel-link.txt") == 0;
    fd = vfs_open("/tmp/symlink-rel-link.txt", VFS_O_RDONLY);
    n = fd >= 0 ? vfs_read(fd, buf, sizeof(buf)) : -1;
    if (fd >= 0) vfs_close(fd);
    ok = ok &&
         n == 7 &&
         memeq(buf, "link-ok", 7) &&
         vfs_chmod("/tmp/symlink-link.txt", 0000400U) == 0 &&
         vfs_access("/tmp/symlink-link.txt", VFS_ACCESS_READ) == 0 &&
         vfs_access("/tmp/symlink-link.txt", VFS_ACCESS_WRITE) < 0 &&
         vfs_chmod("/tmp/symlink-target.txt", 0000600U) == 0 &&
         vfs_truncate("/tmp/symlink-link.txt", 4) == 0 &&
         vfs_stat("/tmp/symlink-target.txt", &ent) == 0 &&
         ent.size == 4;
    ok = ok &&
         vfs_symlink("/tmp/no-such-target.txt", "/tmp/symlink-dangling.txt") == 0 &&
         vfs_readlink("/tmp/symlink-dangling.txt", target, sizeof(target)) == 23 &&
         vfs_stat("/tmp/symlink-dangling.txt", &ent) < 0 &&
         vfs_open("/tmp/symlink-dangling.txt", VFS_O_RDONLY) < 0 &&
         vfs_symlink("/tmp/symlink-loop-b", "/tmp/symlink-loop-a") == 0 &&
         vfs_symlink("/tmp/symlink-loop-a", "/tmp/symlink-loop-b") == 0 &&
         vfs_stat("/tmp/symlink-loop-a", &ent) < 0 &&
         vfs_mkdir("/tmp/symlink-dir") == 0;
    fd = vfs_open("/tmp/symlink-dir/file.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd >= 0) vfs_close(fd);
    ok = ok &&
         fd >= 0 &&
         vfs_symlink("/tmp/symlink-dir", "/tmp/symlink-dir-link") == 0 &&
         vfs_readdir("/tmp/symlink-dir-link", 0, &ent) == 0 &&
         streq(ent.name, "file.txt") &&
         vfs_statfs("/tmp/symlink-dir-link", &fs) == 0 &&
         streq(fs.name, "ramfs") &&
         vfs_unlink("/tmp/symlink-link.txt") == 0 &&
         vfs_stat("/tmp/symlink-target.txt", &ent) == 0;
    (void)vfs_unlink("/tmp/symlink-rel-link.txt");
    (void)vfs_unlink("/tmp/symlink-dangling.txt");
    (void)vfs_unlink("/tmp/symlink-loop-a");
    (void)vfs_unlink("/tmp/symlink-loop-b");
    (void)vfs_unlink("/tmp/symlink-dir-link");
    (void)vfs_unlink("/tmp/symlink-dir/file.txt");
    (void)vfs_unlink("/tmp/symlink-dir");
    (void)vfs_unlink("/tmp/symlink-target.txt");
    return ok;
}

static int test_vfs_pread_pwrite(void) {
    vfs_dirent_t ent;
    char buf[16];
    int fd;
    int ro_fd;
    int ok;
    (void)vfs_unlink("/tmp/pread.txt");
    fd = vfs_open("/tmp/pread.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    ok = vfs_write(fd, "abcdef", 6) == 6 &&
         vfs_seek(fd, 1, VFS_SEEK_SET) == 1 &&
         vfs_pread(fd, buf, 3, 2) == 3 &&
         memeq(buf, "cde", 3) &&
         vfs_seek(fd, 0, VFS_SEEK_CUR) == 1 &&
         vfs_pwrite(fd, "XY", 2, 3) == 2 &&
         vfs_seek(fd, 0, VFS_SEEK_CUR) == 1 &&
         vfs_seek(fd, 0, VFS_SEEK_SET) == 0 &&
         vfs_read(fd, buf, 6) == 6 &&
         memeq(buf, "abcXYf", 6);
    vfs_close(fd);
    ro_fd = vfs_open("/tmp/pread.txt", VFS_O_RDONLY);
    ok = ok &&
         ro_fd >= 0 &&
         vfs_pwrite(ro_fd, "z", 1, 0) < 0;
    if (ro_fd >= 0) vfs_close(ro_fd);
    ok = ok &&
         vfs_stat("/tmp/pread.txt", &ent) == 0 &&
         ent.size == 6;
    (void)vfs_unlink("/tmp/pread.txt");
    if (simplefs_mounted()) {
        (void)vfs_unlink("/disk/pread.txt");
        fd = vfs_open("/disk/pread.txt", VFS_O_CREAT | VFS_O_RDWR);
        if (fd < 0) return 0;
        ok = ok &&
             vfs_write(fd, "simple", 6) == 6 &&
             vfs_seek(fd, 2, VFS_SEEK_SET) == 2 &&
             vfs_pread(fd, buf, 3, 1) == 3 &&
             memeq(buf, "imp", 3) &&
             vfs_seek(fd, 0, VFS_SEEK_CUR) == 2 &&
             vfs_pwrite(fd, "FS", 2, 4) == 2 &&
             vfs_seek(fd, 0, VFS_SEEK_CUR) == 2;
        vfs_close(fd);
        fd = vfs_open("/disk/pread.txt", VFS_O_RDONLY);
        if (fd >= 0) {
            ok = ok &&
                 vfs_read(fd, buf, 6) == 6 &&
                 memeq(buf, "simpFS", 6);
            vfs_close(fd);
        } else {
            ok = 0;
        }
        (void)vfs_unlink("/disk/pread.txt");
    }
    return ok;
}

static int test_vfs_utime(void) {
    vfs_dirent_t ent;
    int fd;
    int ok;
    (void)vfs_unlink("/tmp/utime.txt");
    fd = vfs_open("/tmp/utime.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    vfs_close(fd);
    ok = vfs_utime("/tmp/utime.txt", 111U, 222U) == 0 &&
         vfs_stat("/tmp/utime.txt", &ent) == 0 &&
         ent.accessed_ms == 111U &&
         ent.modified_ms == 222U;
    fd = vfs_open("/tmp/utime.txt", VFS_O_RDWR);
    ok = ok &&
         fd >= 0 &&
         vfs_futime(fd, 333U, 444U) == 0 &&
         vfs_fstat(fd, &ent) == 0 &&
         ent.accessed_ms == 333U &&
         ent.modified_ms == 444U;
    if (fd >= 0) vfs_close(fd);
    ok = ok &&
         vfs_utime("/proc/meminfo", 1U, 2U) < 0;
    (void)vfs_unlink("/tmp/utime.txt");
    if (simplefs_mounted()) {
        (void)vfs_unlink("/disk/utime.txt");
        fd = vfs_open("/disk/utime.txt", VFS_O_CREAT | VFS_O_RDWR);
        if (fd < 0) return 0;
        vfs_close(fd);
        ok = ok &&
             vfs_utime("/disk/utime.txt", 555U, 666U) == 0 &&
             vfs_stat("/disk/utime.txt", &ent) == 0 &&
             ent.modified_ms == 666U &&
             ent.accessed_ms == 666U;
        (void)vfs_unlink("/disk/utime.txt");
    }
    return ok;
}

static int test_vfs_sync(void) {
    char buf[8];
    int fd = -1;
    int sfd = -1;
    int proc_fd = -1;
    int pipe_fds[2] = { -1, -1 };
    int ok = 0;
    (void)vfs_unlink("/tmp/sync.txt");
    fd = vfs_open("/tmp/sync.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    ok = vfs_write(fd, "abc", 3) == 3 &&
         vfs_fsync(fd) == 0 &&
         vfs_fdatasync(fd) == 0 &&
         vfs_sync() == 0 &&
         vfs_fsync(99) < 0 &&
         vfs_fdatasync(99) < 0;
    proc_fd = vfs_open("/proc/meminfo", VFS_O_RDONLY);
    ok = ok && proc_fd >= 0 && vfs_fsync(proc_fd) < 0;
    if (proc_fd >= 0) vfs_close(proc_fd);
    if (vfs_pipe(pipe_fds) == 0) {
        ok = ok &&
             vfs_fsync(pipe_fds[0]) < 0 &&
             vfs_fdatasync(pipe_fds[1]) < 0;
        vfs_close(pipe_fds[0]);
        vfs_close(pipe_fds[1]);
    } else {
        ok = 0;
    }
    vfs_close(fd);
    if (simplefs_mounted()) {
        (void)vfs_unlink("/disk/sync.txt");
        sfd = vfs_open("/disk/sync.txt", VFS_O_CREAT | VFS_O_RDWR);
        if (sfd < 0) return 0;
        ok = ok &&
             vfs_write(sfd, "disk", 4) == 4 &&
             vfs_fsync(sfd) == 0 &&
             vfs_fdatasync(sfd) == 0 &&
             vfs_sync() == 0;
        vfs_close(sfd);
        sfd = vfs_open("/disk/sync.txt", VFS_O_RDONLY);
        if (sfd >= 0) {
            ok = ok &&
                 vfs_read(sfd, buf, 4) == 4 &&
                 memeq(buf, "disk", 4);
            vfs_close(sfd);
        } else {
            ok = 0;
        }
        (void)vfs_unlink("/disk/sync.txt");
    }
    (void)vfs_unlink("/tmp/sync.txt");
    return ok;
}

static int test_vfs_dirfd(void) {
    vfs_dirent_t ent;
    char path[VFS_MAX_PATH];
    int fd = -1;
    int file_fd = -1;
    int ok;
    (void)vfs_unlink("/tmp/dirfd/file.txt");
    (void)vfs_unlink("/tmp/dirfd");
    if (vfs_mkdir("/tmp/dirfd") < 0) return 0;
    file_fd = vfs_open("/tmp/dirfd/file.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (file_fd < 0) return 0;
    vfs_close(file_fd);
    fd = vfs_open("/tmp/dirfd", VFS_O_RDONLY);
    if (fd < 0) return 0;
    ok = vfs_fstat(fd, &ent) == 0 &&
         ent.type == VFS_NODE_DIR &&
         vfs_readdir_fd(fd, 0, &ent) == 0 &&
         streq(ent.name, "file.txt") &&
         vfs_fd_path(fd, path, sizeof(path)) == 0 &&
         streq(path, "/tmp/dirfd") &&
         vfs_read(fd, path, 1) < 0 &&
         vfs_seek(fd, 0, VFS_SEEK_SET) < 0 &&
         vfs_fsync(fd) == 0 &&
         vfs_fchmod(fd, 0000700U) == 0 &&
         vfs_stat("/tmp/dirfd", &ent) == 0 &&
         (ent.mode & VFS_MODE_PERM_MASK) == 0000700U &&
         vfs_open("/tmp/dirfd", VFS_O_RDWR) < 0 &&
         vfs_open("/tmp/dirfd", VFS_O_RDONLY | VFS_O_CREAT) < 0;
    vfs_close(fd);
    fd = vfs_open("/proc/self", VFS_O_RDONLY);
    ok = ok &&
         fd >= 0 &&
         vfs_readdir_fd(fd, 0, &ent) == 0 &&
         streq(ent.name, "status") &&
         vfs_fsync(fd) < 0;
    if (fd >= 0) vfs_close(fd);
    if (simplefs_mounted()) {
        (void)vfs_unlink("/disk/dirfd/file.txt");
        (void)vfs_unlink("/disk/dirfd");
        if (vfs_mkdir("/disk/dirfd") < 0) return 0;
        file_fd = vfs_open("/disk/dirfd/file.txt", VFS_O_CREAT | VFS_O_RDWR);
        if (file_fd < 0) return 0;
        vfs_close(file_fd);
        fd = vfs_open("/disk/dirfd", VFS_O_RDONLY);
        ok = ok &&
             fd >= 0 &&
             vfs_fstat(fd, &ent) == 0 &&
             ent.type == VFS_NODE_DIR &&
             vfs_readdir_fd(fd, 0, &ent) == 0 &&
             streq(ent.name, "file.txt") &&
             vfs_fd_path(fd, path, sizeof(path)) == 0 &&
             streq(path, "/disk/dirfd");
        if (fd >= 0) vfs_close(fd);
        (void)vfs_unlink("/disk/dirfd/file.txt");
        (void)vfs_unlink("/disk/dirfd");
    }
    (void)vfs_unlink("/tmp/dirfd/file.txt");
    (void)vfs_unlink("/tmp/dirfd");
    return ok;
}

static int test_tty_device(void) {
    vfs_dirent_t ent;
    int fd;
    int n;
    uint32_t out_pos = 0;
    uint32_t old_mode = tty_get_mode();
    char buf[8];
    char out_buf[8];
    if (vfs_stat("/dev/tty0", &ent) < 0 || ent.type != VFS_NODE_DEV) return 0;
    if (vfs_stat("/dev/tty", &ent) < 0 || ent.type != VFS_NODE_DEV) return 0;
    if (vfs_stat("/dev/console", &ent) < 0 || ent.type != VFS_NODE_DEV) return 0;
    fd = vfs_open("/dev/console", VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_ioctl(fd, VFS_IOCTL_TTY_GET_SIZE, 0) != (int)((TTY_ROWS << 16) | TTY_COLUMNS)) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    fd = vfs_open("/dev/tty", VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_ioctl(fd, VFS_IOCTL_TTY_GET_MODE, 0) < 0) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    if (tty_output_read(0, out_buf, 1) >= 0 ||
        tty_output_read(&out_pos, 0, 1) >= 0 ||
        tty_output_read(&out_pos, out_buf, 0) != 0) {
        return 0;
    }
    out_pos = 0;
    if (tty_output_read(&out_pos, out_buf, sizeof(out_buf)) < 0) return 0;
    fd = vfs_open("/dev/tty0", VFS_O_RDWR);
    if (fd < 0) return 0;
    if (tty_read(0, 0) != 0 || vfs_read(fd, 0, 0) != 0 || vfs_write(fd, 0, 0) != 0) {
        vfs_close(fd);
        return 0;
    }
    if (vfs_ioctl(fd, VFS_IOCTL_TTY_GET_SIZE, 0) != (int)((TTY_ROWS << 16) | TTY_COLUMNS) ||
        vfs_ioctl(fd, VFS_IOCTL_TTY_GET_MODE, 0) < 0 ||
        vfs_ioctl(fd, 0xFFFFFFFFU, 0) >= 0) {
        vfs_close(fd);
        return 0;
    }
    if (tty_set_mode(TTY_MODE_ECHO | TTY_MODE_CANON) < 0) {
        vfs_close(fd);
        return 0;
    }
    while (tty_read(buf, sizeof(buf)) > 0) {
    }
    if (vfs_set_fd_flags(fd, VFS_O_RDWR | VFS_O_NONBLOCK) < 0 ||
        vfs_read(fd, buf, 1) >= 0 ||
        vfs_set_fd_flags(fd, VFS_O_RDWR) < 0) {
        (void)tty_set_mode(old_mode);
        vfs_close(fd);
        return 0;
    }
    tty_input_char('x');
    if (vfs_poll(fd, VFS_POLL_READ) != 0 || vfs_read(fd, buf, sizeof(buf)) != 0) {
        (void)tty_set_mode(old_mode);
        vfs_close(fd);
        return 0;
    }
    tty_input_char('\n');
    if (vfs_poll(fd, VFS_POLL_READ) != VFS_POLL_READ) {
        (void)tty_set_mode(old_mode);
        vfs_close(fd);
        return 0;
    }
    n = vfs_read(fd, buf, sizeof(buf));
    if (n != 2 || !memeq(buf, "x\n", 2)) {
        (void)tty_set_mode(old_mode);
        vfs_close(fd);
        return 0;
    }
    if (tty_set_mode(0) < 0) {
        (void)tty_set_mode(old_mode);
        vfs_close(fd);
        return 0;
    }
    tty_input_char('r');
    if (vfs_poll(fd, VFS_POLL_READ) != VFS_POLL_READ) {
        (void)tty_set_mode(old_mode);
        vfs_close(fd);
        return 0;
    }
    n = vfs_read(fd, buf, sizeof(buf));
    if (n != 1 || buf[0] != 'r') {
        (void)tty_set_mode(old_mode);
        vfs_close(fd);
        return 0;
    }
    if (tty_set_mode(TTY_MODE_ECHO | TTY_MODE_CANON) < 0) {
        (void)tty_set_mode(old_mode);
        vfs_close(fd);
        return 0;
    }
    tty_input_char('a');
    tty_input_char('b');
    tty_input_char(0x08);
    tty_input_char('c');
    tty_input_char('\n');
    n = vfs_read(fd, buf, sizeof(buf));
    if (tty_set_mode(old_mode) < 0) {
        vfs_close(fd);
        return 0;
    }
    if (vfs_close(fd) != 0) return 0;
    return n == 3 && memeq(buf, "ac\n", 3);
}

static int test_basic_char_devices(void) {
    vfs_dirent_t ent;
    int fd;
    int n;
    uint32_t i;
    uint8_t zero[8];
    uint8_t rnd1[8];
    uint8_t rnd2[8];
    int differs = 0;
    if (vfs_stat("/dev/null", &ent) < 0 || ent.type != VFS_NODE_DEV) return 0;
    if (vfs_stat("/dev/zero", &ent) < 0 || ent.type != VFS_NODE_DEV) return 0;
    if (vfs_stat("/dev/full", &ent) < 0 || ent.type != VFS_NODE_DEV) return 0;
    if (vfs_stat("/dev/urandom", &ent) < 0 || ent.type != VFS_NODE_DEV) return 0;
    if (vfs_stat("/dev/random", &ent) < 0 || ent.type != VFS_NODE_DEV) return 0;
    fd = vfs_open("/dev/null", VFS_O_RDWR);
    if (fd < 0) return 0;
    n = vfs_read(fd, zero, sizeof(zero));
    if (n != 0 || vfs_write(fd, "discard", 7) != 7) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    fd = vfs_open("/dev/zero", VFS_O_RDWR);
    if (fd < 0) return 0;
    n = vfs_read(fd, zero, sizeof(zero));
    if (n != (int)sizeof(zero) || vfs_write(fd, "seed", 4) != 4) {
        vfs_close(fd);
        return 0;
    }
    for (i = 0; i < sizeof(zero); i++) {
        if (zero[i] != 0) {
            vfs_close(fd);
            return 0;
        }
    }
    vfs_close(fd);
    fd = vfs_open("/dev/full", VFS_O_RDWR);
    if (fd < 0) return 0;
    n = vfs_read(fd, zero, sizeof(zero));
    if (n != (int)sizeof(zero) || vfs_write(fd, "fail", 4) >= 0) {
        vfs_close(fd);
        return 0;
    }
    for (i = 0; i < sizeof(zero); i++) {
        if (zero[i] != 0) {
            vfs_close(fd);
            return 0;
        }
    }
    vfs_close(fd);
    fd = vfs_open("/dev/urandom", VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_read(fd, rnd1, sizeof(rnd1)) != (int)sizeof(rnd1) ||
        vfs_write(fd, "entropy", 7) != 7 ||
        vfs_read(fd, rnd2, sizeof(rnd2)) != (int)sizeof(rnd2)) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    for (i = 0; i < sizeof(rnd1); i++) {
        if (rnd1[i] != rnd2[i]) differs = 1;
    }
    fd = vfs_open("/dev/random", VFS_O_RDONLY);
    if (fd < 0) return 0;
    if (vfs_read(fd, rnd2, sizeof(rnd2)) != (int)sizeof(rnd2)) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    return differs && vfs_getrandom(rnd2, sizeof(rnd2)) == (int)sizeof(rnd2);
}

static int test_simplefs_write_metadata_rollback(uint32_t restore_dev) {
    vfs_dirent_t ent;
    int dev;
    int fd = -1;
    int ok = 0;
    selftest_memzero(g_sfs_fault_disk.data, sizeof(g_sfs_fault_disk.data));
    g_sfs_fault_disk.fail_dir_writes = 0;
    dev = block_find("sfsmeta");
    if (dev < 0) {
        dev = block_register("sfsmeta",
                             SELFTEST_SFS_SECTORS,
                             1,
                             selftest_fault_read,
                             selftest_fault_write,
                             &g_sfs_fault_disk);
    }
    if (dev < 0) goto restore;
    if (simplefs_format((uint32_t)dev) < 0 ||
        simplefs_mount((uint32_t)dev, "/fault") < 0) goto restore;
    fd = simplefs_open("/fault/meta.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) goto restore;
    if (simplefs_write(fd, "a", 1) != 1) goto restore;
    if (simplefs_seek(fd, 0, VFS_SEEK_END) < 0) goto restore;
    g_sfs_fault_disk.fail_dir_writes = 1;
    if (simplefs_write(fd, "bc", 2) >= 0) goto restore;
    g_sfs_fault_disk.fail_dir_writes = 0;
    if (simplefs_close(fd) < 0) {
        fd = -1;
        goto restore;
    }
    fd = -1;
    if (simplefs_stat("/fault/meta.txt", &ent) < 0 || ent.size != 1) goto restore;
    ok = 1;

restore:
    g_sfs_fault_disk.fail_dir_writes = 0;
    if (fd >= 0) (void)simplefs_close(fd);
    if (simplefs_mount(restore_dev, "/disk") < 0) ok = 0;
    return ok;
}

static int test_simplefs_disk(void) {
    enum { SFS_DIR_START_FOR_TEST = 1, SFS_DIR_SECTORS_FOR_TEST = 3 };
    uint8_t dir_backup[BLOCK_SECTOR_SIZE * SFS_DIR_SECTORS_FOR_TEST];
    uint8_t dir_corrupt[BLOCK_SECTOR_SIZE * SFS_DIR_SECTORS_FOR_TEST];
    int bad_fd;
    int fd;
    int dev;
    int sfs_fd;
    int sfs_held[16];
    char c;
    uint32_t i;
    uint32_t held_count;
    vfs_dirent_t ent;
    vfs_fsinfo_t fs;
    for (i = 0; i < sizeof(sfs_held) / sizeof(sfs_held[0]); i++) sfs_held[i] = -1;
    if (!simplefs_mounted()) return 0;
    if (vfs_statfs("/disk", &fs) < 0 ||
        !streq(fs.name, "simplefs") ||
        fs.block_size != BLOCK_SECTOR_SIZE ||
        fs.total_files != SIMPLEFS_MAX_FILES ||
        fs.free_files >= fs.total_files) return 0;
    dev = block_find("ram0");
    if (dev < 0) return 0;
    if (block_read((uint32_t)dev,
                   SFS_DIR_START_FOR_TEST,
                   dir_backup,
                   SFS_DIR_SECTORS_FOR_TEST) != SFS_DIR_SECTORS_FOR_TEST) return 0;
    for (i = 0; i < sizeof(dir_corrupt); i++) dir_corrupt[i] = dir_backup[i];
    dir_corrupt[0] = 1;
    dir_corrupt[1] = 0;
    dir_corrupt[2] = 0;
    dir_corrupt[3] = 0;
    for (i = 0; i < SIMPLEFS_NAME_MAX; i++) dir_corrupt[4 + i] = 'x';
    if (block_write((uint32_t)dev,
                    SFS_DIR_START_FOR_TEST,
                    dir_corrupt,
                    SFS_DIR_SECTORS_FOR_TEST) != SFS_DIR_SECTORS_FOR_TEST) return 0;
    if (simplefs_mount((uint32_t)dev, "/badmnt") == 0) {
        (void)block_write((uint32_t)dev,
                          SFS_DIR_START_FOR_TEST,
                          dir_backup,
                          SFS_DIR_SECTORS_FOR_TEST);
        (void)simplefs_mount((uint32_t)dev, "/disk");
        return 0;
    }
    if (block_write((uint32_t)dev,
                    SFS_DIR_START_FOR_TEST,
                    dir_backup,
                    SFS_DIR_SECTORS_FOR_TEST) != SFS_DIR_SECTORS_FOR_TEST) return 0;
    if (!simplefs_mounted() || !streq(simplefs_mount_path(), "/disk")) return 0;
    if (simplefs_mount((uint32_t)dev, "/disk") < 0) return 0;
    if (simplefs_mount((uint32_t)dev, "/bad//mnt") == 0 ||
        simplefs_mount((uint32_t)dev, "/bad/") == 0 ||
        simplefs_mount((uint32_t)dev, "/bad/../mnt") == 0 ||
        simplefs_mount((uint32_t)dev, "/dev/simple") == 0 ||
        simplefs_mount((uint32_t)dev, "/proc/simple") == 0 ||
        !simplefs_mounted() ||
        !streq(simplefs_mount_path(), "/disk")) return 0;
    if (!test_simplefs_write_metadata_rollback((uint32_t)dev)) return 0;
    if (vfs_create("/disk/.") == 0 || vfs_create("/disk/..") == 0) return 0;
    bad_fd = vfs_open("/disk/welcome.txt", VFS_O_RDONLY | 0x4000);
    if (bad_fd >= 0) {
        vfs_close(bad_fd);
        return 0;
    }
    fd = vfs_open("/disk/welcome.txt", VFS_O_RDONLY);
    if (fd < 0) return 0;
    if (simplefs_mount((uint32_t)dev, "/disk") == 0 ||
        simplefs_format((uint32_t)dev) == 0) {
        vfs_close(fd);
        return 0;
    }
    if (simplefs_unmount() == 0) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    if (simplefs_unmount() < 0 || simplefs_mounted()) return 0;
    if (vfs_statfs("/disk", &fs) < 0 || !streq(fs.name, "ramfs")) return 0;
    if (simplefs_mount((uint32_t)dev, "/disk") < 0) return 0;
    (void)vfs_unlink("/disk/open-unlink.txt");
    fd = vfs_open("/disk/open-unlink.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_write(fd, "x", 1) != 1) {
        vfs_close(fd);
        return 0;
    }
    if (vfs_unlink("/disk/open-unlink.txt") == 0) {
        vfs_close(fd);
        return 0;
    }
    if (vfs_seek(fd, 0, VFS_SEEK_SET) < 0 || vfs_read(fd, &c, 1) != 1 || c != 'x') {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    if (vfs_unlink("/disk/open-unlink.txt") < 0) return 0;
    if (!write_read_file("/disk/selftest.txt", "simplefs ok\n")) return 0;
    if (vfs_chmod("/disk/selftest.txt", 0000400U) < 0 ||
        vfs_stat("/disk/selftest.txt", &ent) < 0 ||
        (ent.mode & VFS_MODE_PERM_MASK) != 0000400U ||
        vfs_access("/disk/selftest.txt", VFS_ACCESS_READ) < 0 ||
        vfs_access("/disk/selftest.txt", VFS_ACCESS_WRITE) == 0) return 0;
    fd = vfs_open("/disk/selftest.txt", VFS_O_WRONLY);
    if (fd >= 0) {
        vfs_close(fd);
        return 0;
    }
    if (vfs_chmod("/disk/selftest.txt", 0000600U) < 0) return 0;
    if (!test_open_flags_for_path("/disk/selftest-flags.txt")) return 0;
    (void)vfs_unlink("/disk/sfs-full-trunc.txt");
    fd = vfs_open("/disk/sfs-full-trunc.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_write(fd, "keep", 4) != 4) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    sfs_fd = simplefs_open("/disk/sfs-full-trunc.txt", VFS_O_RDWR);
    if (sfs_fd < 0) return 0;
    if (simplefs_read(sfs_fd, 0, 0) != 0 ||
        simplefs_write(sfs_fd, 0, 0) != 0 ||
        simplefs_read(sfs_fd, 0, 1) >= 0 ||
        simplefs_write(sfs_fd, 0, 1) >= 0) {
        simplefs_close(sfs_fd);
        return 0;
    }
    simplefs_close(sfs_fd);
    if (simplefs_open("/disk/no-parent/reserved.txt", VFS_O_CREAT | VFS_O_RDWR) >= 0) return 0;
    held_count = 0;
    for (i = 0; i < sizeof(sfs_held) / sizeof(sfs_held[0]); i++) {
        sfs_held[i] = simplefs_open("/disk/welcome.txt", VFS_O_RDONLY);
        if (sfs_held[i] < 0) break;
        held_count++;
    }
    if (held_count != SELFTEST_SFS_MAX_OPEN) {
        for (i = 0; i < sizeof(sfs_held) / sizeof(sfs_held[0]); i++)
            if (sfs_held[i] >= 0) simplefs_close(sfs_held[i]);
        return 0;
    }
    sfs_fd = simplefs_open("/disk/sfs-full-trunc.txt", VFS_O_WRONLY | VFS_O_TRUNC);
    if (sfs_fd >= 0) {
        simplefs_close(sfs_fd);
        for (i = 0; i < sizeof(sfs_held) / sizeof(sfs_held[0]); i++)
            if (sfs_held[i] >= 0) simplefs_close(sfs_held[i]);
        return 0;
    }
    for (i = 0; i < sizeof(sfs_held) / sizeof(sfs_held[0]); i++)
        if (sfs_held[i] >= 0) simplefs_close(sfs_held[i]);
    if (vfs_stat("/disk/sfs-full-trunc.txt", &ent) < 0 || ent.size != 4) return 0;
    if (vfs_unlink("/disk/sfs-full-trunc.txt") < 0) return 0;
    (void)vfs_unlink("/disk/selftest-rename.txt");
    (void)vfs_unlink("/disk/selftest-renamed.txt");
    fd = vfs_open("/disk/selftest-rename.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_write(fd, "s", 1) != 1) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    if (vfs_rename("/disk/selftest-rename.txt", "/disk/selftest-renamed.txt") < 0) return 0;
    if (vfs_stat("/disk/selftest-rename.txt", &ent) == 0) return 0;
    if (vfs_stat("/disk/selftest-renamed.txt", &ent) < 0 || ent.size != 1) return 0;
    (void)vfs_unlink("/disk/selftest-renamed.txt");
    (void)vfs_unlink("/disk/selftest-dir/file.txt");
    (void)vfs_unlink("/disk/selftest-dir/sub");
    (void)vfs_unlink("/disk/selftest-dir");
    (void)vfs_unlink("/disk/selftest-renamed-dir/file.txt");
    (void)vfs_unlink("/disk/selftest-renamed-dir/sub");
    (void)vfs_unlink("/disk/selftest-renamed-dir");
    if (vfs_mkdir("/disk/selftest-dir") < 0) return 0;
    if (vfs_create("/disk/selftest-dir") == 0) return 0;
    fd = vfs_open("/disk/selftest-dir", VFS_O_RDONLY);
    if (fd < 0) return 0;
    if (vfs_fstat(fd, &ent) < 0 || ent.type != VFS_NODE_DIR) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    fd = vfs_open("/disk/selftest-dir/file.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_write(fd, "d", 1) != 1) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    if (vfs_mkdir("/disk/selftest-dir/file.txt") == 0) return 0;
    if (vfs_readdir("/disk/selftest-dir", 0, &ent) < 0 ||
        !streq(ent.name, "file.txt") ||
        ent.type != VFS_NODE_FILE ||
        ent.size != 1) return 0;
    if (vfs_mkdir("/disk/selftest-dir/sub") < 0) return 0;
    if (vfs_unlink("/disk/selftest-dir") == 0) return 0;
    if (vfs_rename("/disk/selftest-dir", "/disk/selftest-dir/sub/moved") == 0) return 0;
    if (vfs_rename("/disk/selftest-dir", "/disk/selftest-renamed-dir") < 0) return 0;
    if (vfs_stat("/disk/selftest-dir/file.txt", &ent) == 0) return 0;
    if (vfs_stat("/disk/selftest-renamed-dir/file.txt", &ent) < 0 ||
        ent.type != VFS_NODE_FILE ||
        ent.size != 1) return 0;
    if (vfs_stat("/disk/selftest-renamed-dir/sub", &ent) < 0 || ent.type != VFS_NODE_DIR) return 0;
    if (vfs_readdir("/disk/selftest-renamed-dir", 0, &ent) < 0 ||
        (!streq(ent.name, "file.txt") && !streq(ent.name, "sub"))) return 0;
    if (vfs_unlink("/disk/selftest-renamed-dir") == 0) return 0;
    if (vfs_unlink("/disk/selftest-renamed-dir/file.txt") < 0) return 0;
    if (vfs_unlink("/disk/selftest-renamed-dir/sub") < 0) return 0;
    if (vfs_unlink("/disk/selftest-renamed-dir") < 0) return 0;
    if (vfs_stat("/disk/selftest-renamed-dir", &ent) == 0) return 0;
    return 1;
}

static int test_block_ram0(void) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    int bad_fd;
    int raw_fd;
    char b;
    int dev = block_find("ram0");
    int dyn_dev;
    const block_device_t *info;
    const block_device_t *dyn_info;
    uint32_t read_ops_before;
    uint32_t write_ops_before;
    uint32_t read_sectors_before;
    uint32_t write_sectors_before;
    vfs_dirent_t ent;
    if (dev < 0) return 0;
    info = block_at((uint32_t)dev);
    if (!info || info->sector_size != BLOCK_SECTOR_SIZE || info->sector_count == 0) return 0;
    if (block_register("ram0", 1, 1, selftest_block_read, selftest_block_write, 0) >= 0) return 0;
    if (block_register("", 1, 1, selftest_block_read, selftest_block_write, 0) >= 0) return 0;
    if (block_register("bad/name", 1, 1, selftest_block_read, selftest_block_write, 0) >= 0) return 0;
    if (block_register("abcdefghijklmnopqrstuvwxyz012345", 1, 1, selftest_block_read, selftest_block_write, 0) >= 0) return 0;
    if (vfs_stat("/dev/ram0", &ent) < 0 ||
        ent.type != VFS_NODE_DEV ||
        ent.size != info->sector_count * info->sector_size) return 0;
    if (block_find("ata3") < 0 && vfs_stat("/dev/ata3", &ent) == 0) return 0;
    dyn_dev = block_find("sfsmeta");
    if (dyn_dev < 0) dyn_dev = block_find("selfblk");
    if (dyn_dev < 0 && block_count() < BLOCK_MAX_DEVICES) {
        dyn_dev = block_register("selfblk", 2, 1, selftest_block_read, selftest_block_write, 0);
    }
    if (dyn_dev < 0) return 0;
    dyn_info = block_at((uint32_t)dyn_dev);
    if (!dyn_info || !dyn_info->name) return 0;
    if (!dir_has_entry("/dev", dyn_info->name, VFS_NODE_DEV)) return 0;
    {
        char dyn_path[VFS_MAX_PATH];
        uint32_t pos = 0;
        append_str(dyn_path, &pos, sizeof(dyn_path), "/dev/");
        append_str(dyn_path, &pos, sizeof(dyn_path), dyn_info->name);
        if (vfs_stat(dyn_path, &ent) < 0 ||
            ent.type != VFS_NODE_DEV ||
            ent.size != dyn_info->sector_count * dyn_info->sector_size) return 0;
    }
    bad_fd = vfs_open("/dev/ata3", VFS_O_RDONLY);
    if (bad_fd >= 0) {
        if (block_find("ata3") < 0 && vfs_read(bad_fd, &b, 1) >= 0) {
            vfs_close(bad_fd);
            return 0;
        }
        vfs_close(bad_fd);
    }
    raw_fd = vfs_open("/dev/ram0", VFS_O_RDWR);
    if (raw_fd < 0) return 0;
    if (vfs_seek(raw_fd,
                 (int32_t)(info->sector_count * info->sector_size),
                 VFS_SEEK_SET) < 0 ||
        vfs_write(raw_fd, "x", 1) >= 0) {
        vfs_close(raw_fd);
        return 0;
    }
    vfs_close(raw_fd);
    read_ops_before = info->read_ops;
    write_ops_before = info->write_ops;
    read_sectors_before = info->read_sectors;
    write_sectors_before = info->write_sectors;
    if (block_read((uint32_t)dev, 0, sector, 1) != 1 ||
        info->read_ops != read_ops_before + 1U ||
        info->read_sectors != read_sectors_before + 1U) return 0;
    if (block_write((uint32_t)dev, 0, sector, 1) != 1 ||
        info->write_ops != write_ops_before + 1U ||
        info->write_sectors != write_sectors_before + 1U) return 0;
    return block_read((uint32_t)dev, info->sector_count, sector, 1) < 0 &&
           block_write((uint32_t)dev, info->sector_count, sector, 1) < 0 &&
           block_read((uint32_t)dev, 0, sector, 0) == 0 &&
           block_write((uint32_t)dev, 0, sector, 0) == 0 &&
           block_read((uint32_t)dev, 0, 0, 0) == 0 &&
           block_write((uint32_t)dev, 0, 0, 0) == 0 &&
           block_find("missing0") < 0;
}

static int test_elf_user_image(void) {
    uint8_t image[256];
    uint8_t bad_image[256];
    uint8_t overlap[256];
    elf_image_t meta;
    int fd;
    int n;
    uint32_t i;
    fd = vfs_open("/bin/hello", VFS_O_RDONLY);
    if (fd < 0) return 0;
    n = vfs_read(fd, image, sizeof(image));
    vfs_close(fd);
    if (n <= 0) return 0;
    if (!elf_probe32(image, (uint32_t)n)) return 0;
    if (elf_load32_metadata(image, (uint32_t)n, &meta) < 0) return 0;
    for (i = 0; i < (uint32_t)n; i++) bad_image[i] = image[i];
    bad_image[24] = (uint8_t)(USER_BASE & 0xFF);
    bad_image[25] = (uint8_t)((USER_BASE >> 8) & 0xFF);
    bad_image[26] = (uint8_t)((USER_BASE >> 16) & 0xFF);
    bad_image[27] = (uint8_t)((USER_BASE >> 24) & 0xFF);
    if (elf_load32_metadata(bad_image, (uint32_t)n, &meta) == 0) return 0;
    if (meta.entry < USER_BASE || meta.low_vaddr < USER_BASE || meta.load_segments == 0) return 0;
    fd = vfs_open("/bin/hostname", VFS_O_RDONLY);
    if (fd < 0) return 0;
    n = vfs_read(fd, image, sizeof(image));
    vfs_close(fd);
    if (n <= 0) return 0;
    if (!elf_probe32(image, (uint32_t)n)) return 0;
    if (elf_load32_metadata(image, (uint32_t)n, &meta) < 0) return 0;
    if (meta.entry < USER_BASE || meta.low_vaddr < USER_BASE || meta.load_segments == 0) return 0;
    for (i = 0; i < sizeof(overlap); i++) overlap[i] = 0;
    overlap[0] = 0x7F;
    overlap[1] = 'E';
    overlap[2] = 'L';
    overlap[3] = 'F';
    overlap[4] = 1;
    overlap[5] = 1;
    overlap[6] = 1;
    store16(overlap, 16, 2);
    store16(overlap, 18, 3);
    store32(overlap, 20, 1);
    store32(overlap, 24, USER_BASE);
    store32(overlap, 28, 0x34);
    store16(overlap, 40, 0x34);
    store16(overlap, 42, 0x20);
    store16(overlap, 44, 2);
    store32(overlap, 0x34, 1);
    store32(overlap, 0x38, 0x80);
    store32(overlap, 0x3C, USER_BASE);
    store32(overlap, 0x44, 1);
    store32(overlap, 0x48, 1);
    store32(overlap, 0x4C, ELF_PF_R | ELF_PF_X);
    store32(overlap, 0x50, 1);
    store32(overlap, 0x54, 1);
    store32(overlap, 0x58, 0x81);
    store32(overlap, 0x5C, USER_BASE);
    store32(overlap, 0x64, 1);
    store32(overlap, 0x68, 1);
    store32(overlap, 0x6C, ELF_PF_R | ELF_PF_X);
    store32(overlap, 0x70, 1);
    overlap[0x80] = 0xC3;
    overlap[0x81] = 0xC3;
    if (elf_load32_metadata(overlap, sizeof(overlap), &meta) == 0) return 0;
    if (elf_load32_segments(overlap,
                            sizeof(overlap),
                            elf_segment_should_not_run,
                            0) == 0) return 0;
    if (process_create_user_image("overlap", overlap, sizeof(overlap)) != 0) return 0;
    for (i = 0; i < sizeof(overlap); i++) overlap[i] = 0;
    overlap[0] = 0x7F;
    overlap[1] = 'E';
    overlap[2] = 'L';
    overlap[3] = 'F';
    overlap[4] = 1;
    overlap[5] = 1;
    overlap[6] = 1;
    store16(overlap, 16, 2);
    store16(overlap, 18, 3);
    store32(overlap, 20, 1);
    store32(overlap, 24, 0xFFFFF000U);
    store32(overlap, 28, 0x34);
    store16(overlap, 40, 0x34);
    store16(overlap, 42, 0x20);
    store16(overlap, 44, 1);
    store32(overlap, 0x34, 1);
    store32(overlap, 0x38, 0x80);
    store32(overlap, 0x3C, 0xFFFFF000U);
    store32(overlap, 0x44, 1);
    store32(overlap, 0x48, 0x2000);
    store32(overlap, 0x4C, ELF_PF_R | ELF_PF_X);
    store32(overlap, 0x50, 1);
    overlap[0x80] = 0xC3;
    if (elf_load32_segments(overlap,
                            sizeof(overlap),
                            elf_segment_should_not_run,
                            0) == 0) return 0;
    return 1;
}

static int test_paging_user_map(void) {
    uint32_t *dir = paging_clone_kernel_directory();
    void *ro_page;
    void *rw_page;
    void *dup_page;
    void *range_page;
    uint32_t ro_flags;
    uint32_t rw_flags;
    int dup_rejected;
    int range_rejected;
    int ok;
    if (!dir) return 0;
    ro_page = pmm_alloc();
    if (!ro_page) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    rw_page = pmm_alloc();
    if (!rw_page) {
        pmm_free(ro_page);
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)ro_page, PAGE_USER) < 0) {
        pmm_free(ro_page);
        pmm_free(rw_page);
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE + PAGE_SIZE, (uint32_t)rw_page, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(rw_page);
        paging_destroy_user_directory(dir);
        return 0;
    }
    dup_page = pmm_alloc();
    if (!dup_page) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    dup_rejected = paging_map_user_page(dir, USER_BASE, (uint32_t)dup_page, PAGE_USER | PAGE_WRITE) < 0;
    if (dup_rejected) pmm_free(dup_page);
    range_page = pmm_alloc();
    if (!range_page) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    range_rejected = paging_map_user_page(dir,
                                          USER_BASE - PAGE_SIZE,
                                          (uint32_t)range_page,
                                          PAGE_USER | PAGE_WRITE) < 0;
    if (range_rejected) pmm_free(range_page);
    ro_flags = paging_get_flags_in_directory(dir, USER_BASE);
    rw_flags = paging_get_flags_in_directory(dir, USER_BASE + PAGE_SIZE);
    ok = paging_get_phys_in_directory(dir, USER_BASE) == (uint32_t)ro_page &&
         paging_get_phys_in_directory(dir, USER_BASE + PAGE_SIZE) == (uint32_t)rw_page &&
         dup_rejected &&
         range_rejected &&
         paging_map_range(USER_BASE, USER_BASE, 0, PAGE_WRITE) == 0 &&
         paging_map_range(0xFFFFF000U, 0x00100000U, 0x2000U, PAGE_WRITE) < 0 &&
         (ro_flags & (PAGE_PRESENT | PAGE_USER)) == (PAGE_PRESENT | PAGE_USER) &&
         (ro_flags & PAGE_WRITE) == 0 &&
         (rw_flags & (PAGE_PRESENT | PAGE_USER | PAGE_WRITE)) ==
             (PAGE_PRESENT | PAGE_USER | PAGE_WRITE);
    paging_destroy_user_directory(dir);
    return ok;
}

static int test_pmm_allocator(void) {
    uint32_t before = pmm_free_pages();
    void *page = pmm_alloc();
    if (!page) return before == 0;
    if (((uint32_t)page & (PAGE_SIZE - 1)) != 0) {
        pmm_free(page);
        return 0;
    }
    if (pmm_free_pages() != before - 1) {
        pmm_free(page);
        return 0;
    }
    pmm_free(page);
    if (pmm_free_pages() != before) return 0;
    pmm_free(page);
    if (pmm_free_pages() != before) return 0;
    pmm_free((void *)PAGE_SIZE);
    return pmm_free_pages() == before;
}

static int test_kernel_heap(void) {
    void *a;
    void *b;
    void *c;
    size_t free_before = heap_free_bytes();
    size_t used_before = heap_used_bytes();
    size_t free_after_alloc;
    size_t used_after_alloc;
    if (free_before == 0 || heap_block_count() == 0 || heap_free_block_count() == 0) return 0;
    if (kmalloc(0) != 0) return 0;
    kfree((void *)PAGE_SIZE);
    if (heap_free_bytes() != free_before || heap_used_bytes() != used_before) return 0;
    a = kmalloc(64);
    b = kmalloc(64);
    if (!a || !b) {
        kfree(a);
        kfree(b);
        return 0;
    }
    free_after_alloc = heap_free_bytes();
    used_after_alloc = heap_used_bytes();
    if (free_after_alloc >= free_before || used_after_alloc < used_before + (size_t)128) {
        kfree(a);
        kfree(b);
        return 0;
    }
    if (((uint32_t)a & 7U) != 0 || ((uint32_t)b & 7U) != 0) {
        kfree(a);
        kfree(b);
        return 0;
    }
    kfree(a);
    kfree(a);
    kfree(b);
    if (heap_free_bytes() != free_before || heap_used_bytes() != used_before) return 0;
    c = kmalloc(128);
    if (!c) return 0;
    if (heap_used_bytes() < used_before + (size_t)128) {
        kfree(c);
        return 0;
    }
    kfree(c);
    return heap_free_bytes() == free_before && heap_used_bytes() == used_before;
}

static int test_process_heap(void) {
    process_t *p = process_spawn_path("/bin/hello");
    uint32_t old_break;
    uint32_t phys;
    uint32_t flags;
    int code = 0;
    int ok;
    if (!p || !p->page_dir || p->heap_start == 0 || p->heap_break != p->heap_start) return 0;
    old_break = p->heap_break;
    if (process_sbrk(p, 64) != (int)old_break) {
        (void)process_kill(p->pid, -9);
        (void)process_wait(p->pid, &code);
        return 0;
    }
    phys = paging_get_phys_in_directory((uint32_t *)p->page_dir, old_break);
    flags = paging_get_flags_in_directory((uint32_t *)p->page_dir, old_break);
    ok = p->heap_break == old_break + 64U &&
         phys != 0 &&
         (flags & (PAGE_PRESENT | PAGE_USER | PAGE_WRITE)) ==
             (PAGE_PRESENT | PAGE_USER | PAGE_WRITE) &&
         process_sbrk(p, -64) == (int)(old_break + 64U) &&
         p->heap_break == old_break &&
         paging_get_phys_in_directory((uint32_t *)p->page_dir, old_break) == 0 &&
         process_sbrk(p, -1) < 0;
    if (process_kill(p->pid, -9) < 0) return 0;
    if (process_wait(p->pid, &code) != 1) return 0;
    return ok && code == -9;
}

static int test_process_mmap(void) {
    process_t *p = process_spawn_path("/bin/hello");
    int mapped;
    uint32_t addr = 0;
    uint32_t phys0;
    uint32_t phys1;
    uint32_t phys2;
    uint32_t flags0;
    uint32_t flags1;
    char maps_path[64];
    uint32_t maps_path_pos = 0;
    int code = 0;
    int ok = 0;
    if (!p || !p->page_dir) return 0;
    append_str(maps_path, &maps_path_pos, sizeof(maps_path), "/proc/");
    append_dec(maps_path, &maps_path_pos, sizeof(maps_path), p->pid);
    append_str(maps_path, &maps_path_pos, sizeof(maps_path), "/maps");
    mapped = process_mmap(p, PAGE_SIZE + 64U, PROCESS_MMAP_WRITE);
    if (mapped >= 0) {
        addr = (uint32_t)mapped;
        phys0 = paging_get_phys_in_directory((uint32_t *)p->page_dir, addr);
        phys1 = paging_get_phys_in_directory((uint32_t *)p->page_dir, addr + PAGE_SIZE);
        phys2 = paging_get_phys_in_directory((uint32_t *)p->page_dir, addr + (2U * PAGE_SIZE));
        flags0 = paging_get_flags_in_directory((uint32_t *)p->page_dir, addr);
        flags1 = paging_get_flags_in_directory((uint32_t *)p->page_dir, addr + PAGE_SIZE);
        ok = addr >= USER_MMAP_BASE &&
             addr + (2U * PAGE_SIZE) <= USER_MMAP_TOP &&
             (addr & (PAGE_SIZE - 1U)) == 0 &&
             phys0 != 0 &&
             phys1 != 0 &&
             phys2 == 0 &&
             (flags0 & (PAGE_PRESENT | PAGE_USER | PAGE_WRITE)) ==
                 (PAGE_PRESENT | PAGE_USER | PAGE_WRITE) &&
             (flags1 & (PAGE_PRESENT | PAGE_USER | PAGE_WRITE)) ==
                 (PAGE_PRESENT | PAGE_USER | PAGE_WRITE);
        if (ok && process_mprotect(p, addr, PAGE_SIZE, 0) == 0) {
            ok = (paging_get_flags_in_directory((uint32_t *)p->page_dir, addr) & PAGE_WRITE) == 0 &&
                 file_text_contains(maps_path, "r-- mmap\n") &&
                 file_text_contains(maps_path, "rw- mmap\n");
        } else {
            ok = 0;
        }
        if (ok && process_mprotect(p, addr, PAGE_SIZE, PROCESS_MMAP_WRITE) == 0) {
            ok = (paging_get_flags_in_directory((uint32_t *)p->page_dir, addr) & PAGE_WRITE) != 0 &&
                 file_text_contains(maps_path, "rw- mmap\n");
        } else {
            ok = 0;
        }
        ok = ok &&
             process_mprotect(p, addr + 1U, PAGE_SIZE, 0) < 0 &&
             process_mprotect(p, USER_BASE, PAGE_SIZE, 0) < 0 &&
             process_mprotect(p, addr, PAGE_SIZE, 0x80000000U) < 0 &&
             process_mmap(p, 0, PROCESS_MMAP_WRITE) < 0 &&
             process_mmap(p, PAGE_SIZE, 0x80000000U) < 0 &&
             process_munmap(p, addr + 1U, PAGE_SIZE) < 0 &&
             process_munmap(p, USER_BASE, PAGE_SIZE) < 0 &&
             process_munmap(p, addr, PAGE_SIZE + 64U) == 0 &&
             paging_get_phys_in_directory((uint32_t *)p->page_dir, addr) == 0 &&
             paging_get_phys_in_directory((uint32_t *)p->page_dir, addr + PAGE_SIZE) == 0 &&
             process_munmap(p, addr, PAGE_SIZE) < 0;
    }
    if (process_kill(p->pid, -9) < 0) return 0;
    if (process_wait(p->pid, &code) != 1) return 0;
    return ok && code == -9;
}

static int test_process_argv(void) {
    const char *const args[3] = { "/bin/hello", "one", "two" };
    const char *const too_many[PROCESS_MAX_ARGS + 1] = { "a", "b", "c", "d", "e" };
    process_t *proc = process_spawn_path_args("/bin/hello", 3, args);
    uint32_t argc_value = 0;
    uint32_t argv_ptr = 0;
    uint32_t envp_ptr = 0;
    uint32_t arg0 = 0;
    uint32_t arg1 = 0;
    uint32_t arg2 = 0;
    uint32_t arg_end = 1;
    uint32_t env0 = 0;
    uint32_t env_end = 1;
    uint32_t envc;
    char proc_cmd_path[64];
    char proc_cmdline[128];
    uint32_t proc_cmd_path_pos = 0;
    int proc_cmd_fd;
    int proc_cmd_n;
    int code = 0;
    int ok;
    if (!proc) return 0;
    envc = (uint32_t)process_env_count(proc);
    ok = proc->argc == 3 &&
         proc->user_stack >= USER_STACK_TOP - USER_STACK_SIZE &&
         proc->user_stack + 12U < USER_STACK_TOP &&
         read_process_u32(proc, proc->user_stack, &argc_value) &&
         read_process_u32(proc, proc->user_stack + 4U, &argv_ptr) &&
         read_process_u32(proc, proc->user_stack + 8U, &envp_ptr) &&
         argc_value == 3 &&
         read_process_u32(proc, argv_ptr, &arg0) &&
         read_process_u32(proc, argv_ptr + 4U, &arg1) &&
         read_process_u32(proc, argv_ptr + 8U, &arg2) &&
         read_process_u32(proc, argv_ptr + 12U, &arg_end) &&
         arg_end == 0 &&
         user_string_eq(proc, arg0, "/bin/hello") &&
         user_string_eq(proc, arg1, "one") &&
         user_string_eq(proc, arg2, "two") &&
         envc > 0 &&
         read_process_u32(proc, envp_ptr, &env0) &&
         read_process_u32(proc, envp_ptr + envc * 4U, &env_end) &&
         env_end == 0 &&
         user_string_eq(proc, env0, "PATH=/bin");
    append_str(proc_cmd_path, &proc_cmd_path_pos, sizeof(proc_cmd_path), "/proc/");
    append_dec(proc_cmd_path, &proc_cmd_path_pos, sizeof(proc_cmd_path), proc->pid);
    append_str(proc_cmd_path, &proc_cmd_path_pos, sizeof(proc_cmd_path), "/cmdline");
    proc_cmd_fd = vfs_open(proc_cmd_path, VFS_O_RDONLY);
    if (proc_cmd_fd < 0) {
        ok = 0;
    } else {
        proc_cmd_n = vfs_read(proc_cmd_fd, proc_cmdline, sizeof(proc_cmdline) - 1);
        vfs_close(proc_cmd_fd);
        if (proc_cmd_n <= 0) {
            ok = 0;
        } else {
            proc_cmdline[proc_cmd_n] = 0;
            ok = ok && contains_text(proc_cmdline, "/bin/hello one two\n");
        }
    }
    if (process_kill(proc->pid, -9) < 0) return 0;
    if (process_wait(proc->pid, &code) != 1) return 0;
    if (process_spawn_path_args("/bin/hello", PROCESS_MAX_ARGS + 1, too_many) != 0) return 0;
    return ok && code == -9;
}

static int test_process_exec_replace(void) {
    const char *const args[2] = { "/bin/hostname", "node" };
    process_t *proc = process_spawn_path("/bin/hello");
    uint32_t pid;
    uint32_t old_dir;
    uint32_t argc_value = 0;
    uint32_t argv_ptr = 0;
    uint32_t arg0 = 0;
    uint32_t arg1 = 0;
    uint32_t arg_end = 1;
    uint32_t i;
    int task_updated = 0;
    int code = 0;
    int close_fd;
    int keep_fd;
    int ok;
    if (!proc) return 0;
    pid = proc->pid;
    old_dir = proc->page_dir;
    close_fd = vfs_open("/etc/hostname", VFS_O_RDONLY);
    keep_fd = vfs_open("/etc/hostname", VFS_O_RDONLY);
    if (close_fd < 0 || keep_fd < 0 || proc->fds[3] >= 0 || proc->fds[4] >= 0) {
        if (close_fd >= 0) vfs_close(close_fd);
        if (keep_fd >= 0) vfs_close(keep_fd);
        (void)process_kill(pid, -9);
        (void)process_wait(pid, &code);
        return 0;
    }
    proc->fds[3] = close_fd;
    proc->fd_flags[3] = PROCESS_FD_CLOEXEC;
    proc->fds[4] = keep_fd;
    proc->fd_flags[4] = 0;
    if (process_replace_path_args(proc, "/bin/hostname", 2, args) < 0) {
        (void)process_kill(pid, -9);
        (void)process_wait(pid, &code);
        return 0;
    }
    for (i = 0; i < task_table_size(); i++) {
        const task_t *task = task_at(i);
        if (task && task->process_id == pid && task->regs.cr3 == proc->page_dir) {
            task_updated = 1;
            break;
        }
    }
    ok = proc->pid == pid &&
         proc->page_dir != 0 &&
         proc->page_dir != old_dir &&
         proc->entry >= USER_BASE &&
         proc->entry < USER_STACK_TOP &&
         proc->user_stack >= USER_STACK_TOP - USER_STACK_SIZE &&
         proc->user_stack + 12U < USER_STACK_TOP &&
         proc->argc == 2 &&
         streq(proc->name, "/bin/hostname") &&
         task_updated &&
         proc->fds[3] < 0 &&
         proc->fd_flags[3] == 0 &&
         proc->fds[4] == keep_fd &&
         proc->fd_flags[4] == 0 &&
         read_process_u32(proc, proc->user_stack, &argc_value) &&
         read_process_u32(proc, proc->user_stack + 4U, &argv_ptr) &&
         argc_value == 2 &&
         read_process_u32(proc, argv_ptr, &arg0) &&
         read_process_u32(proc, argv_ptr + 4U, &arg1) &&
         read_process_u32(proc, argv_ptr + 8U, &arg_end) &&
         arg_end == 0 &&
         user_string_eq(proc, arg0, "/bin/hostname") &&
         user_string_eq(proc, arg1, "node");
    if (process_kill(pid, -9) < 0) return 0;
    if (process_wait(pid, &code) != 1) return 0;
    return ok && code == -9;
}

static int test_process_fork_user(void) {
    const char *const args[2] = { "/bin/hello", "fork" };
    process_t *parent = process_spawn_path_args("/bin/hello", 2, args);
    process_t *child;
    selftest_syscall_frame_t frame;
    uint32_t *old_dir = paging_current_directory();
    uint32_t old_pid = task_current_process_id();
    uint32_t old_as = task_current_address_space();
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int fd;
    int child_pid;
    int parent_code = 0;
    int child_code = 0;
    int task_ok = 0;
    int copy_ok = 0;
    int ok = 0;
    uint32_t parent_argv = 0;
    uint32_t parent_arg0 = 0;
    uint32_t child_argv = 0;
    uint32_t child_arg0 = 0;
    uint32_t child_arg1 = 0;
    uint32_t child_arg_end = 1;
    uint32_t argc_value = 0;
    uint32_t phys;
    uint32_t i;
    if (!parent || !parent->page_dir) return 0;
    fd = vfs_open("/etc/hostname", VFS_O_RDONLY);
    if (fd < 0 || parent->fds[3] >= 0) {
        if (fd >= 0) vfs_close(fd);
        (void)process_kill(parent->pid, -9);
        (void)process_wait(parent->pid, &parent_code);
        return 0;
    }
    parent->fds[3] = fd;
    parent->fd_flags[3] = PROCESS_FD_CLOEXEC;
    selftest_memzero((uint8_t *)&frame, sizeof(frame));
    frame.gs = GDT_USER_DATA;
    frame.fs = GDT_USER_DATA;
    frame.es = GDT_USER_DATA;
    frame.ds = GDT_USER_DATA;
    frame.edi = 0x11111111U;
    frame.esi = 0x22222222U;
    frame.ebp = 0x33333333U;
    frame.ebx = 0x44444444U;
    frame.edx = 0x55555555U;
    frame.ecx = 0x66666666U;
    frame.eax = SYS_FORK;
    frame.eip = parent->entry;
    frame.cs = GDT_USER_CODE;
    frame.eflags = 0x202U;
    frame.user_esp = parent->user_stack;
    frame.ss = GDT_USER_DATA;
    scheduler_set_preemption(0, quantum);
    task_set_current_process_id(parent->pid);
    task_set_current_address_space(parent->page_dir);
    paging_switch_directory((uint32_t *)parent->page_dir);
    child_pid = (int)syscall_handler_c_frame(SYS_FORK, 0, 0, 0, 0, &frame);
    paging_switch_directory(old_dir);
    task_set_current_process_id(old_pid);
    task_set_current_address_space(old_as);
    scheduler_set_preemption(preempt, quantum);
    child = child_pid >= 0 ? process_find((uint32_t)child_pid) : 0;
    if (!child) {
        (void)process_kill(parent->pid, -9);
        (void)process_wait(parent->pid, &parent_code);
        return 0;
    }
    for (i = 0; i < task_table_size(); i++) {
        const task_t *task = task_at(i);
        if (task && task->process_id == (uint32_t)child_pid) {
            const uint32_t *sp = (const uint32_t *)task->regs.esp;
            task_ok = task->regs.cr3 == child->page_dir &&
                      sp[4] == frame.edi &&
                      sp[5] == frame.esi &&
                      sp[6] == frame.ebp &&
                      sp[8] == frame.ebx &&
                      sp[9] == frame.edx &&
                      sp[10] == frame.ecx &&
                      sp[11] == 0 &&
                      sp[12] == frame.eip &&
                      sp[13] == GDT_USER_CODE &&
                      sp[15] == frame.user_esp &&
                      sp[16] == GDT_USER_DATA;
            break;
        }
    }
    if (read_process_u32(parent, parent->user_stack + 4U, &parent_argv) &&
        read_process_u32(parent, parent_argv, &parent_arg0) &&
        read_process_u32(child, child->user_stack + 4U, &child_argv) &&
        read_process_u32(child, child_argv, &child_arg0)) {
        phys = paging_get_phys_in_directory((uint32_t *)parent->page_dir, parent_arg0);
        if (phys) {
            ((char *)phys)[0] = 'X';
            copy_ok = user_string_eq(child, child_arg0, "/bin/hello");
        }
    }
    ok = child->pid == (uint32_t)child_pid &&
         child->ppid == parent->pid &&
         child->page_dir != 0 &&
         child->page_dir != parent->page_dir &&
         child->entry == parent->entry &&
         child->user_stack == frame.user_esp &&
         child->heap_start == parent->heap_start &&
         child->heap_break == parent->heap_break &&
         child->argc == parent->argc &&
         streq(child->name, parent->name) &&
         child->fds[3] == parent->fds[3] &&
         child->fd_flags[3] == PROCESS_FD_CLOEXEC &&
         read_process_u32(child, child->user_stack, &argc_value) &&
         argc_value == 2 &&
         read_process_u32(child, child_argv + 4U, &child_arg1) &&
         read_process_u32(child, child_argv + 8U, &child_arg_end) &&
         child_arg_end == 0 &&
         user_string_eq(child, child_arg0, "/bin/hello") &&
         user_string_eq(child, child_arg1, "fork") &&
         task_ok &&
         copy_ok;
    if (process_kill((uint32_t)child_pid, -9) < 0) ok = 0;
    if (process_wait((uint32_t)child_pid, &child_code) != 1 || child_code != -9) ok = 0;
    if (process_kill(parent->pid, -9) < 0) ok = 0;
    if (process_wait(parent->pid, &parent_code) != 1 || parent_code != -9) ok = 0;
    return ok;
}

static int test_process_env(void) {
    process_t *parent = process_current();
    process_t *proc;
    int code = 0;
    int ok = 0;
    (void)process_env_unset(parent, "SELFTEST_ENV");
    if (process_env_set(parent, "BAD-NAME", "x", 1) == 0) return 0;
    if (process_env_set(parent, "SELFTEST_ENV", "parent", 1) < 0) return 0;
    if (process_env_set(parent, "SELFTEST_ENV", "ignored", 0) < 0) return 0;
    if (!streq(process_env_get(parent, "SELFTEST_ENV"), "parent")) {
        (void)process_env_unset(parent, "SELFTEST_ENV");
        return 0;
    }
    if (process_env_set(parent, "SELFTEST_ENV", "child", 1) < 0) {
        (void)process_env_unset(parent, "SELFTEST_ENV");
        return 0;
    }
    proc = process_spawn_path("/bin/hello");
    if (proc) {
        ok = streq(process_env_get(proc, "SELFTEST_ENV"), "child") &&
             user_env_contains(proc, "SELFTEST_ENV=child") &&
             process_env_count(proc) >= 6 &&
             process_env_unset(parent, "SELFTEST_ENV") == 0 &&
             process_env_get(parent, "SELFTEST_ENV") == 0 &&
             streq(process_env_get(proc, "SELFTEST_ENV"), "child");
        if (process_kill(proc->pid, -9) < 0) ok = 0;
        if (process_wait(proc->pid, &code) != 1) ok = 0;
    }
    (void)process_env_unset(parent, "SELFTEST_ENV");
    return ok && code == -9;
}

static int test_process_umask(void) {
    process_t *parent = process_current();
    uint32_t old_mask = process_umask_get(parent);
    process_t *proc;
    int code = 0;
    int ok = 0;
    if (process_umask_set(parent, 0000077U) < 0) return 0;
    proc = process_spawn_path("/bin/hello");
    if (proc) {
        ok = process_umask_get(proc) == 0000077U;
        if (process_kill(proc->pid, -9) < 0) ok = 0;
        if (process_wait(proc->pid, &code) != 1) ok = 0;
    }
    if (process_umask_set(parent, old_mask) < 0) ok = 0;
    return ok && code == -9 && process_umask_get(parent) == old_mask;
}

static int test_user_process_exit(void) {
    process_t *proc = process_spawn_path("/bin/hello");
    uint32_t pid;
    uint32_t guard = 128;
    int code = -1;
    if (!proc || proc->pid == 0) return 0;
    pid = proc->pid;
    while (guard--) {
        process_t *cur = process_find(pid);
        if (!cur) return 0;
        if (cur->state == PROC_ZOMBIE) break;
        task_yield();
    }
    proc = process_find(pid);
    if (!proc) return 0;
    if (proc->state != PROC_ZOMBIE) {
        (void)process_kill(pid, -9);
        (void)process_wait(pid, &code);
        return 0;
    }
    if (process_wait(pid, &code) != 1) return 0;
    return code == 0 && process_find(pid) == 0;
}

static int test_user_fault_isolation(void) {
    uint32_t before_faults = exception_count(14);
    process_t *proc = process_spawn_path("/bin/fault");
    uint32_t pid;
    uint32_t guard = 128;
    int code = 0;
    if (!proc || proc->pid == 0) return 0;
    pid = proc->pid;
    while (guard--) {
        process_t *cur = process_find(pid);
        if (!cur) return 0;
        if (cur->state == PROC_ZOMBIE) break;
        task_yield();
    }
    proc = process_find(pid);
    if (!proc) return 0;
    if (proc->state != PROC_ZOMBIE) {
        (void)process_kill(pid, -9);
        (void)process_wait(pid, &code);
        return 0;
    }
    if (process_wait(pid, &code) != 1) return 0;
    return code == -14 &&
           exception_count(14) == before_faults + 1U &&
           exception_last_vector() == 14 &&
           exception_last_cr2() == 0xC0000000U &&
           process_find(pid) == 0;
}

static int test_usermode_entry_validation(void) {
    return usermode_enter(0, 0) < 0 &&
           usermode_enter(USER_BASE, USER_STACK_TOP) < 0 &&
           usermode_enter(USER_STACK_TOP - USER_STACK_SIZE,
                          USER_STACK_TOP - 4U) < 0 &&
           usermode_enter(USER_BASE, USER_STACK_TOP - 2U) < 0;
}

static void selftest_sleep_entry(void);

static int test_process_table(void) {
    process_t *cur = process_current();
    process_t *child;
    int code = 0;
    if (!cur || !process_find(0) || process_count() == 0) return 0;
    child = process_create_kernel("ppid-test", selftest_sleep_entry);
    if (!child) return 0;
    if (child->pid == 0 || child->pid == cur->pid || child->ppid != cur->pid) {
        (void)process_kill(child->pid, -9);
        (void)process_wait(child->pid, &code);
        return 0;
    }
    if (process_kill(child->pid, -9) < 0) return 0;
    if (process_wait(child->pid, &code) != 1) return 0;
    return code == -9;
}

static int test_process_orphan_reparent(void) {
    process_t *parent = process_create_kernel("orphan-parent", selftest_sleep_entry);
    process_t *child;
    uint32_t parent_pid;
    uint32_t child_pid;
    int parent_code = 0;
    int child_code = 0;
    int ok = 0;
    if (!parent) return 0;
    parent_pid = parent->pid;
    task_set_current_process_id(parent_pid);
    child = process_spawn_path("/bin/hello");
    task_set_current_process_id(0);
    if (!child) {
        (void)process_kill(parent_pid, -9);
        (void)process_wait(parent_pid, &parent_code);
        return 0;
    }
    child_pid = child->pid;
    if (child->ppid == parent_pid &&
        process_kill(parent_pid, -9) == 0 &&
        child->ppid == 0) {
        ok = 1;
    }
    if (process_find(child_pid)) (void)process_kill(child_pid, -9);
    if (process_find(child_pid)) (void)process_wait(child_pid, &child_code);
    if (process_find(parent_pid)) (void)process_wait(parent_pid, &parent_code);
    return ok && parent_code == -9 && child_code == -9;
}

static int test_process_fd_table(void) {
    int vfs_fd = vfs_open("/etc/hostname", VFS_O_RDONLY);
    int proc_fd;
    int dup_fd;
    int dup2_fd;
    char c;
    int ok;
    if (vfs_fd < 0) return 0;
    proc_fd = process_fd_install(vfs_fd);
    if (proc_fd < 0) {
        vfs_close(vfs_fd);
        return 0;
    }
    if (process_fd_set_flags(proc_fd, PROCESS_FD_CLOEXEC) < 0) {
        process_fd_close(proc_fd);
        return 0;
    }
    dup_fd = process_fd_dup(proc_fd);
    if (dup_fd < 0) {
        process_fd_close(proc_fd);
        return 0;
    }
    dup2_fd = process_fd_dup2(proc_fd, PROCESS_MAX_FDS - 1);
    if (dup2_fd < 0) {
        process_fd_close(dup_fd);
        process_fd_close(proc_fd);
        return 0;
    }
    ok = proc_fd >= 3 &&
         proc_fd < PROCESS_MAX_FDS &&
         dup_fd >= 3 &&
         dup_fd < PROCESS_MAX_FDS &&
         dup_fd != proc_fd &&
         dup2_fd == PROCESS_MAX_FDS - 1 &&
         process_fd_get_flags(proc_fd) == (int)PROCESS_FD_CLOEXEC &&
         process_fd_get_flags(dup_fd) == 0 &&
         process_fd_get_flags(dup2_fd) == 0 &&
         process_fd_set_flags(proc_fd, PROCESS_FD_CLOEXEC | 0x80000000U) < 0 &&
         process_fd_resolve(dup_fd) == vfs_fd &&
         process_fd_resolve(dup2_fd) == vfs_fd &&
         process_fd_resolve(proc_fd) == vfs_fd &&
         process_fd_close(proc_fd) == 0 &&
         process_fd_resolve(proc_fd) < 0 &&
         process_fd_get_flags(proc_fd) < 0 &&
         process_fd_resolve(dup_fd) == vfs_fd &&
         process_fd_resolve(dup2_fd) == vfs_fd &&
         vfs_read(process_fd_resolve(dup_fd), &c, 1) == 1 &&
         process_fd_close(dup_fd) == 0 &&
         process_fd_resolve(dup_fd) < 0 &&
         process_fd_close(dup2_fd) == 0 &&
         process_fd_resolve(dup2_fd) < 0 &&
         process_fd_resolve(0) < 0;
    if (!ok) {
        (void)process_fd_close(proc_fd);
        (void)process_fd_close(dup_fd);
        (void)process_fd_close(dup2_fd);
    }
    return ok;
}

static int test_process_cwd(void) {
    char old[PROCESS_CWD_MAX];
    char resolved[PROCESS_CWD_MAX];
    int ok;
    strcopy(old, process_cwd(), sizeof(old));
    if (process_resolve_path("", resolved, sizeof(resolved)) == 0) return 0;
    if (process_chdir("/tmp") < 0) return 0;
    ok = streq(process_cwd(), "/tmp") &&
         process_resolve_path("cwd.txt", resolved, sizeof(resolved)) == 0 &&
         streq(resolved, "/tmp/cwd.txt") &&
         process_resolve_path("../etc/hostname", resolved, sizeof(resolved)) == 0 &&
         streq(resolved, "/etc/hostname") &&
         process_chdir("/no-such-dir") < 0;
    if (process_chdir(old) < 0) return 0;
    return ok && streq(process_cwd(), old);
}

static int test_process_pipe(void) {
    int fds[2];
    int bad_fds[2];
    int read_vfs;
    int write_vfs;
    char buf[8];
    int n;
    if (process_pipe(fds) < 0) return 0;
    read_vfs = process_fd_resolve(fds[0]);
    write_vfs = process_fd_resolve(fds[1]);
    if (read_vfs < 0 || write_vfs < 0) {
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    if (vfs_poll(read_vfs, VFS_POLL_READ) != 0 ||
        vfs_poll(write_vfs, VFS_POLL_WRITE) != VFS_POLL_WRITE) {
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    if (vfs_write(write_vfs, "pipe", 4) != 4) {
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    if (vfs_poll(read_vfs, VFS_POLL_READ) != VFS_POLL_READ) {
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    if (vfs_read(read_vfs, 0, 0) != 0 || vfs_write(write_vfs, 0, 0) != 0) {
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    n = vfs_read(read_vfs, buf, sizeof(buf));
    if (n != 4 || !memeq(buf, "pipe", 4)) {
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    if (vfs_read(read_vfs, buf, sizeof(buf)) != 0) {
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    if (vfs_set_fd_flags(read_vfs, VFS_O_RDONLY | VFS_O_NONBLOCK) < 0 ||
        vfs_read(read_vfs, buf, 1) >= 0 ||
        vfs_set_fd_flags(write_vfs, VFS_O_WRONLY | VFS_O_NONBLOCK) < 0) {
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    {
        uint32_t fills = 0;
        while (fills < 256U && vfs_write(write_vfs, "x", 1) == 1) fills++;
        if (fills == 0 || fills >= 256U || vfs_write(write_vfs, "y", 1) >= 0) {
            (void)process_fd_close(fds[0]);
            (void)process_fd_close(fds[1]);
            return 0;
        }
    }
    if (process_fd_close(fds[0]) != 0) {
        (void)process_fd_close(fds[1]);
        return 0;
    }
    if (vfs_write(write_vfs, "x", 1) >= 0) {
        (void)process_fd_close(fds[1]);
        return 0;
    }
    if (process_fd_close(fds[1]) != 0) return 0;
    if (process_pipe_flags(fds, PROCESS_FD_CLOEXEC) < 0) return 0;
    if (process_fd_get_flags(fds[0]) != (int)PROCESS_FD_CLOEXEC ||
        process_fd_get_flags(fds[1]) != (int)PROCESS_FD_CLOEXEC) {
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    if (process_pipe_flags(bad_fds, PROCESS_FD_CLOEXEC | 0x80000000U) == 0) {
        (void)process_fd_close(bad_fds[0]);
        (void)process_fd_close(bad_fds[1]);
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    return process_fd_close(fds[0]) == 0 &&
           process_fd_close(fds[1]) == 0;
}

static void selftest_sleep_entry(void) {
    while (1) task_sleep(1000);
}

static void selftest_return_entry(void) {
    g_selftest_return_runs++;
}

static int test_process_kill(void) {
    process_t *p = process_create_kernel("kill-test", selftest_sleep_entry);
    uint32_t old_pid = task_current_process_id();
    int code = 0;
    if (process_wait(0, &code) >= 0) return 0;
    if (!p || p->pid == 0) return 0;
    task_set_current_process_id(p->pid);
    if (process_wait(p->pid, &code) >= 0) {
        task_set_current_process_id(old_pid);
        (void)process_kill(p->pid, -9);
        (void)process_wait(p->pid, &code);
        return 0;
    }
    task_set_current_process_id(old_pid);
    if (process_kill(p->pid, -9) < 0) return 0;
    if (!process_find(p->pid) || process_find(p->pid)->state != PROC_ZOMBIE) return 0;
    if (process_wait(p->pid, &code) != 1) return 0;
    return code == -9 && process_find(p->pid) == 0;
}

static int test_process_return_exit(void) {
    process_t *p = process_create_kernel("return-test", selftest_return_entry);
    uint32_t before = g_selftest_return_runs;
    uint32_t pid;
    int code = -1;
    int ok;
    if (!p || p->pid == 0) return 0;
    pid = p->pid;
    task_yield();
    p = process_find(pid);
    ok = g_selftest_return_runs == before + 1U &&
         p &&
         p->state == PROC_ZOMBIE;
    if (process_wait(pid, &code) != 1) return 0;
    return ok && code == 0 && process_find(pid) == 0;
}

static int test_process_wait_permissions(void) {
    process_t *owner = process_create_kernel("wait-owner", selftest_sleep_entry);
    process_t *other = process_create_kernel("wait-other", selftest_sleep_entry);
    uint32_t old_pid = task_current_process_id();
    int code = 0;
    int denied;
    if (!owner || !other) {
        if (owner) {
            (void)process_kill(owner->pid, -9);
            (void)process_wait(owner->pid, &code);
        }
        if (other) {
            (void)process_kill(other->pid, -9);
            (void)process_wait(other->pid, &code);
        }
        return 0;
    }
    if (process_kill(other->pid, -9) < 0) {
        (void)process_kill(owner->pid, -9);
        (void)process_wait(owner->pid, &code);
        return 0;
    }
    task_set_current_process_id(owner->pid);
    denied = process_wait(other->pid, &code) < 0;
    task_set_current_process_id(old_pid);
    if (process_kill(owner->pid, -9) < 0) return 0;
    if (process_wait(owner->pid, &code) != 1) return 0;
    if (process_wait(other->pid, &code) != 1) return 0;
    return denied;
}

static int test_process_wait_any(void) {
    process_t *p = process_create_kernel("wait-any", selftest_sleep_entry);
    uint32_t pid;
    int code = 123;
    int reaped;
    if (!p || p->pid == 0) return 0;
    pid = p->pid;
    if (process_kill(pid, -9) < 0) return 0;
    reaped = process_wait_any(&code);
    return reaped == (int)pid &&
           code == -9 &&
           process_find(pid) == 0;
}

static int test_process_kill_permissions(void) {
    process_t *owner = process_create_kernel("kill-owner", selftest_sleep_entry);
    process_t *other = process_create_kernel("kill-other", selftest_sleep_entry);
    uint32_t old_pid = task_current_process_id();
    int code = 0;
    int denied;
    if (!owner || !other) {
        if (owner) {
            (void)process_kill(owner->pid, -9);
            (void)process_wait(owner->pid, &code);
        }
        if (other) {
            (void)process_kill(other->pid, -9);
            (void)process_wait(other->pid, &code);
        }
        return 0;
    }
    task_set_current_process_id(owner->pid);
    denied = process_kill(other->pid, -9) < 0;
    task_set_current_process_id(old_pid);
    if (!denied) return 0;
    {
        process_t *still_other = process_find(other->pid);
        if (!still_other || still_other->state == PROC_ZOMBIE) return 0;
    }
    if (process_kill(owner->pid, -9) < 0) return 0;
    if (process_wait(owner->pid, &code) != 1) return 0;
    if (process_kill(other->pid, -9) < 0) return 0;
    if (process_wait(other->pid, &code) != 1) return 0;
    return code == -9;
}

static int test_process_groups(void) {
    uint32_t old_pid = task_current_process_id();
    process_t *leader = process_create_kernel("pgrp-leader", selftest_sleep_entry);
    process_t *child;
    uint32_t leader_pid;
    uint32_t child_pid = 0;
    int leader_code = 0;
    int child_code = 0;
    int ok = 0;
    if (!leader || leader->pid == 0) return 0;
    leader_pid = leader->pid;
    task_set_current_process_id(leader_pid);
    child = process_create_kernel("pgrp-child", selftest_sleep_entry);
    if (child && child->pid != 0) {
        child_pid = child->pid;
        ok = child->ppid == leader_pid &&
             child->pgid == leader->pgid &&
             child->sid == leader->sid &&
             process_getpgid(child_pid) == (int)leader->pgid &&
             process_getsid(child_pid) == (int)leader->sid &&
             process_setpgid(child_pid, 0) == 0 &&
             process_getpgid(child_pid) == (int)child_pid &&
             process_setpgid(child_pid, leader->pgid) == 0 &&
             process_getpgid(child_pid) == (int)leader->pgid;
        task_set_current_process_id(child_pid);
        ok = ok &&
             process_setsid() == (int)child_pid &&
             process_getpgid(0) == (int)child_pid &&
             process_getsid(0) == (int)child_pid;
        task_set_current_process_id(leader_pid);
        ok = ok && process_setpgid(child_pid, leader->pgid) < 0;
    }
    task_set_current_process_id(old_pid);
    if (child_pid && process_find(child_pid)) {
        (void)process_kill(child_pid, -9);
        (void)process_wait(child_pid, &child_code);
    }
    if (process_find(leader_pid)) {
        (void)process_kill(leader_pid, -9);
        (void)process_wait(leader_pid, &leader_code);
    }
    return ok && process_find(child_pid) == 0 && process_find(leader_pid) == 0;
}

static int test_process_kill_group(void) {
    uint32_t old_pid = task_current_process_id();
    process_t *leader = process_create_kernel("killpg-leader", selftest_sleep_entry);
    process_t *child1;
    process_t *child2;
    uint32_t leader_pid;
    uint32_t child1_pid = 0;
    uint32_t child2_pid = 0;
    int code_leader = 0;
    int code_child1 = 0;
    int code_child2 = 0;
    int ok;
    if (!leader || leader->pid == 0) return 0;
    leader_pid = leader->pid;
    task_set_current_process_id(leader_pid);
    child1 = process_create_kernel("killpg-child1", selftest_sleep_entry);
    child2 = process_create_kernel("killpg-child2", selftest_sleep_entry);
    task_set_current_process_id(old_pid);
    if (!child1 || !child2 || child1->pid == 0 || child2->pid == 0) {
        if (child1) {
            (void)process_kill(child1->pid, -9);
            (void)process_wait(child1->pid, &code_child1);
        }
        if (child2) {
            (void)process_kill(child2->pid, -9);
            (void)process_wait(child2->pid, &code_child2);
        }
        (void)process_kill(leader_pid, -9);
        (void)process_wait(leader_pid, &code_leader);
        return 0;
    }
    child1_pid = child1->pid;
    child2_pid = child2->pid;
    ok = child1->pgid == leader->pgid &&
         child2->pgid == leader->pgid &&
         process_kill_group(leader->pgid, -15) == 0 &&
         process_wait(leader_pid, &code_leader) == 1 &&
         process_wait(child1_pid, &code_child1) == 1 &&
         process_wait(child2_pid, &code_child2) == 1 &&
         code_leader == -15 &&
         code_child1 == -15 &&
         code_child2 == -15 &&
         process_find(leader_pid) == 0 &&
         process_find(child1_pid) == 0 &&
         process_find(child2_pid) == 0;
    return ok;
}

static int test_syscall_validation(void) {
    uint32_t bad_user = USER_BASE;
    return syscall_handler_c(SYS_OPEN, 0, VFS_O_RDONLY, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_WRITE, 1, 0, 0, 0) == 0 &&
           syscall_handler_c(SYS_WRITE, 1, bad_user, 1, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_READ, 0, bad_user, 1, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_WRITE, 99, 0, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_READ, 99, 0, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_GETCWD, bad_user, 8, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_CHDIR, bad_user, 0, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_WAIT, 1, bad_user, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_PIPE, bad_user, 0, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_PIPE2, bad_user, PROCESS_FD_CLOEXEC, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_PIPE2,
                             bad_user,
                             PROCESS_FD_CLOEXEC | 0x80000000U,
                             0,
                             0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_SPAWN_ARGS, 0, PROCESS_MAX_ARGS + 1, bad_user, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_EXEC, bad_user, 0, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_EXEC_ARGS, bad_user, 0, bad_user, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_FORK, 0, 0, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_GETENV, bad_user, bad_user, PROCESS_ENV_MAX, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_SETENV, bad_user, bad_user, 1, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_UNSETENV, bad_user, 0, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_ENV_INFO, 0, bad_user, PROCESS_ENV_MAX, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_POLL_MANY, bad_user, 1, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_GETHOSTNAME, bad_user, UTS_FIELD_MAX, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_SETHOSTNAME, bad_user, 0, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_UMASK, 0001000U, 0, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(SYS_RMDIR, bad_user, 0, 0, 0) == (uint32_t)-1 &&
           syscall_handler_c(0xFFFFU, 0, 0, 0, 0) == (uint32_t)-1;
}

static int test_syscall_path_copy(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    void *phys;
    char old_cwd[PROCESS_CWD_MAX];
    int ok = 0;
    uint32_t i;
    strcopy(old_cwd, process_cwd(), sizeof(old_cwd));
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        char *unterminated = (char *)(USER_BASE + 128U);
        strcopy(path, "/tmp", 32);
        for (i = 0; i < VFS_MAX_PATH; i++) unterminated[i] = 'x';
        ok = syscall_handler_c(SYS_CHDIR, (uint32_t)path, 0, 0, 0) == 0 &&
             streq(process_cwd(), "/tmp") &&
             syscall_handler_c(SYS_STAT, (uint32_t)path, USER_BASE + PAGE_SIZE, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_READDIR, (uint32_t)path, 0, USER_BASE + PAGE_SIZE, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_OPEN, (uint32_t)unterminated, VFS_O_RDONLY, 0, 0) == (uint32_t)-1;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    if (process_chdir(old_cwd) < 0) ok = 0;
    paging_destroy_user_directory(dir);
    return ok && streq(process_cwd(), old_cwd);
}

static int test_syscall_dirfd(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    char old_cwd[PROCESS_CWD_MAX];
    void *phys;
    int file_fd = -1;
    int vfs_fd = -1;
    int proc_fd = -1;
    int ok = 0;
    strcopy(old_cwd, process_cwd(), sizeof(old_cwd));
    (void)vfs_unlink("/tmp/sys-dirfd/file.txt");
    (void)vfs_unlink("/tmp/sys-dirfd");
    if (vfs_mkdir("/tmp/sys-dirfd") < 0) {
        if (dir) paging_destroy_user_directory(dir);
        return 0;
    }
    file_fd = vfs_open("/tmp/sys-dirfd/file.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (file_fd < 0) {
        if (dir) paging_destroy_user_directory(dir);
        (void)vfs_unlink("/tmp/sys-dirfd");
        return 0;
    }
    vfs_close(file_fd);
    if (!dir) {
        (void)vfs_unlink("/tmp/sys-dirfd/file.txt");
        (void)vfs_unlink("/tmp/sys-dirfd");
        return 0;
    }
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        (void)vfs_unlink("/tmp/sys-dirfd/file.txt");
        (void)vfs_unlink("/tmp/sys-dirfd");
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        (void)vfs_unlink("/tmp/sys-dirfd/file.txt");
        (void)vfs_unlink("/tmp/sys-dirfd");
        return 0;
    }
    vfs_fd = vfs_open("/tmp/sys-dirfd", VFS_O_RDONLY);
    if (vfs_fd >= 0) proc_fd = process_fd_install(vfs_fd);
    if (proc_fd < 0) {
        if (vfs_fd >= 0) vfs_close(vfs_fd);
        paging_destroy_user_directory(dir);
        (void)vfs_unlink("/tmp/sys-dirfd/file.txt");
        (void)vfs_unlink("/tmp/sys-dirfd");
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        vfs_dirent_t *ent = (vfs_dirent_t *)USER_BASE;
        char *cwd = (char *)(USER_BASE + 128U);
        ok = syscall_handler_c(SYS_FREADDIR, (uint32_t)proc_fd, 0, (uint32_t)ent, 0) == 0 &&
             streq(ent->name, "file.txt") &&
             syscall_handler_c(SYS_FREADDIR, 99, 0, (uint32_t)ent, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FREADDIR,
                               (uint32_t)proc_fd,
                               0,
                               USER_BASE + PAGE_SIZE,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FCHDIR, (uint32_t)proc_fd, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_GETCWD,
                               (uint32_t)cwd,
                               PROCESS_CWD_MAX,
                               0,
                               0) > 0 &&
             streq(cwd, "/tmp/sys-dirfd") &&
             syscall_handler_c(SYS_FCHDIR, 99, 0, 0, 0) == (uint32_t)-1;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    (void)process_fd_close(proc_fd);
    if (process_chdir(old_cwd) < 0) ok = 0;
    paging_destroy_user_directory(dir);
    (void)vfs_unlink("/tmp/sys-dirfd/file.txt");
    (void)vfs_unlink("/tmp/sys-dirfd");
    return ok && streq(process_cwd(), old_cwd);
}

static int test_syscall_at_paths(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    void *phys;
    int dir_vfs_fd = -1;
    int dir_proc_fd = -1;
    int fd = -1;
    int cwd_fd = -1;
    int ok = 0;
    (void)vfs_unlink("/tmp/atdir/file.txt");
    (void)vfs_unlink("/tmp/atdir/renamed.txt");
    (void)vfs_unlink("/tmp/atdir/cwd.txt");
    (void)vfs_rmdir("/tmp/atdir/sub");
    (void)vfs_rmdir("/tmp/atdir");
    if (vfs_mkdir("/tmp/atdir") < 0) {
        if (dir) paging_destroy_user_directory(dir);
        return 0;
    }
    if (!dir) {
        (void)vfs_rmdir("/tmp/atdir");
        return 0;
    }
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        (void)vfs_rmdir("/tmp/atdir");
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        (void)vfs_rmdir("/tmp/atdir");
        return 0;
    }
    dir_vfs_fd = vfs_open("/tmp/atdir", VFS_O_RDONLY);
    if (dir_vfs_fd >= 0) dir_proc_fd = process_fd_install(dir_vfs_fd);
    if (dir_proc_fd < 0) {
        if (dir_vfs_fd >= 0) vfs_close(dir_vfs_fd);
        paging_destroy_user_directory(dir);
        (void)vfs_rmdir("/tmp/atdir");
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *file = (char *)USER_BASE;
        char *renamed = (char *)(USER_BASE + 64U);
        char *subdir = (char *)(USER_BASE + 128U);
        char *payload = (char *)(USER_BASE + 192U);
        vfs_dirent_t *ent = (vfs_dirent_t *)(USER_BASE + 256U);
        char *abs_path = (char *)(USER_BASE + 384U);
        strcopy(file, "file.txt", 32);
        strcopy(renamed, "renamed.txt", 32);
        strcopy(subdir, "sub", 16);
        strcopy(payload, "at", 8);
        strcopy(abs_path, "/tmp/atdir/cwd.txt", 32);
        fd = (int)syscall_handler_c(SYS_OPENAT,
                                    (uint32_t)dir_proc_fd,
                                    (uint32_t)file,
                                    VFS_O_CREAT | VFS_O_RDWR,
                                    0);
        ok = fd >= 3;
        if (fd >= 3) {
            ok = ok &&
                 syscall_handler_c(SYS_WRITE, (uint32_t)fd, (uint32_t)payload, 2, 0) == 2 &&
                 syscall_handler_c(SYS_CLOSE, (uint32_t)fd, 0, 0, 0) == 0;
        }
        ok = ok &&
             syscall_handler_c(SYS_FSTATAT,
                               (uint32_t)dir_proc_fd,
                               (uint32_t)file,
                               (uint32_t)ent,
                               0) == 0 &&
             ent->size == 2 &&
             syscall_handler_c(SYS_ACCESSAT,
                               (uint32_t)dir_proc_fd,
                               (uint32_t)file,
                               SYS_ACCESS_READ,
                               0) == 0 &&
             syscall_handler_c(SYS_RENAMEAT,
                               (uint32_t)dir_proc_fd,
                               (uint32_t)file,
                               (uint32_t)dir_proc_fd,
                               (uint32_t)renamed) == 0 &&
             syscall_handler_c(SYS_FSTATAT,
                               (uint32_t)dir_proc_fd,
                               (uint32_t)renamed,
                               (uint32_t)ent,
                               0) == 0 &&
             ent->size == 2 &&
             syscall_handler_c(SYS_MKDIRAT,
                               (uint32_t)dir_proc_fd,
                               (uint32_t)subdir,
                               0,
                               0) == 0 &&
             syscall_handler_c(SYS_UNLINKAT,
                               (uint32_t)dir_proc_fd,
                               (uint32_t)renamed,
                               0,
                               0) == 0 &&
             syscall_handler_c(SYS_OPENAT,
                               99,
                               (uint32_t)file,
                               VFS_O_RDONLY,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FSTATAT,
                               (uint32_t)dir_proc_fd,
                               (uint32_t)file,
                               USER_BASE + PAGE_SIZE,
                               0) == (uint32_t)-1;
        cwd_fd = (int)syscall_handler_c(SYS_OPENAT,
                                        SYS_AT_FDCWD,
                                        (uint32_t)abs_path,
                                        VFS_O_CREAT | VFS_O_RDWR,
                                        0);
        ok = ok && cwd_fd >= 3;
        if (cwd_fd >= 3)
            ok = ok && syscall_handler_c(SYS_CLOSE, (uint32_t)cwd_fd, 0, 0, 0) == 0;
        ok = ok &&
             syscall_handler_c(SYS_UNLINKAT, SYS_AT_FDCWD, (uint32_t)abs_path, 0, 0) == 0;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    (void)process_fd_close(dir_proc_fd);
    paging_destroy_user_directory(dir);
    (void)vfs_unlink("/tmp/atdir/file.txt");
    (void)vfs_unlink("/tmp/atdir/renamed.txt");
    (void)vfs_unlink("/tmp/atdir/cwd.txt");
    (void)vfs_rmdir("/tmp/atdir/sub");
    (void)vfs_rmdir("/tmp/atdir");
    return ok;
}

static int test_syscall_spawn_args(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t pid = 0;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int code = 0;
    int ok = 0;
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        char *arg0 = (char *)(USER_BASE + 64U);
        char *arg1 = (char *)(USER_BASE + 128U);
        char *arg2 = (char *)(USER_BASE + 192U);
        const char **argv = (const char **)(USER_BASE + 256U);
        strcopy(path, "/bin/hello", 32);
        strcopy(arg0, "/bin/hello", 32);
        strcopy(arg1, "sys", 32);
        strcopy(arg2, "argv", 32);
        argv[0] = arg0;
        argv[1] = arg1;
        argv[2] = arg2;
        pid = syscall_handler_c(SYS_SPAWN_ARGS, (uint32_t)path, 3, (uint32_t)argv, 0);
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    if (pid != (uint32_t)-1) {
        const process_t *proc = process_find(pid);
        ok = proc &&
             proc->argc == 3 &&
             streq(proc->name, "/bin/hello") &&
             process_kill(pid, -9) == 0 &&
             process_wait(pid, &code) == 1 &&
             code == -9;
    }
    paging_destroy_user_directory(dir);
    return ok;
}

static int test_syscall_wait_any(void) {
    process_t *p = process_create_kernel("sys-wait-any", selftest_sleep_entry);
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    uint32_t pid;
    int ok = 0;
    if (!p || p->pid == 0) {
        if (dir) paging_destroy_user_directory(dir);
        return 0;
    }
    pid = p->pid;
    if (!dir) {
        (void)process_kill(pid, -9);
        return 0;
    }
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        (void)process_kill(pid, -9);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        (void)process_kill(pid, -9);
        return 0;
    }
    if (process_kill(pid, -9) < 0) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        int *code = (int *)USER_BASE;
        *code = 123;
        ok = syscall_handler_c(SYS_WAIT_ANY, (uint32_t)code, 0, 0, 0) == pid &&
             *code == -9 &&
             syscall_handler_c(SYS_WAIT_ANY, USER_BASE + PAGE_SIZE, 0, 0, 0) ==
                 (uint32_t)-1;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    if (!ok && process_find(pid)) {
        int code = 0;
        (void)process_wait(pid, &code);
    }
    return ok && process_find(pid) == 0;
}

static int test_syscall_kill_signal(void) {
    process_t *p = process_create_kernel("sys-kill-signal", selftest_sleep_entry);
    uint32_t pid;
    int code = 0;
    int ok;
    if (!p || p->pid == 0) return 0;
    pid = p->pid;
    ok = syscall_handler_c(SYS_KILL_SIGNAL, pid, 15, 0, 0) == 0 &&
         process_wait(pid, &code) == 1 &&
         code == -15 &&
         syscall_handler_c(SYS_KILL_SIGNAL, pid, 0, 0, 0) == (uint32_t)-1 &&
         syscall_handler_c(SYS_KILL_SIGNAL, pid, 32, 0, 0) == (uint32_t)-1;
    if (process_find(pid)) {
        (void)process_kill(pid, -9);
        (void)process_wait(pid, &code);
    }
    return ok;
}

static int test_syscall_process_groups(void) {
    uint32_t old_pid = task_current_process_id();
    process_t *leader = process_create_kernel("sys-pgrp-leader", selftest_sleep_entry);
    process_t *child;
    uint32_t leader_pid;
    uint32_t child_pid = 0;
    int leader_code = 0;
    int child_code = 0;
    int ok = 0;
    if (!leader || leader->pid == 0) return 0;
    leader_pid = leader->pid;
    task_set_current_process_id(leader_pid);
    child = process_create_kernel("sys-pgrp-child", selftest_sleep_entry);
    if (child && child->pid != 0) {
        child_pid = child->pid;
        ok = syscall_handler_c(SYS_GETPGID, child_pid, 0, 0, 0) == leader->pgid &&
             syscall_handler_c(SYS_GETSID, child_pid, 0, 0, 0) == leader->sid &&
             syscall_handler_c(SYS_SETPGID, child_pid, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_GETPGID, child_pid, 0, 0, 0) == child_pid &&
             syscall_handler_c(SYS_SETPGID, child_pid, leader->pgid, 0, 0) == 0 &&
             syscall_handler_c(SYS_GETPGID, child_pid, 0, 0, 0) == leader->pgid;
        task_set_current_process_id(child_pid);
        ok = ok &&
             syscall_handler_c(SYS_SETSID, 0, 0, 0, 0) == child_pid &&
             syscall_handler_c(SYS_GETPGID, 0, 0, 0, 0) == child_pid &&
             syscall_handler_c(SYS_GETSID, 0, 0, 0, 0) == child_pid &&
             syscall_handler_c(SYS_SETPGID, 0, leader->pgid, 0, 0) == (uint32_t)-1;
    }
    task_set_current_process_id(old_pid);
    if (child_pid && process_find(child_pid)) {
        (void)process_kill(child_pid, -9);
        (void)process_wait(child_pid, &child_code);
    }
    if (process_find(leader_pid)) {
        (void)process_kill(leader_pid, -9);
        (void)process_wait(leader_pid, &leader_code);
    }
    return ok && process_find(child_pid) == 0 && process_find(leader_pid) == 0;
}

static int test_syscall_kill_group(void) {
    uint32_t old_pid = task_current_process_id();
    process_t *leader = process_create_kernel("sys-killpg-leader", selftest_sleep_entry);
    process_t *child;
    uint32_t leader_pid;
    uint32_t child_pid = 0;
    int leader_code = 0;
    int child_code = 0;
    int ok = 0;
    if (!leader || leader->pid == 0) return 0;
    leader_pid = leader->pid;
    task_set_current_process_id(leader_pid);
    child = process_create_kernel("sys-killpg-child", selftest_sleep_entry);
    task_set_current_process_id(old_pid);
    if (child && child->pid != 0) {
        child_pid = child->pid;
        ok = child->pgid == leader->pgid &&
             syscall_handler_c(SYS_KILL_GROUP, leader->pgid, 15, 0, 0) == 0 &&
             process_wait(leader_pid, &leader_code) == 1 &&
             process_wait(child_pid, &child_code) == 1 &&
             leader_code == -15 &&
             child_code == -15 &&
             syscall_handler_c(SYS_KILL_GROUP, leader->pgid, 0, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_KILL_GROUP, leader->pgid, 32, 0, 0) == (uint32_t)-1;
    }
    if (child_pid && process_find(child_pid)) {
        (void)process_kill(child_pid, -9);
        (void)process_wait(child_pid, &child_code);
    }
    if (process_find(leader_pid)) {
        (void)process_kill(leader_pid, -9);
        (void)process_wait(leader_pid, &leader_code);
    }
    return ok && process_find(child_pid) == 0 && process_find(leader_pid) == 0;
}


static int test_syscall_fstat(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int fd = -1;
    int dup2_fd = -1;
    int ok = 0;
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        vfs_dirent_t *ent = (vfs_dirent_t *)(USER_BASE + 128U);
        strcopy(path, "/etc/hostname", 32);
        fd = (int)syscall_handler_c(SYS_OPEN, (uint32_t)path, VFS_O_RDONLY, 0, 0);
        if (fd >= 3)
            dup2_fd = (int)syscall_handler_c(SYS_DUP2,
                                             (uint32_t)fd,
                                             PROCESS_MAX_FDS - 1U,
                                             0,
                                             0);
        ok = fd >= 3 &&
             dup2_fd == PROCESS_MAX_FDS - 1 &&
             syscall_handler_c(SYS_FSTAT, (uint32_t)fd, (uint32_t)ent, 0, 0) == 0 &&
             streq(ent->name, "hostname") &&
             ent->type == VFS_NODE_FILE &&
             ent->size > 0 &&
             syscall_handler_c(SYS_FSTAT,
                               PROCESS_MAX_FDS - 1U,
                               (uint32_t)ent,
                               0,
                               0) == 0 &&
             streq(ent->name, "hostname") &&
             syscall_handler_c(SYS_DUP2, (uint32_t)fd, PROCESS_MAX_FDS, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FSTAT, (uint32_t)fd, USER_BASE + PAGE_SIZE, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FSTAT, 99, (uint32_t)ent, 0, 0) == (uint32_t)-1;
        if (dup2_fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)dup2_fd, 0, 0, 0);
        if (fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)fd, 0, 0, 0);
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    return ok;
}

static int test_syscall_mmap(void) {
    process_t *p = process_spawn_path("/bin/hello");
    uint32_t *old_dir = paging_current_directory();
    uint32_t old_pid = task_current_process_id();
    uint32_t old_as = task_current_address_space();
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    uint32_t addr;
    uint32_t flags;
    int code = 0;
    int ok = 0;
    if (!p || !p->page_dir) return 0;
    scheduler_set_preemption(0, quantum);
    task_set_current_process_id(p->pid);
    task_set_current_address_space(p->page_dir);
    paging_switch_directory((uint32_t *)p->page_dir);
    addr = syscall_handler_c(SYS_MMAP, PAGE_SIZE, PROCESS_MMAP_WRITE, 0, 0);
    flags = addr == (uint32_t)-1
        ? 0
        : paging_get_flags_in_directory((uint32_t *)p->page_dir, addr);
    ok = addr != (uint32_t)-1 &&
         addr >= USER_MMAP_BASE &&
         addr + PAGE_SIZE <= USER_MMAP_TOP &&
         (addr & (PAGE_SIZE - 1U)) == 0 &&
         (flags & (PAGE_PRESENT | PAGE_USER | PAGE_WRITE)) ==
             (PAGE_PRESENT | PAGE_USER | PAGE_WRITE) &&
         syscall_handler_c(SYS_MMAP, 0, PROCESS_MMAP_WRITE, 0, 0) == (uint32_t)-1 &&
         syscall_handler_c(SYS_MPROTECT, addr, PAGE_SIZE, 0, 0) == 0 &&
         (paging_get_flags_in_directory((uint32_t *)p->page_dir, addr) & PAGE_WRITE) == 0 &&
         syscall_handler_c(SYS_MPROTECT, addr, PAGE_SIZE, PROCESS_MMAP_WRITE, 0) == 0 &&
         (paging_get_flags_in_directory((uint32_t *)p->page_dir, addr) & PAGE_WRITE) != 0 &&
         syscall_handler_c(SYS_MPROTECT, addr + 1U, PAGE_SIZE, 0, 0) == (uint32_t)-1 &&
         syscall_handler_c(SYS_MPROTECT, USER_BASE, PAGE_SIZE, 0, 0) == (uint32_t)-1 &&
         syscall_handler_c(SYS_MUNMAP, addr + 1U, PAGE_SIZE, 0, 0) == (uint32_t)-1 &&
         syscall_handler_c(SYS_MUNMAP, USER_BASE, PAGE_SIZE, 0, 0) == (uint32_t)-1 &&
         syscall_handler_c(SYS_MUNMAP, addr, PAGE_SIZE, 0, 0) == 0 &&
         paging_get_phys_in_directory((uint32_t *)p->page_dir, addr) == 0 &&
         syscall_handler_c(SYS_MUNMAP, addr, PAGE_SIZE, 0, 0) == (uint32_t)-1;
    paging_switch_directory(old_dir);
    task_set_current_process_id(old_pid);
    task_set_current_address_space(old_as);
    scheduler_set_preemption(preempt, quantum);
    if (process_kill(p->pid, -9) < 0) return 0;
    if (process_wait(p->pid, &code) != 1) return 0;
    return ok && code == -9;
}

static int test_syscall_truncate(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int fd = -1;
    int ok = 0;
    (void)vfs_unlink("/tmp/sys-trunc.txt");
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        char *payload = (char *)(USER_BASE + 128U);
        vfs_dirent_t *ent = (vfs_dirent_t *)(USER_BASE + 256U);
        char *readback = (char *)(USER_BASE + 384U);
        strcopy(path, "/tmp/sys-trunc.txt", 32);
        strcopy(payload, "abcdef", 16);
        fd = (int)syscall_handler_c(SYS_OPEN,
                                    (uint32_t)path,
                                    VFS_O_CREAT | VFS_O_RDWR,
                                    0,
                                    0);
        ok = fd >= 3 &&
             syscall_handler_c(SYS_WRITE, (uint32_t)fd, (uint32_t)payload, 6, 0) == 6 &&
             syscall_handler_c(SYS_POLL,
                               (uint32_t)fd,
                               VFS_POLL_READ | VFS_POLL_WRITE,
                               0,
                               0) == (VFS_POLL_READ | VFS_POLL_WRITE) &&
             syscall_handler_c(SYS_POLL, (uint32_t)fd, 0x80000000U, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FTRUNCATE, (uint32_t)fd, 3, 0, 0) == 0 &&
             syscall_handler_c(SYS_FSTAT, (uint32_t)fd, (uint32_t)ent, 0, 0) == 0 &&
             ent->size == 3 &&
             syscall_handler_c(SYS_TRUNCATE, (uint32_t)path, 5, 0, 0) == 0 &&
             syscall_handler_c(SYS_FSTAT, (uint32_t)fd, (uint32_t)ent, 0, 0) == 0 &&
             ent->size == 5 &&
             syscall_handler_c(SYS_SEEK, (uint32_t)fd, 0, VFS_SEEK_SET, 0) == 0 &&
             syscall_handler_c(SYS_READ, (uint32_t)fd, (uint32_t)readback, 5, 0) == 5 &&
             memeq(readback, "abc", 3) &&
             readback[3] == 0 &&
             readback[4] == 0 &&
             syscall_handler_c(SYS_TRUNCATE, USER_BASE + PAGE_SIZE, 1, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FTRUNCATE, 99, 1, 0, 0) == (uint32_t)-1;
        if (fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)fd, 0, 0, 0);
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    (void)vfs_unlink("/tmp/sys-trunc.txt");
    return ok;
}

static int test_syscall_poll_many(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int fds[2];
    int pipe2_fds[2] = { -1, -1 };
    int read_vfs;
    int write_vfs;
    int ok = 0;
    if (process_pipe(fds) < 0) return 0;
    read_vfs = process_fd_resolve(fds[0]);
    write_vfs = process_fd_resolve(fds[1]);
    if (read_vfs < 0 || write_vfs < 0) {
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    if (!dir) {
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        (void)process_fd_close(fds[0]);
        (void)process_fd_close(fds[1]);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        sys_pollfd_t *polls = (sys_pollfd_t *)USER_BASE;
        int *user_pipe2_fds = (int *)(USER_BASE + 128U);
        polls[0].fd = fds[0];
        polls[0].events = VFS_POLL_READ;
        polls[0].revents = 0xFFFFFFFFU;
        polls[1].fd = fds[1];
        polls[1].events = VFS_POLL_WRITE;
        polls[1].revents = 0xFFFFFFFFU;
        ok = syscall_handler_c(SYS_POLL_MANY, (uint32_t)polls, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_POLL_MANY, (uint32_t)polls, 2, 0, 0) == 1 &&
             polls[0].revents == 0 &&
             polls[1].revents == VFS_POLL_WRITE &&
             vfs_write(write_vfs, "x", 1) == 1 &&
             syscall_handler_c(SYS_POLL_MANY, (uint32_t)polls, 2, 0, 0) == 2 &&
             polls[0].revents == VFS_POLL_READ &&
             polls[1].revents == VFS_POLL_WRITE;
        if (syscall_handler_c(SYS_PIPE2,
                              (uint32_t)user_pipe2_fds,
                              PROCESS_FD_CLOEXEC | VFS_O_NONBLOCK,
                              0,
                              0) == 0) {
            int p2_read_vfs;
            int p2_write_vfs;
            pipe2_fds[0] = user_pipe2_fds[0];
            pipe2_fds[1] = user_pipe2_fds[1];
            p2_read_vfs = process_fd_resolve(pipe2_fds[0]);
            p2_write_vfs = process_fd_resolve(pipe2_fds[1]);
            ok = ok &&
                 process_fd_get_flags(pipe2_fds[0]) == (int)PROCESS_FD_CLOEXEC &&
                 process_fd_get_flags(pipe2_fds[1]) == (int)PROCESS_FD_CLOEXEC &&
                 p2_read_vfs >= 0 &&
                 p2_write_vfs >= 0 &&
                 vfs_fd_flags(p2_read_vfs) == (VFS_O_RDONLY | VFS_O_NONBLOCK) &&
                 vfs_fd_flags(p2_write_vfs) == (VFS_O_WRONLY | VFS_O_NONBLOCK) &&
                 vfs_read(p2_read_vfs, (char *)(USER_BASE + 192U), 1) < 0;
        } else {
            ok = 0;
        }
        polls[0].events = 0x80000000U;
        ok = ok &&
             syscall_handler_c(SYS_POLL_MANY, (uint32_t)polls, 1, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_POLL_MANY, USER_BASE + PAGE_SIZE, 1, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_POLL_MANY,
                               (uint32_t)polls,
                               SYS_POLL_MAX + 1U,
                               0,
                               0) == (uint32_t)-1;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    if (pipe2_fds[0] >= 0 && process_fd_close(pipe2_fds[0]) != 0) ok = 0;
    if (pipe2_fds[1] >= 0 && process_fd_close(pipe2_fds[1]) != 0) ok = 0;
    if (process_fd_close(fds[0]) != 0) ok = 0;
    if (process_fd_close(fds[1]) != 0) ok = 0;
    return ok;
}

static int test_syscall_chmod_access(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int fd;
    int ok = 0;
    (void)vfs_unlink("/tmp/sys-mode.txt");
    fd = vfs_open("/tmp/sys-mode.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_write(fd, "mode", 4) != 4) {
        vfs_close(fd);
        (void)vfs_unlink("/tmp/sys-mode.txt");
        return 0;
    }
    vfs_close(fd);
    if (!dir) {
        (void)vfs_unlink("/tmp/sys-mode.txt");
        return 0;
    }
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        (void)vfs_unlink("/tmp/sys-mode.txt");
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        (void)vfs_unlink("/tmp/sys-mode.txt");
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        vfs_dirent_t *ent = (vfs_dirent_t *)(USER_BASE + 128U);
        strcopy(path, "/tmp/sys-mode.txt", 32);
        ok = syscall_handler_c(SYS_ACCESS,
                               (uint32_t)path,
                               VFS_ACCESS_READ | VFS_ACCESS_WRITE,
                               0,
                               0) == 0 &&
             syscall_handler_c(SYS_CHMOD, (uint32_t)path, 0000400U, 0, 0) == 0 &&
             syscall_handler_c(SYS_STAT, (uint32_t)path, (uint32_t)ent, 0, 0) == 0 &&
             (ent->mode & VFS_MODE_PERM_MASK) == 0000400U &&
             syscall_handler_c(SYS_ACCESS, (uint32_t)path, VFS_ACCESS_READ, 0, 0) == 0 &&
             syscall_handler_c(SYS_ACCESS, (uint32_t)path, VFS_ACCESS_WRITE, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_OPEN, (uint32_t)path, VFS_O_WRONLY, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_CHMOD, USER_BASE + PAGE_SIZE, 0000600U, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_ACCESS, (uint32_t)path, 0x80000000U, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_CHMOD, (uint32_t)path, 0000600U, 0, 0) == 0;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    (void)vfs_unlink("/tmp/sys-mode.txt");
    return ok;
}

static int test_syscall_fchmod(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int fd = -1;
    int ok = 0;
    (void)vfs_unlink("/tmp/sys-fchmod.txt");
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        vfs_dirent_t *ent = (vfs_dirent_t *)(USER_BASE + 128U);
        strcopy(path, "/tmp/sys-fchmod.txt", 32);
        fd = (int)syscall_handler_c(SYS_OPEN,
                                    (uint32_t)path,
                                    VFS_O_CREAT | VFS_O_RDWR,
                                    0,
                                    0);
        ok = fd >= 3 &&
             syscall_handler_c(SYS_FCHMOD, (uint32_t)fd, 0000400U, 0, 0) == 0 &&
             syscall_handler_c(SYS_FSTAT, (uint32_t)fd, (uint32_t)ent, 0, 0) == 0 &&
             (ent->mode & VFS_MODE_PERM_MASK) == 0000400U &&
             syscall_handler_c(SYS_FCHMOD, (uint32_t)fd, 0001000U, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FCHMOD, 99, 0000600U, 0, 0) == (uint32_t)-1;
        if (fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)fd, 0, 0, 0);
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    fd = vfs_open("/tmp/sys-fchmod.txt", VFS_O_WRONLY);
    if (fd >= 0) {
        vfs_close(fd);
        ok = 0;
    }
    (void)vfs_unlink("/tmp/sys-fchmod.txt");
    return ok;
}

static int test_syscall_credentials(void) {
    process_t *cur = process_current();
    uint32_t old_uid = process_uid_get(cur);
    uint32_t old_gid = process_gid_get(cur);
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int fd = -1;
    int ok = 0;
    if (!cur) return 0;
    (void)process_uid_set(cur, 0);
    (void)process_gid_set(cur, 0);
    (void)vfs_unlink("/tmp/sys-own.txt");
    if (!dir) goto done;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        goto done;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        goto done;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        vfs_dirent_t *ent = (vfs_dirent_t *)(USER_BASE + 128U);
        sys_process_info_t *info = (sys_process_info_t *)(USER_BASE + 256U);
        strcopy(path, "/tmp/sys-own.txt", 32);
        fd = (int)syscall_handler_c(SYS_OPEN,
                                    (uint32_t)path,
                                    VFS_O_CREAT | VFS_O_RDWR,
                                    0,
                                    0);
        ok = fd >= 3 &&
             syscall_handler_c(SYS_GETUID, 0, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_GETGID, 0, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_FCHOWN, (uint32_t)fd, 44, 8, 0) == 0 &&
             syscall_handler_c(SYS_FSTAT, (uint32_t)fd, (uint32_t)ent, 0, 0) == 0 &&
             ent->uid == 44 &&
             ent->gid == 8 &&
             syscall_handler_c(SYS_CHOWN, (uint32_t)path, 45, 9, 0) == 0 &&
             syscall_handler_c(SYS_STAT, (uint32_t)path, (uint32_t)ent, 0, 0) == 0 &&
             ent->uid == 45 &&
             ent->gid == 9 &&
             syscall_handler_c(SYS_SETGID, 9, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_SETUID, 46, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_GETUID, 0, 0, 0, 0) == 46 &&
             syscall_handler_c(SYS_GETGID, 0, 0, 0, 0) == 9 &&
             syscall_handler_c(SYS_CHOWN, (uint32_t)path, 0, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_PROCESS_INFO, 0, (uint32_t)info, 0, 0) == 0 &&
             info->uid == 46 &&
             info->gid == 9 &&
             info->pgid == 0 &&
             info->sid == 0 &&
             syscall_handler_c(SYS_SETUID, 0, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_SETGID, 0, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_CHOWN, USER_BASE + PAGE_SIZE, 0, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FCHOWN, 99, 0, 0, 0) == (uint32_t)-1;
        if (fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)fd, 0, 0, 0);
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
done:
    (void)process_uid_set(cur, 0);
    (void)process_gid_set(cur, 0);
    (void)vfs_unlink("/tmp/sys-own.txt");
    (void)process_uid_set(cur, old_uid);
    (void)process_gid_set(cur, old_gid);
    return ok;
}

static int test_syscall_symlink_readlink(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int fd;
    int ok = 0;
    (void)vfs_unlink("/tmp/sys-symlink-link.txt");
    (void)vfs_unlink("/tmp/sys-symlink-target.txt");
    fd = vfs_open("/tmp/sys-symlink-target.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_write(fd, "syslink", 7) != 7) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    if (!dir) {
        (void)vfs_unlink("/tmp/sys-symlink-target.txt");
        return 0;
    }
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        (void)vfs_unlink("/tmp/sys-symlink-target.txt");
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        (void)vfs_unlink("/tmp/sys-symlink-target.txt");
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *target = (char *)USER_BASE;
        char *link_path = (char *)(USER_BASE + 128U);
        char *out = (char *)(USER_BASE + 256U);
        char *data = (char *)(USER_BASE + 384U);
        vfs_dirent_t *ent = (vfs_dirent_t *)(USER_BASE + 512U);
        int proc_fd;
        strcopy(target, "/tmp/sys-symlink-target.txt", 64);
        strcopy(link_path, "/tmp/sys-symlink-link.txt", 64);
        ok = syscall_handler_c(SYS_SYMLINK,
                               (uint32_t)target,
                               (uint32_t)link_path,
                               0,
                               0) == 0 &&
             syscall_handler_c(SYS_READLINK,
                               (uint32_t)link_path,
                               (uint32_t)out,
                               VFS_MAX_PATH,
                               0) == 27 &&
             streq(out, "/tmp/sys-symlink-target.txt") &&
             syscall_handler_c(SYS_LSTAT,
                               (uint32_t)link_path,
                               (uint32_t)ent,
                               0,
                               0) == 0 &&
             ent->type == VFS_NODE_SYMLINK &&
             ent->size == 27 &&
             syscall_handler_c(SYS_READLINK,
                               (uint32_t)link_path,
                               (uint32_t)out,
                               4,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_READLINK,
                               USER_BASE + PAGE_SIZE,
                               (uint32_t)out,
                               VFS_MAX_PATH,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_READLINK,
                               (uint32_t)link_path,
                               USER_BASE + PAGE_SIZE,
                               VFS_MAX_PATH,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_LSTAT,
                               (uint32_t)link_path,
                               USER_BASE + PAGE_SIZE,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_SYMLINK,
                               USER_BASE + PAGE_SIZE,
                               (uint32_t)link_path,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_SYMLINK,
                               (uint32_t)target,
                               USER_BASE + PAGE_SIZE,
                               0,
                               0) == (uint32_t)-1;
        proc_fd = (int)syscall_handler_c(SYS_OPEN,
                                         (uint32_t)link_path,
                                         VFS_O_RDONLY,
                                         0,
                                         0);
        ok = ok &&
             proc_fd >= 3 &&
             syscall_handler_c(SYS_READ, (uint32_t)proc_fd, (uint32_t)data, 7, 0) == 7 &&
             memeq(data, "syslink", 7);
        if (proc_fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)proc_fd, 0, 0, 0);
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    (void)vfs_unlink("/tmp/sys-symlink-link.txt");
    (void)vfs_unlink("/tmp/sys-symlink-target.txt");
    return ok;
}

static int test_syscall_pread_pwrite(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int ok = 0;
    (void)vfs_unlink("/tmp/sys-pread.txt");
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        char *buf = (char *)(USER_BASE + 128U);
        int fd;
        strcopy(path, "/tmp/sys-pread.txt", 32);
        strcopy(buf, "abcdef", 32);
        fd = (int)syscall_handler_c(SYS_OPEN,
                                    (uint32_t)path,
                                    VFS_O_CREAT | VFS_O_RDWR,
                                    0,
                                    0);
        ok = fd >= 3 &&
             syscall_handler_c(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, 6, 0) == 6 &&
             syscall_handler_c(SYS_SEEK, (uint32_t)fd, 1, VFS_SEEK_SET, 0) == 1 &&
             syscall_handler_c(SYS_PREAD, (uint32_t)fd, (uint32_t)buf, 3, 2) == 3 &&
             memeq(buf, "cde", 3) &&
             syscall_handler_c(SYS_SEEK, (uint32_t)fd, 0, VFS_SEEK_CUR, 0) == 1;
        strcopy(buf, "XY", 32);
        ok = ok &&
             syscall_handler_c(SYS_PWRITE, (uint32_t)fd, (uint32_t)buf, 2, 3) == 2 &&
             syscall_handler_c(SYS_SEEK, (uint32_t)fd, 0, VFS_SEEK_CUR, 0) == 1 &&
             syscall_handler_c(SYS_SEEK, (uint32_t)fd, 0, VFS_SEEK_SET, 0) == 0 &&
             syscall_handler_c(SYS_READ, (uint32_t)fd, (uint32_t)buf, 6, 0) == 6 &&
             memeq(buf, "abcXYf", 6) &&
             syscall_handler_c(SYS_PREAD,
                               (uint32_t)fd,
                               USER_BASE + PAGE_SIZE,
                               1,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_PWRITE,
                               (uint32_t)fd,
                               USER_BASE + PAGE_SIZE,
                               1,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_PREAD, 99, (uint32_t)buf, 1, 0) == (uint32_t)-1;
        if (fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)fd, 0, 0, 0);
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    (void)vfs_unlink("/tmp/sys-pread.txt");
    return ok;
}

static int test_syscall_readv_writev(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int ok = 0;
    (void)vfs_unlink("/tmp/sys-iov.txt");
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        sys_iovec_t *iov = (sys_iovec_t *)(USER_BASE + 128U);
        char *part0 = (char *)(USER_BASE + 256U);
        char *part1 = (char *)(USER_BASE + 320U);
        char *part2 = (char *)(USER_BASE + 384U);
        int fd;
        strcopy(path, "/tmp/sys-iov.txt", 32);
        strcopy(part0, "ab", 16);
        strcopy(part1, "cd", 16);
        strcopy(part2, "ef", 16);
        fd = (int)syscall_handler_c(SYS_OPEN,
                                    (uint32_t)path,
                                    VFS_O_CREAT | VFS_O_RDWR,
                                    0,
                                    0);
        iov[0].base = part0;
        iov[0].len = 2;
        iov[1].base = part1;
        iov[1].len = 2;
        iov[2].base = part2;
        iov[2].len = 2;
        ok = fd >= 3 &&
             syscall_handler_c(SYS_WRITEV, (uint32_t)fd, (uint32_t)iov, 3, 0) == 6 &&
             syscall_handler_c(SYS_SEEK, (uint32_t)fd, 0, VFS_SEEK_SET, 0) == 0;
        part0[0] = 0;
        part1[0] = 0;
        iov[0].base = part0;
        iov[0].len = 2;
        iov[1].base = part1;
        iov[1].len = 4;
        ok = ok &&
             syscall_handler_c(SYS_READV, (uint32_t)fd, (uint32_t)iov, 2, 0) == 6 &&
             memeq(part0, "ab", 2) &&
             memeq(part1, "cdef", 4) &&
             syscall_handler_c(SYS_WRITEV,
                               (uint32_t)fd,
                               USER_BASE + PAGE_SIZE,
                               1,
                               0) == (uint32_t)-1;
        iov[0].base = (void *)(USER_BASE + PAGE_SIZE);
        iov[0].len = 1;
        ok = ok &&
             syscall_handler_c(SYS_READV, (uint32_t)fd, (uint32_t)iov, 1, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_READV,
                               (uint32_t)fd,
                               (uint32_t)iov,
                               SYS_IOV_MAX + 1U,
                               0) == (uint32_t)-1;
        if (fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)fd, 0, 0, 0);
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    (void)vfs_unlink("/tmp/sys-iov.txt");
    return ok;
}

static int test_syscall_fcntl(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int ok = 0;
    (void)vfs_unlink("/tmp/sys-fcntl.txt");
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        char *buf = (char *)(USER_BASE + 128U);
        int fd;
        int dup_fd;
        int clo_fd;
        int nb_fd;
        int flags;
        int updated_flags;
        int nb_flags;
        int fd_flags0;
        int fd_flags1;
        int dup_fd_flags;
        int clo_fd_flags;
        strcopy(path, "/tmp/sys-fcntl.txt", 32);
        fd = (int)syscall_handler_c(SYS_OPEN,
                                    (uint32_t)path,
                                    VFS_O_CREAT | VFS_O_RDWR,
                                    0,
                                    0);
        flags = (int)syscall_handler_c(SYS_FCNTL,
                                       (uint32_t)fd,
                                       SYS_FCNTL_GETFL,
                                       0,
                                       0);
        fd_flags0 = (int)syscall_handler_c(SYS_FCNTL,
                                           (uint32_t)fd,
                                           SYS_FCNTL_GETFD,
                                           0,
                                           0);
        if (fd >= 0) {
            (void)syscall_handler_c(SYS_FCNTL,
                                    (uint32_t)fd,
                                    SYS_FCNTL_SETFD,
                                    SYS_FD_CLOEXEC,
                                    0);
        }
        fd_flags1 = (int)syscall_handler_c(SYS_FCNTL,
                                           (uint32_t)fd,
                                           SYS_FCNTL_GETFD,
                                           0,
                                           0);
        dup_fd = (int)syscall_handler_c(SYS_FCNTL,
                                        (uint32_t)fd,
                                        SYS_FCNTL_DUPFD,
                                        4,
                                        0);
        dup_fd_flags = (int)syscall_handler_c(SYS_FCNTL,
                                              (uint32_t)dup_fd,
                                              SYS_FCNTL_GETFD,
                                              0,
                                              0);
        clo_fd = (int)syscall_handler_c(SYS_OPEN,
                                        (uint32_t)path,
                                        VFS_O_RDWR | VFS_O_CLOEXEC,
                                        0,
                                        0);
        clo_fd_flags = (int)syscall_handler_c(SYS_FCNTL,
                                              (uint32_t)clo_fd,
                                              SYS_FCNTL_GETFD,
                                              0,
                                              0);
        nb_fd = (int)syscall_handler_c(SYS_OPEN,
                                       (uint32_t)path,
                                       VFS_O_RDWR | VFS_O_NONBLOCK,
                                       0,
                                       0);
        nb_flags = (int)syscall_handler_c(SYS_FCNTL,
                                          (uint32_t)nb_fd,
                                          SYS_FCNTL_GETFL,
                                          0,
                                          0);
        ok = fd >= 3 &&
             flags == VFS_O_RDWR &&
             fd_flags0 == 0 &&
             fd_flags1 == (int)SYS_FD_CLOEXEC &&
             dup_fd >= 4 &&
             dup_fd_flags == 0 &&
             clo_fd >= 3 &&
             clo_fd_flags == (int)SYS_FD_CLOEXEC &&
             nb_fd >= 3 &&
             nb_flags == (VFS_O_RDWR | VFS_O_NONBLOCK) &&
             syscall_handler_c(SYS_FCNTL,
                               (uint32_t)fd,
                               SYS_FCNTL_SETFL,
                               (uint32_t)(flags | VFS_O_APPEND | VFS_O_NONBLOCK),
                               0) == 0;
        updated_flags = (int)syscall_handler_c(SYS_FCNTL,
                                               (uint32_t)fd,
                                               SYS_FCNTL_GETFL,
                                               0,
                                               0);
        ok = ok &&
             updated_flags == (VFS_O_RDWR | VFS_O_APPEND | VFS_O_NONBLOCK) &&
             syscall_handler_c(SYS_FCNTL,
                               (uint32_t)fd,
                               SYS_FCNTL_SETFD,
                               SYS_FD_CLOEXEC | 0x80000000U,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FCNTL,
                               (uint32_t)fd,
                               SYS_FCNTL_SETFL,
                               VFS_O_APPEND,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FCNTL, 99, SYS_FCNTL_GETFL, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_FCNTL, (uint32_t)fd, 99, 0, 0) == (uint32_t)-1;
        strcopy(buf, "a", 8);
        ok = ok &&
             syscall_handler_c(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, 1, 0) == 1 &&
             syscall_handler_c(SYS_SEEK, (uint32_t)fd, 0, VFS_SEEK_SET, 0) == 0;
        strcopy(buf, "b", 8);
        ok = ok &&
             syscall_handler_c(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, 1, 0) == 1 &&
             syscall_handler_c(SYS_SEEK, (uint32_t)fd, 0, VFS_SEEK_SET, 0) == 0 &&
             syscall_handler_c(SYS_READ, (uint32_t)fd, (uint32_t)buf, 2, 0) == 2 &&
             memeq(buf, "ab", 2);
        if (clo_fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)clo_fd, 0, 0, 0);
        if (nb_fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)nb_fd, 0, 0, 0);
        if (dup_fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)dup_fd, 0, 0, 0);
        if (fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)fd, 0, 0, 0);
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    (void)vfs_unlink("/tmp/sys-fcntl.txt");
    return ok;
}

static int test_syscall_utime_futime(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int ok = 0;
    (void)vfs_unlink("/tmp/sys-utime.txt");
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        vfs_dirent_t *ent = (vfs_dirent_t *)(USER_BASE + 128U);
        int fd;
        strcopy(path, "/tmp/sys-utime.txt", 32);
        fd = (int)syscall_handler_c(SYS_OPEN,
                                    (uint32_t)path,
                                    VFS_O_CREAT | VFS_O_RDWR,
                                    0,
                                    0);
        ok = fd >= 3 &&
             syscall_handler_c(SYS_UTIME, (uint32_t)path, 111U, 222U, 0) == 0 &&
             syscall_handler_c(SYS_STAT, (uint32_t)path, (uint32_t)ent, 0, 0) == 0 &&
             ent->accessed_ms == 111U &&
             ent->modified_ms == 222U &&
             syscall_handler_c(SYS_FUTIME, (uint32_t)fd, 333U, 444U, 0) == 0 &&
             syscall_handler_c(SYS_FSTAT, (uint32_t)fd, (uint32_t)ent, 0, 0) == 0 &&
             ent->accessed_ms == 333U &&
             ent->modified_ms == 444U &&
             syscall_handler_c(SYS_UTIME, USER_BASE + PAGE_SIZE, 1U, 2U, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_FUTIME, 99, 1U, 2U, 0) == (uint32_t)-1;
        if (fd >= 0) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)fd, 0, 0, 0);
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    (void)vfs_unlink("/tmp/sys-utime.txt");
    return ok;
}

static int test_syscall_sync(void) {
    int vfs_fd;
    int proc_fd;
    int proc_vfs_fd;
    int proc_proc_fd;
    int pipe_fds[2] = { -1, -1 };
    int pipe_proc_fd = -1;
    int ok;
    (void)vfs_unlink("/tmp/sys-sync.txt");
    vfs_fd = vfs_open("/tmp/sys-sync.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (vfs_fd < 0) return 0;
    if (vfs_write(vfs_fd, "sync", 4) != 4) {
        vfs_close(vfs_fd);
        (void)vfs_unlink("/tmp/sys-sync.txt");
        return 0;
    }
    proc_fd = process_fd_install(vfs_fd);
    if (proc_fd < 0) {
        vfs_close(vfs_fd);
        (void)vfs_unlink("/tmp/sys-sync.txt");
        return 0;
    }
    ok = syscall_handler_c(SYS_FSYNC, (uint32_t)proc_fd, 0, 0, 0) == 0 &&
         syscall_handler_c(SYS_FDATASYNC, (uint32_t)proc_fd, 0, 0, 0) == 0 &&
         syscall_handler_c(SYS_SYNC, 0, 0, 0, 0) == 0 &&
         syscall_handler_c(SYS_FSYNC, 99, 0, 0, 0) == (uint32_t)-1 &&
         syscall_handler_c(SYS_FDATASYNC, 99, 0, 0, 0) == (uint32_t)-1;
    proc_vfs_fd = vfs_open("/proc/meminfo", VFS_O_RDONLY);
    if (proc_vfs_fd >= 0) {
        proc_proc_fd = process_fd_install(proc_vfs_fd);
        if (proc_proc_fd >= 0) {
            ok = ok &&
                 syscall_handler_c(SYS_FSYNC, (uint32_t)proc_proc_fd, 0, 0, 0) == (uint32_t)-1;
            (void)process_fd_close(proc_proc_fd);
        } else {
            vfs_close(proc_vfs_fd);
            ok = 0;
        }
    } else {
        ok = 0;
    }
    if (vfs_pipe(pipe_fds) == 0) {
        pipe_proc_fd = process_fd_install(pipe_fds[0]);
        if (pipe_proc_fd >= 0) {
            ok = ok &&
                 syscall_handler_c(SYS_FSYNC, (uint32_t)pipe_proc_fd, 0, 0, 0) == (uint32_t)-1;
            (void)process_fd_close(pipe_proc_fd);
            vfs_close(pipe_fds[1]);
        } else {
            vfs_close(pipe_fds[0]);
            vfs_close(pipe_fds[1]);
            ok = 0;
        }
    } else {
        ok = 0;
    }
    (void)process_fd_close(proc_fd);
    (void)vfs_unlink("/tmp/sys-sync.txt");
    return ok;
}

static int test_syscall_env(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int ok = 0;
    (void)process_env_unset(process_current(), "SYSENV");
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *name = (char *)USER_BASE;
        char *value = (char *)(USER_BASE + 64U);
        char *out = (char *)(USER_BASE + 128U);
        char *entry = (char *)(USER_BASE + 256U);
        char *bad_name = (char *)(USER_BASE + 384U);
        strcopy(name, "SYSENV", 32);
        strcopy(value, "alpha", 32);
        strcopy(bad_name, "BAD-NAME", 32);
        ok = syscall_handler_c(SYS_SETENV, (uint32_t)name, (uint32_t)value, 1, 0) == 0 &&
             syscall_handler_c(SYS_GETENV,
                               (uint32_t)name,
                               (uint32_t)out,
                               PROCESS_ENV_MAX,
                               0) == 5 &&
             streq(out, "alpha") &&
             syscall_handler_c(SYS_SETENV, (uint32_t)name, (uint32_t)value, 0, 0) == 0 &&
             syscall_handler_c(SYS_GETENV, (uint32_t)name, (uint32_t)out, 5, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_ENV_COUNT, 0, 0, 0, 0) >= 1 &&
             syscall_handler_c(SYS_ENV_INFO,
                               0,
                               (uint32_t)entry,
                               PROCESS_ENV_MAX,
                               0) > 0 &&
             contains_text(entry, "=") &&
             syscall_handler_c(SYS_SETENV, (uint32_t)bad_name, (uint32_t)value, 1, 0) ==
                 (uint32_t)-1;
        strcopy(value, "beta", 32);
        ok = ok &&
             syscall_handler_c(SYS_SETENV, (uint32_t)name, (uint32_t)value, 1, 0) == 0 &&
             syscall_handler_c(SYS_GETENV,
                               (uint32_t)name,
                               (uint32_t)out,
                               PROCESS_ENV_MAX,
                               0) == 4 &&
             streq(out, "beta") &&
             syscall_handler_c(SYS_ENV_INFO, 0, USER_BASE + PAGE_SIZE, PROCESS_ENV_MAX, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_UNSETENV, (uint32_t)name, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_GETENV,
                               (uint32_t)name,
                               (uint32_t)out,
                               PROCESS_ENV_MAX,
                               0) == (uint32_t)-1;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    (void)process_env_unset(process_current(), "SYSENV");
    return ok;
}

static int test_syscall_hostname(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    char old_name[UTS_FIELD_MAX];
    int ok = 0;
    if (uts_copy_nodename(old_name, sizeof(old_name)) < 0) return 0;
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *name = (char *)USER_BASE;
        char *out = (char *)(USER_BASE + 64U);
        char *bad = (char *)(USER_BASE + 128U);
        sys_utsname_t *uts = (sys_utsname_t *)(USER_BASE + 192U);
        strcopy(name, "node-test", 32);
        strcopy(bad, "bad/name", 32);
        ok = syscall_handler_c(SYS_GETHOSTNAME,
                               (uint32_t)out,
                               UTS_FIELD_MAX,
                               0,
                               0) > 0 &&
             streq(out, old_name) &&
             syscall_handler_c(SYS_SETHOSTNAME, (uint32_t)name, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_GETHOSTNAME,
                               (uint32_t)out,
                               UTS_FIELD_MAX,
                               0,
                               0) == 9 &&
             streq(out, "node-test") &&
             syscall_handler_c(SYS_UNAME, (uint32_t)uts, 0, 0, 0) == 0 &&
             streq(uts->nodename, "node-test") &&
             syscall_handler_c(SYS_GETHOSTNAME, (uint32_t)out, 4, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_GETHOSTNAME, USER_BASE + PAGE_SIZE, UTS_FIELD_MAX, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_SETHOSTNAME, (uint32_t)bad, 0, 0, 0) ==
                 (uint32_t)-1 &&
             streq(uts_nodename(), "node-test");
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    if (uts_set_nodename(old_name) < 0) ok = 0;
    (void)process_env_set(process_current(), "HOSTNAME", uts_nodename(), 1);
    return ok && streq(uts_nodename(), old_name);
}

static int test_syscall_umask(void) {
    process_t *proc = process_current();
    uint32_t old_mask = process_umask_get(proc);
    int ret;
    int ok;
    ret = (int)syscall_handler_c(SYS_UMASK, 0000077U, 0, 0, 0);
    ok = ret == (int)old_mask &&
         process_umask_get(proc) == 0000077U &&
         syscall_handler_c(SYS_UMASK, 0001000U, 0, 0, 0) == (uint32_t)-1 &&
         process_umask_get(proc) == 0000077U;
    if (process_umask_set(proc, old_mask) < 0) ok = 0;
    return ok && process_umask_get(proc) == old_mask;
}

static int test_syscall_rmdir(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int fd;
    int ok = 0;
    (void)vfs_unlink("/tmp/sys-rmdir-file.txt");
    (void)vfs_unlink("/tmp/sys-rmdir-dir");
    fd = vfs_open("/tmp/sys-rmdir-file.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    vfs_close(fd);
    if (!dir) {
        (void)vfs_unlink("/tmp/sys-rmdir-file.txt");
        return 0;
    }
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        (void)vfs_unlink("/tmp/sys-rmdir-file.txt");
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        (void)vfs_unlink("/tmp/sys-rmdir-file.txt");
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *dir_path = (char *)USER_BASE;
        char *file_path = (char *)(USER_BASE + 128U);
        strcopy(dir_path, "/tmp/sys-rmdir-dir", 32);
        strcopy(file_path, "/tmp/sys-rmdir-file.txt", 32);
        ok = syscall_handler_c(SYS_MKDIR, (uint32_t)dir_path, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_RMDIR, (uint32_t)file_path, 0, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_RMDIR, (uint32_t)dir_path, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_RMDIR, USER_BASE + PAGE_SIZE, 0, 0, 0) == (uint32_t)-1;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    (void)vfs_unlink("/tmp/sys-rmdir-file.txt");
    (void)vfs_unlink("/tmp/sys-rmdir-dir");
    return ok;
}

static int test_syscall_task_priority(void) {
    int id = task_create("prio-sys", selftest_sleep_entry);
    int ok;
    if (id < 0) return 0;
    ok = syscall_handler_c(SYS_TASK_PRIORITY, (uint32_t)id, 0, 0, 0) ==
             TASK_PRIORITY_DEFAULT &&
         syscall_handler_c(SYS_TASK_SET_PRIORITY, (uint32_t)id, 19, 0, 0) == 0 &&
         syscall_handler_c(SYS_TASK_PRIORITY, (uint32_t)id, 0, 0, 0) == 19 &&
         task_priority(id) == 19 &&
         syscall_handler_c(SYS_TASK_SET_PRIORITY,
                           (uint32_t)id,
                           TASK_PRIORITY_MAX + 1U,
                           0,
                           0) == (uint32_t)-1 &&
         syscall_handler_c(SYS_TASK_PRIORITY, MAX_TASKS, 0, 0, 0) == (uint32_t)-1;
    if (task_destroy(id) < 0) return 0;
    return ok;
}

static int test_syscall_statfs(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int ok = 0;
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        char *path = (char *)USER_BASE;
        sys_fsinfo_t *fs = (sys_fsinfo_t *)(USER_BASE + 128U);
        strcopy(path, "/", 8);
        ok = syscall_handler_c(SYS_STATFS, (uint32_t)path, (uint32_t)fs, 0, 0) == 0 &&
             streq(fs->name, "ramfs") &&
             fs->total_files > 0 &&
             syscall_handler_c(SYS_STATFS, (uint32_t)path, USER_BASE + PAGE_SIZE, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_STATFS, USER_BASE + PAGE_SIZE, (uint32_t)fs, 0, 0) ==
                 (uint32_t)-1;
        strcopy(path, "/disk", 8);
        ok = ok &&
             syscall_handler_c(SYS_STATFS, (uint32_t)path, (uint32_t)fs, 0, 0) == 0 &&
             streq(fs->name, "simplefs") &&
             fs->block_size == BLOCK_SECTOR_SIZE;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    return ok;
}

static int test_syscall_tty_mode(void) {
    uint32_t old_mode = tty_get_mode();
    int vfs_fd = vfs_open("/dev/tty0", VFS_O_RDWR);
    int proc_fd;
    int ok = syscall_handler_c(SYS_TTY_GET_MODE, 0, 0, 0, 0) == old_mode &&
             syscall_handler_c(SYS_TTY_SET_MODE, 0, 0, 0, 0) == 0 &&
             tty_get_mode() == 0 &&
             syscall_handler_c(SYS_TTY_GET_MODE, 0, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_TTY_SET_MODE,
                               TTY_MODE_ECHO | TTY_MODE_CANON,
                               0,
                               0,
                               0) == 0 &&
             tty_get_mode() == (TTY_MODE_ECHO | TTY_MODE_CANON) &&
             syscall_handler_c(SYS_TTY_SET_MODE, 0x80000000U, 0, 0, 0) == (uint32_t)-1;
    if (vfs_fd < 0) {
        (void)tty_set_mode(old_mode);
        return 0;
    }
    proc_fd = process_fd_install(vfs_fd);
    if (proc_fd < 0) {
        vfs_close(vfs_fd);
        (void)tty_set_mode(old_mode);
        return 0;
    }
    ok = ok &&
         syscall_handler_c(SYS_IOCTL,
                           (uint32_t)proc_fd,
                           VFS_IOCTL_TTY_GET_SIZE,
                           0,
                           0) == ((TTY_ROWS << 16) | TTY_COLUMNS) &&
         syscall_handler_c(SYS_IOCTL,
                           (uint32_t)proc_fd,
                           VFS_IOCTL_TTY_SET_MODE,
                           0,
                           0) == 0 &&
         tty_get_mode() == 0 &&
         syscall_handler_c(SYS_IOCTL,
                           (uint32_t)proc_fd,
                           VFS_IOCTL_TTY_GET_MODE,
                           0,
                           0) == 0 &&
         syscall_handler_c(SYS_IOCTL,
                           (uint32_t)proc_fd,
                           0xFFFFFFFFU,
                           0,
                           0) == (uint32_t)-1;
    (void)process_fd_close(proc_fd);
    if (tty_set_mode(old_mode) < 0) return 0;
    return ok;
}

static int test_syscall_clock(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    void *phys;
    int ok = 0;
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        sys_timespec_t *ts = (sys_timespec_t *)USER_BASE;
        sys_timeval_t *tv = (sys_timeval_t *)(USER_BASE + 16U);
        sys_timespec_t *req = (sys_timespec_t *)(USER_BASE + 32U);
        sys_timespec_t *rem = (sys_timespec_t *)(USER_BASE + 48U);
        uint32_t *time_out = (uint32_t *)(USER_BASE + 64U);
        uint32_t before = timer_ms();
        uint32_t ms_ret = syscall_handler_c(SYS_CLOCK_MS, 0, 0, 0, 0);
        uint32_t after;
        uint32_t ts_ms;
        uint32_t sec_ret;
        req->sec = 0;
        req->nsec = 0;
        rem->sec = 77;
        rem->nsec = 88;
        ok = ms_ret >= before &&
             syscall_handler_c(SYS_CLOCK_GETTIME,
                               SYS_CLOCK_UPTIME_MONOTONIC,
                               (uint32_t)ts,
                               0,
                               0) == 0 &&
             ts->nsec < 1000000000U &&
             (ts->nsec % 1000000U) == 0 &&
             syscall_handler_c(SYS_CLOCK_GETTIME, 99, (uint32_t)ts, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_CLOCK_GETTIME,
                               SYS_CLOCK_UPTIME_MONOTONIC,
                               USER_BASE + PAGE_SIZE,
                               0,
                               0) == (uint32_t)-1;
        sec_ret = syscall_handler_c(SYS_TIME, (uint32_t)time_out, 0, 0, 0);
        ok = ok &&
             sec_ret == *time_out &&
             *time_out >= before / 1000U &&
             syscall_handler_c(SYS_TIME, 0, 0, 0, 0) >= *time_out &&
             syscall_handler_c(SYS_TIME, USER_BASE + PAGE_SIZE, 0, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_GETTIMEOFDAY, (uint32_t)tv, 0, 0, 0) == 0 &&
             tv->usec < 1000000U &&
             syscall_handler_c(SYS_GETTIMEOFDAY, USER_BASE + PAGE_SIZE, 0, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_NANOSLEEP, (uint32_t)req, (uint32_t)rem, 0, 0) == 0 &&
             rem->sec == 0 &&
             rem->nsec == 0;
        req->nsec = 1000000000U;
        ok = ok &&
             syscall_handler_c(SYS_NANOSLEEP, (uint32_t)req, 0, 0, 0) == (uint32_t)-1;
        req->nsec = 0;
        ok = ok &&
             syscall_handler_c(SYS_NANOSLEEP, USER_BASE + PAGE_SIZE, 0, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_NANOSLEEP, (uint32_t)req, USER_BASE + PAGE_SIZE, 0, 0) ==
                 (uint32_t)-1;
        after = timer_ms();
        ts_ms = ts->sec * 1000U + ts->nsec / 1000000U;
        ok = ok && ts_ms >= before && ts_ms <= after;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    return ok;
}

static int test_syscall_getrandom(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    void *phys;
    int ok = 0;
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        uint8_t *buf = (uint8_t *)USER_BASE;
        ok = syscall_handler_c(SYS_GETRANDOM, (uint32_t)buf, 16, 0, 0) == 16 &&
             syscall_handler_c(SYS_GETRANDOM, (uint32_t)buf, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_GETRANDOM, USER_BASE + PAGE_SIZE, 1, 0, 0) ==
                 (uint32_t)-1 &&
             syscall_handler_c(SYS_GETRANDOM, (uint32_t)buf, 1, 1, 0) == (uint32_t)-1;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    return ok;
}

static int test_syscall_network(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int ok = 0;
    if (netif_count() == 0) return 0;
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        sys_netif_info_t *info = (sys_netif_info_t *)USER_BASE;
        char *payload = (char *)(USER_BASE + 128U);
        char *recv = (char *)(USER_BASE + 256U);
        sys_udp_peer_t *peer = (sys_udp_peer_t *)(USER_BASE + 384U);
        strcopy(payload, "net-sys", 16);
        ok = syscall_handler_c(SYS_NETIF_COUNT, 0, 0, 0, 0) >= 1 &&
             syscall_handler_c(SYS_NETIF_INFO, 0, (uint32_t)info, 0, 0) == 0 &&
             streq(info->name, "lo0") &&
             info->ipv4 == 0x7F000001U &&
             syscall_handler_c(SYS_NETIF_INFO, 0, USER_BASE + PAGE_SIZE, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_NET_PING4, 0x7F000001U, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_UDP_SEND4,
                               0x7F000001U,
                               9100,
                               (uint32_t)payload,
                               7) == 7 &&
             syscall_handler_c(SYS_UDP_RECV4,
                               9100,
                               (uint32_t)recv,
                               16,
                               (uint32_t)peer) == 7 &&
             memeq(recv, "net-sys", 7) &&
             peer->src_ipv4 == 0x7F000001U &&
             peer->src_port == 9101 &&
             syscall_handler_c(SYS_UDP_SEND4,
                               0x7F000001U,
                               9102,
                               USER_BASE + PAGE_SIZE,
                               1) == (uint32_t)-1 &&
	             syscall_handler_c(SYS_UDP_RECV4,
	                               9100,
	                               USER_BASE + PAGE_SIZE,
	                               1,
	                               (uint32_t)peer) == (uint32_t)-1 &&
	             syscall_handler_c(SYS_UDP_RECV4,
	                               9100,
	                               (uint32_t)recv,
	                               1,
	                               USER_BASE + PAGE_SIZE) == (uint32_t)-1;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    return ok;
}

static int test_syscall_udp_socket(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int ok = 0;
    if (netif_count() == 0) return 0;
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        sys_sockaddr_in4_t *addr = (sys_sockaddr_in4_t *)USER_BASE;
        sys_sockaddr_in4_t *peer = (sys_sockaddr_in4_t *)(USER_BASE + 16U);
        char *payload = (char *)(USER_BASE + 64U);
        char *recv = (char *)(USER_BASE + 128U);
        vfs_dirent_t *st = (vfs_dirent_t *)(USER_BASE + 256U);
        uint32_t *readfds = (uint32_t *)(USER_BASE + 384U);
        uint32_t *writefds = (uint32_t *)(USER_BASE + 388U);
        int fd = (int)syscall_handler_c(SYS_SOCKET_UDP4, 0, 0, 0, 0);
        strcopy(payload, "sock", 16);
        addr->ipv4 = 0x7F000001U;
        addr->port = 9300;
        if (fd >= 3) {
            uint32_t fd_bit = 1U << (uint32_t)fd;
            *readfds = fd_bit;
            *writefds = fd_bit;
            ok = syscall_handler_c(SYS_BIND_UDP4, (uint32_t)fd, 9300, 0, 0) == 0 &&
                 syscall_handler_c(SYS_BIND_UDP4, (uint32_t)fd, 9301, 0, 0) == (uint32_t)-1 &&
                 syscall_handler_c(SYS_FSTAT, (uint32_t)fd, (uint32_t)st, 0, 0) == 0 &&
                 st->type == VFS_NODE_DEV &&
                 syscall_handler_c(SYS_POLL, (uint32_t)fd, VFS_POLL_READ, 0, 0) == 0 &&
                 syscall_handler_c(SYS_SENDTO_UDP4,
                                   (uint32_t)fd,
                                   (uint32_t)addr,
                                   (uint32_t)payload,
                                   4) == 4 &&
                 syscall_handler_c(SYS_POLL, (uint32_t)fd, VFS_POLL_READ, 0, 0) == VFS_POLL_READ &&
                 syscall_handler_c(SYS_SELECT,
                                   (uint32_t)readfds,
                                   0,
                                   (uint32_t)fd + 1U,
                                   0) == 1 &&
                 (*readfds & fd_bit) != 0 &&
                 syscall_handler_c(SYS_RECVFROM_UDP4,
                                   (uint32_t)fd,
                                   (uint32_t)peer,
                                   (uint32_t)recv,
                                   16) == 4 &&
                 memeq(recv, "sock", 4) &&
                 peer->ipv4 == 0x7F000001U &&
                 peer->port == 9300 &&
                 syscall_handler_c(SYS_POLL, (uint32_t)fd, VFS_POLL_READ, 0, 0) == 0 &&
                 syscall_handler_c(SYS_RECVFROM_UDP4,
                                   (uint32_t)fd,
                                   (uint32_t)peer,
                                   (uint32_t)recv,
                                   16) == 0 &&
                 syscall_handler_c(SYS_FCNTL,
                                   (uint32_t)fd,
                                   SYS_FCNTL_SETFL,
                                   VFS_O_RDWR | VFS_O_NONBLOCK,
                                   0) == 0 &&
                 syscall_handler_c(SYS_RECVFROM_UDP4,
                                   (uint32_t)fd,
                                   (uint32_t)peer,
                                   (uint32_t)recv,
                                   16) == (uint32_t)-1 &&
                 syscall_handler_c(SYS_FCNTL,
                                   (uint32_t)fd,
                                   SYS_FCNTL_SETFL,
                                   VFS_O_RDWR,
                                   0) == 0 &&
                 syscall_handler_c(SYS_CONNECT_UDP4,
                                   (uint32_t)fd,
                                   (uint32_t)addr,
                                   0,
                                   0) == 0 &&
                 syscall_handler_c(SYS_GETSOCKNAME_UDP4,
                                   (uint32_t)fd,
                                   (uint32_t)peer,
                                   0,
                                   0) == 0 &&
                 peer->ipv4 == 0x7F000001U &&
                 peer->port == 9300 &&
                 syscall_handler_c(SYS_GETPEERNAME_UDP4,
                                   (uint32_t)fd,
                                   (uint32_t)peer,
                                   0,
                                   0) == 0 &&
                 peer->ipv4 == 0x7F000001U &&
                 peer->port == 9300 &&
                 syscall_handler_c(SYS_SELECT,
                                   0,
                                   (uint32_t)writefds,
                                   (uint32_t)fd + 1U,
                                   0) == 1 &&
                 (*writefds & fd_bit) != 0 &&
                 syscall_handler_c(SYS_WRITE, (uint32_t)fd, (uint32_t)payload, 4, 0) == 4 &&
                 syscall_handler_c(SYS_READ, (uint32_t)fd, (uint32_t)recv, 16, 0) == 4 &&
                 memeq(recv, "sock", 4) &&
                 syscall_handler_c(SYS_SELECT,
                                   USER_BASE + PAGE_SIZE,
                                   0,
                                   (uint32_t)fd + 1U,
                                   0) == (uint32_t)-1 &&
                 syscall_handler_c(SYS_SELECT,
                                   (uint32_t)readfds,
                                   0,
                                   PROCESS_MAX_FDS + 1U,
                                   0) == (uint32_t)-1 &&
                 syscall_handler_c(SYS_CONNECT_UDP4,
                                   (uint32_t)fd,
                                   USER_BASE + PAGE_SIZE,
                                   0,
                                   0) == (uint32_t)-1 &&
                 syscall_handler_c(SYS_GETSOCKNAME_UDP4,
                                   (uint32_t)fd,
                                   USER_BASE + PAGE_SIZE,
                                   0,
                                   0) == (uint32_t)-1 &&
                 syscall_handler_c(SYS_SENDTO_UDP4,
                                   (uint32_t)fd,
                                   USER_BASE + PAGE_SIZE,
                                   (uint32_t)payload,
                                   4) == (uint32_t)-1 &&
                 syscall_handler_c(SYS_SENDTO_UDP4,
                                   (uint32_t)fd,
                                   (uint32_t)addr,
                                   USER_BASE + PAGE_SIZE,
                                   1) == (uint32_t)-1 &&
                 syscall_handler_c(SYS_RECVFROM_UDP4,
                                   (uint32_t)fd,
                                   USER_BASE + PAGE_SIZE,
                                   (uint32_t)recv,
                                   16) == (uint32_t)-1 &&
                 syscall_handler_c(SYS_CLOSE, (uint32_t)fd, 0, 0, 0) == 0 &&
                 syscall_handler_c(SYS_RECVFROM_UDP4,
                                   (uint32_t)fd,
                                   (uint32_t)peer,
                                   (uint32_t)recv,
                                   16) == (uint32_t)-1;
            if (!ok) (void)syscall_handler_c(SYS_CLOSE, (uint32_t)fd, 0, 0, 0);
        }
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    return ok;
}


static int test_syscall_network_control(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    const netif_t *lo = netif_at(0);
    uint32_t old_ip;
    int old_up;
    int ok = 0;
    if (!lo) return 0;
    old_ip = lo->ipv4;
    old_up = lo->up;
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        uint8_t *mac = (uint8_t *)USER_BASE;
        sys_route_info_t *route_info = (sys_route_info_t *)(USER_BASE + 64U);
        sys_arp_info_t *arp_info = (sys_arp_info_t *)(USER_BASE + 128U);
        uint8_t found[6];
        uint32_t route_count_before = syscall_handler_c(SYS_ROUTE_COUNT, 0, 0, 0, 0);
        uint32_t arp_count_before = syscall_handler_c(SYS_ARP_COUNT, 0, 0, 0, 0);
        uint32_t ifindex = 99;
        uint32_t gateway = 99;
        mac[0] = 0x02;
        mac[1] = 0x00;
        mac[2] = 0x00;
        mac[3] = 0x00;
        mac[4] = 0x00;
        mac[5] = 0x77;
        ok = syscall_handler_c(SYS_NETIF_SET_UP, 0, 0, 0, 0) == 0 &&
             netif_at(0) && !netif_at(0)->up &&
             syscall_handler_c(SYS_NETIF_SET_UP, 0, 1, 0, 0) == 0 &&
             netif_at(0) && netif_at(0)->up &&
             syscall_handler_c(SYS_NETIF_SET_IPV4, 0, 0x7F000010U, 0, 0) == 0 &&
             netif_at(0) && netif_at(0)->ipv4 == 0x7F000010U &&
             syscall_handler_c(SYS_ROUTE_ADD4,
                               0xC0000200U,
                               0xFFFFFF00U,
                               0,
                               0) == 0 &&
             net_route_lookup4(0xC0000201U, &ifindex, &gateway) == 0 &&
             ifindex == 0 &&
             gateway == 0 &&
             syscall_handler_c(SYS_ARP_ADD4, 0, 0xC0000202U, (uint32_t)mac, 0) == 0 &&
             net_arp_lookup(0, 0xC0000202U, found) == 0 &&
             memeq((const char *)found, (const char *)mac, 6) &&
             syscall_handler_c(SYS_ROUTE_DEL4,
                               0xC0000200U,
                               0xFFFFFF00U,
                               0,
                               0) == 0 &&
             net_route_lookup4(0xC0000201U, &ifindex, &gateway) < 0 &&
             syscall_handler_c(SYS_ROUTE_DEL4,
                               0xC0000200U,
                               0xFFFFFF00U,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_ARP_DEL4, 0, 0xC0000202U, 0, 0) == 0 &&
             net_arp_lookup(0, 0xC0000202U, found) < 0 &&
             syscall_handler_c(SYS_ARP_DEL4, 0, 0xC0000202U, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_ARP_ADD4,
                               0,
                               0xC0000203U,
                               USER_BASE + PAGE_SIZE,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_NETIF_SET_IPV4,
                               NET_MAX_IFACES,
                               0x01020304U,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_ROUTE_ADD4,
                               0x0A000000U,
                               0x00FF00FFU,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_ROUTE_COUNT, 0, 0, 0, 0) >= route_count_before &&
             syscall_handler_c(SYS_ARP_COUNT, 0, 0, 0, 0) >= arp_count_before &&
             syscall_handler_c(SYS_ROUTE_INFO, 0, (uint32_t)route_info, 0, 0) == 0 &&
             route_info->ifindex < netif_count() &&
             syscall_handler_c(SYS_ARP_INFO, 0, (uint32_t)arp_info, 0, 0) == 0 &&
             arp_info->ifindex < netif_count() &&
             syscall_handler_c(SYS_ROUTE_INFO,
                               syscall_handler_c(SYS_ROUTE_COUNT, 0, 0, 0, 0),
                               (uint32_t)route_info,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_ARP_INFO,
                               syscall_handler_c(SYS_ARP_COUNT, 0, 0, 0, 0),
                               (uint32_t)arp_info,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_ROUTE_INFO,
                               route_count_before,
                               USER_BASE + PAGE_SIZE,
                               0,
                               0) == (uint32_t)-1;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    if (netif_set_ipv4(0, old_ip) < 0) ok = 0;
    if (netif_set_up(0, old_up) < 0) ok = 0;
    paging_destroy_user_directory(dir);
    return ok;
}

static int test_syscall_observability(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir = paging_clone_kernel_directory();
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int ok = 0;
    if (block_count() == 0 || driver_count() == 0 || process_count() == 0) return 0;
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        sys_sysinfo_t *sysinfo = (sys_sysinfo_t *)USER_BASE;
        sys_process_info_t *proc = (sys_process_info_t *)(USER_BASE + 128U);
        sys_block_info_t *block = (sys_block_info_t *)(USER_BASE + 256U);
        sys_driver_info_t *driver = (sys_driver_info_t *)(USER_BASE + 384U);
        sys_task_info_t *task = (sys_task_info_t *)(USER_BASE + 512U);
        sys_pci_info_t *pci = (sys_pci_info_t *)(USER_BASE + 640U);
        sys_utsname_t *uts = (sys_utsname_t *)(USER_BASE + 768U);
        uint32_t proc_count = process_count();
        uint32_t block_count_now = block_count();
        uint32_t driver_count_now = driver_count();
        uint32_t pci_count_now = pci_count();
        uint32_t task_count_now;
        ok = syscall_handler_c(SYS_SYSINFO, (uint32_t)sysinfo, 0, 0, 0) == 0 &&
             sysinfo->process_count == proc_count &&
             sysinfo->block_count == block_count_now &&
             sysinfo->driver_count == driver_count_now &&
             sysinfo->pci_count == pci_count_now &&
             sysinfo->netif_count == netif_count() &&
             syscall_handler_c(SYS_SYSINFO, USER_BASE + PAGE_SIZE, 0, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_UNAME, (uint32_t)uts, 0, 0, 0) == 0 &&
             streq(uts->sysname, "MyOS") &&
             streq(uts->nodename, "myos-machine") &&
             streq(uts->machine, "i386") &&
             syscall_handler_c(SYS_UNAME, USER_BASE + PAGE_SIZE, 0, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_SYSCONF, SYS_CONF_PAGE_SIZE, 0, 0, 0) == PAGE_SIZE &&
             syscall_handler_c(SYS_SYSCONF, SYS_CONF_PROCESS_MAX, 0, 0, 0) == PROCESS_MAX &&
             syscall_handler_c(SYS_SYSCONF, SYS_CONF_TASK_MAX, 0, 0, 0) == MAX_TASKS &&
             syscall_handler_c(SYS_SYSCONF, SYS_CONF_TIMER_HZ, 0, 0, 0) == timer_hz() &&
             syscall_handler_c(SYS_SYSCONF, SYS_CONF_ENV_MAX, 0, 0, 0) == PROCESS_MAX_ENV &&
             syscall_handler_c(SYS_SYSCONF, SYS_CONF_ENV_VALUE_MAX, 0, 0, 0) == PROCESS_ENV_MAX &&
             syscall_handler_c(SYS_SYSCONF, SYS_CONF_UTS_FIELD_MAX, 0, 0, 0) == UTS_FIELD_MAX &&
             syscall_handler_c(SYS_SYSCONF, 0xFFFFFFFFU, 0, 0, 0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_PROCESS_COUNT, 0, 0, 0, 0) == proc_count &&
             syscall_handler_c(SYS_PROCESS_INFO, 0, (uint32_t)proc, 0, 0) == 0 &&
             proc->pid == 0 &&
             proc->pgid == 0 &&
             proc->sid == 0 &&
             streq(proc->name, "kernel") &&
             syscall_handler_c(SYS_PROCESS_INFO,
                               proc_count,
                               (uint32_t)proc,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_PROCESS_INFO,
                               0,
                               USER_BASE + PAGE_SIZE,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_BLOCK_COUNT, 0, 0, 0, 0) == block_count_now &&
             syscall_handler_c(SYS_BLOCK_INFO, 0, (uint32_t)block, 0, 0) == 0 &&
             streq(block->name, "ram0") &&
             block->sector_size == BLOCK_SECTOR_SIZE &&
             syscall_handler_c(SYS_BLOCK_INFO,
                               block_count_now,
                               (uint32_t)block,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_DRIVER_COUNT, 0, 0, 0, 0) == driver_count_now &&
             syscall_handler_c(SYS_DRIVER_INFO, 0, (uint32_t)driver, 0, 0) == 0 &&
             streq(driver->name, "serial-console") &&
             driver->loaded == 1U &&
             syscall_handler_c(SYS_DRIVER_INFO,
                               driver_count_now,
                               (uint32_t)driver,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_TASK_COUNT, 0, 0, 0, 0) >= 1;
        task_count_now = syscall_handler_c(SYS_TASK_COUNT, 0, 0, 0, 0);
        ok = ok &&
             sysinfo->task_count == task_count_now &&
             syscall_handler_c(SYS_TASK_INFO, 0, (uint32_t)task, 0, 0) == 0 &&
             task->id == 0 &&
             task->priority == TASK_PRIORITY_DEFAULT &&
             streq(task->name, "kernel") &&
             syscall_handler_c(SYS_TASK_INFO,
                               task_count_now,
                               (uint32_t)task,
                               0,
                               0) == (uint32_t)-1 &&
             syscall_handler_c(SYS_PCI_COUNT, 0, 0, 0, 0) == pci_count_now &&
             syscall_handler_c(SYS_PCI_INFO,
                               pci_count_now,
                               (uint32_t)pci,
                               0,
                               0) == (uint32_t)-1;
        if (ok && pci_count_now > 0) {
            ok = syscall_handler_c(SYS_PCI_INFO, 0, (uint32_t)pci, 0, 0) == 0 &&
                 pci->vendor_id != 0xFFFFU;
        }
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    paging_destroy_user_directory(dir);
    return ok;
}


static int test_syscall_block_simplefs(void) {
    uint32_t *old_dir = paging_current_directory();
    uint32_t *dir;
    void *phys;
    uint32_t quantum = scheduler_quantum_ticks();
    int preempt = scheduler_preemption_enabled();
    int restore_dev = block_find("ram0");
    int dev;
    int ok = 0;
    vfs_dirent_t ent;
    if (restore_dev < 0) return 0;
    if (vfs_mkdir("/tmp/sysmnt") < 0 &&
        (vfs_stat("/tmp/sysmnt", &ent) < 0 || ent.type != VFS_NODE_DIR)) return 0;
    selftest_memzero(g_syscall_disk.data, sizeof(g_syscall_disk.data));
    g_syscall_disk.fail_dir_writes = 0;
    dev = block_find("sysblk");
    if (dev < 0) {
        dev = block_register("sysblk",
                             SELFTEST_SFS_SECTORS,
                             1,
                             selftest_fault_read,
                             selftest_fault_write,
                             &g_syscall_disk);
    }
    if (dev < 0) return 0;
    dir = paging_clone_kernel_directory();
    if (!dir) return 0;
    phys = pmm_alloc();
    if (!phys) {
        paging_destroy_user_directory(dir);
        return 0;
    }
    if (paging_map_user_page(dir, USER_BASE, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
        pmm_free(phys);
        paging_destroy_user_directory(dir);
        return 0;
    }
    scheduler_set_preemption(0, quantum);
    paging_switch_directory(dir);
    {
        uint8_t *sector = (uint8_t *)USER_BASE;
        uint8_t *readback = (uint8_t *)(USER_BASE + BLOCK_SECTOR_SIZE);
        char *mount_path = (char *)(USER_BASE + BLOCK_SECTOR_SIZE * 2U);
        uint32_t i;
        for (i = 0; i < BLOCK_SECTOR_SIZE; i++) {
            sector[i] = (uint8_t)i;
            readback[i] = 0;
        }
        strcopy(mount_path, "/tmp/sysmnt", 32);
        ok = syscall_handler_c(SYS_BLOCK_WRITE, (uint32_t)dev, 7, (uint32_t)sector, 1) == 1 &&
             syscall_handler_c(SYS_BLOCK_READ, (uint32_t)dev, 7, (uint32_t)readback, 1) == 1 &&
             memeq((const char *)sector, (const char *)readback, BLOCK_SECTOR_SIZE) &&
             syscall_handler_c(SYS_BLOCK_READ,
                               (uint32_t)dev,
                               7,
                               USER_BASE + PAGE_SIZE,
                               1) == (uint32_t)-1 &&
             syscall_handler_c(SYS_BLOCK_WRITE,
                               (uint32_t)dev,
                               7,
                               USER_BASE + PAGE_SIZE,
                               1) == (uint32_t)-1 &&
             syscall_handler_c(SYS_BLOCK_READ, (uint32_t)dev, 0, (uint32_t)readback, 0) == 0 &&
             syscall_handler_c(SYS_BLOCK_READ,
                               (uint32_t)dev,
                               0,
                               (uint32_t)readback,
                               0xFFFFFFFFU) == (uint32_t)-1 &&
             syscall_handler_c(SYS_SIMPLEFS_FORMAT, (uint32_t)dev, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_SIMPLEFS_MOUNT,
                               (uint32_t)dev,
                               (uint32_t)mount_path,
                               0,
                               0) == 0 &&
             syscall_handler_c(SYS_SIMPLEFS_MOUNT,
                               (uint32_t)dev,
                               USER_BASE + PAGE_SIZE,
                               0,
                               0) == (uint32_t)-1 &&
             write_read_file("/tmp/sysmnt/user.txt", "disk syscall\n") &&
             syscall_handler_c(SYS_SIMPLEFS_UNMOUNT, 0, 0, 0, 0) == 0 &&
             syscall_handler_c(SYS_SIMPLEFS_UNMOUNT, 0, 0, 0, 0) == (uint32_t)-1;
    }
    paging_switch_directory(old_dir);
    scheduler_set_preemption(preempt, quantum);
    if (simplefs_mount((uint32_t)restore_dev, "/disk") < 0) ok = 0;
    paging_destroy_user_directory(dir);
    return ok;
}

static int test_scheduler_process_state(void) {
    process_t *p = process_create_kernel("state-test", selftest_sleep_entry);
    uint32_t pid;
    int code = 0;
    int ok;
    if (!p || p->state != PROC_READY) return 0;
    pid = p->pid;
    task_yield();
    p = process_find(pid);
    ok = p && p->state == PROC_SLEEPING;
    if (process_kill(pid, -9) < 0) return 0;
    if (process_wait(pid, &code) != 1) return 0;
    return ok && code == -9;
}

static int test_scheduler_address_space(void) {
    return scheduler_preemption_enabled() &&
           task_current_address_space() == (uint32_t)paging_current_directory();
}

static int test_scheduler_task_slots(void) {
    return task_at(0) != 0 &&
           task_table_size() <= MAX_TASKS &&
           task_free_slots() < MAX_TASKS;
}

static int test_scheduler_task_destroy(void) {
    uint32_t before_free = task_free_slots();
    int id = task_create("destroy-test", selftest_sleep_entry);
    if (id < 0) return 0;
    if (task_free_slots() + 1U != before_free) {
        (void)task_destroy(id);
        return 0;
    }
    if (task_destroy(id) < 0) return 0;
    return task_free_slots() == before_free && task_at((uint32_t)id) == 0;
}

static int test_scheduler_task_create_metadata(void) {
    uint32_t before_free = task_free_slots();
    uint32_t cr3 = (uint32_t)paging_kernel_directory();
    uint32_t pid = 0x1234U;
    int id = task_create_for_process("configured-task", selftest_sleep_entry, pid, cr3);
    const task_t *task;
    int ok;
    if (id < 0) return 0;
    task = task_at((uint32_t)id);
    ok = task &&
         task->state == TASK_READY &&
         task->process_id == pid &&
         task->regs.cr3 == cr3 &&
         task->priority == TASK_PRIORITY_DEFAULT &&
         task_free_slots() + 1U == before_free;
    if (task_destroy(id) < 0) return 0;
    return ok && task_free_slots() == before_free;
}

static int test_scheduler_task_priority(void) {
    int id = task_create("priority-test", selftest_sleep_entry);
    int ok;
    if (id < 0) return 0;
    ok = task_priority(id) == TASK_PRIORITY_DEFAULT &&
         task_set_priority(id, TASK_PRIORITY_MAX) == 0 &&
         task_priority(id) == TASK_PRIORITY_MAX &&
         task_at((uint32_t)id) &&
         task_at((uint32_t)id)->priority == TASK_PRIORITY_MAX &&
         task_set_priority(id, TASK_PRIORITY_MAX + 1U) < 0 &&
         task_set_priority(MAX_TASKS, TASK_PRIORITY_DEFAULT) < 0;
    if (task_destroy(id) < 0) return 0;
    return ok;
}

static int test_scheduler_sleep_zero_state(void) {
    int id = task_current_id();
    const task_t *task;
    task_sleep(0);
    task = task_at((uint32_t)id);
    return task_current_id() == id &&
           task &&
           task->state == TASK_RUNNING;
}

static void selftest_task_entry(void) {
    g_selftest_task_runs++;
}

static int test_scheduler_task_reuse(void) {
    uint32_t before_free = task_free_slots();
    uint32_t before_runs = g_selftest_task_runs;
    int id = task_create("selftest-task", selftest_task_entry);
    if (id < 0) return 0;
    task_yield();
    if (g_selftest_task_runs != before_runs + 1) return 0;
    return task_free_slots() == before_free;
}

static void selftest_preempt_entry(void) {
    uint32_t start = timer_ticks();
    uint32_t guard = 2000000U;
    do {
        g_selftest_preempt_runs++;
    } while (guard-- && timer_ticks() - start < 2);
}

static int test_scheduler_irq_preemption(void) {
    uint32_t before_irq = scheduler_irq_switches();
    uint32_t before_runs = g_selftest_preempt_runs;
    uint32_t start = timer_ticks();
    uint32_t guard = 4000000U;
    int id = task_create("preempt-test", selftest_preempt_entry);
    int ok;
    if (id < 0) return 0;
    while (guard-- &&
           g_selftest_preempt_runs == before_runs &&
           timer_ticks() - start < 100) {
    }
    ok = g_selftest_preempt_runs != before_runs &&
         scheduler_irq_switches() > before_irq;
    if (!ok) task_yield();
    return ok;
}

static int test_driver_registry(void) {
    const driver_t *first = driver_at(0);
    uint32_t before = driver_count();
    uint32_t expected = before;
    char tmp_name[DRIVER_NAME_MAX] = "tmp-driver";
    int tmp_idx = -1;
    uint32_t i;
    if (driver_register("", DRIVER_BUS_PLATFORM, 0, 0) >= 0) return 0;
    if (driver_register("bad/name", DRIVER_BUS_PLATFORM, 0, 0) >= 0) return 0;
    if (driver_register("abcdefghijklmnopqrstuvwxyzabcdef", DRIVER_BUS_PLATFORM, 0, 0) >= 0) return 0;
    if (driver_register("bad-bus", (driver_bus_t)99, 0, 0) >= 0) return 0;
    for (i = 0; i < driver_count(); i++) {
        const driver_t *drv = driver_at(i);
        if (drv && streq(drv->name, "tmp-driver")) tmp_idx = (int)i;
    }
    if (tmp_idx < 0) {
        if (before >= DRIVER_MAX) return 0;
        tmp_idx = driver_register(tmp_name, DRIVER_BUS_PLATFORM, 1, 2);
        expected = before + 1U;
    }
    if (tmp_idx < 0) return 0;
    tmp_name[0] = 'x';
    return driver_count() > 0 &&
           driver_count() == expected &&
           first != 0 &&
           first->loaded &&
           driver_at((uint32_t)tmp_idx) &&
           streq(driver_at((uint32_t)tmp_idx)->name, "tmp-driver") &&
           streq(driver_bus_name(first->bus), "platform") &&
           streq(driver_bus_name((driver_bus_t)99), "unknown");
}

static int test_pci_registry(void) {
    if (pci_count() > PCI_MAX_DEVICES) return 0;
    if (pci_config_read32(0, 32, 0, 0) != 0xFFFFFFFFU) return 0;
    if (pci_config_read32(0, 0, 8, 0) != 0xFFFFFFFFU) return 0;
    if (pci_config_read32(0, 0, 0, 0xFF) != 0xFFFFFFFFU) return 0;
    pci_config_write32(0, 32, 0, 0, 0);
    return pci_count() == 0 || pci_at(0) != 0;
}

static int test_net_loopback(void) {
    const netif_t *lo;
    const netif_t *dyn;
    vfs_dirent_t net_ent;
    uint32_t original_ip;
    uint32_t tx_before;
    uint32_t rx_before;
    uint32_t rxq_before;
    uint32_t udp_before;
    uint32_t route_if = 0;
    uint32_t route_gateway = 1;
    uint32_t dyn_original_ip;
    uint32_t dyn_route_if;
    uint32_t dyn_route_gateway;
    uint32_t i;
    int down_ok;
    int dyn_net;
    int fd;
    int n;
    uint16_t ip_sum;
    char dyn_path[16];
    char dyn_name[16];
    char buf[16];
    char copied_name[NET_NAME_MAX] = "tmpnet";
    uint8_t arp_mac[6];
    uint8_t dyn_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x42};
    uint8_t copy_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x24};
    uint8_t zero_mac[6] = {0, 0, 0, 0, 0, 0};
    uint8_t multicast_mac[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01};
    uint8_t peer_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x33};
    uint8_t oversize[NET_FRAME_MAX + 1];
    uint8_t bad_udp[28];
    uint8_t bad_icmp[28];
    static const char payload[] = "netdev";
    if (netif_count() == 0) return 0;
    lo = netif_at(0);
    if (!lo) return 0;
    if (net_route_count() == 0 ||
        net_route_at(0) == 0 ||
        net_route_lookup4(lo->ipv4, &route_if, &route_gateway) < 0 ||
        route_if != 0 ||
        route_gateway != 0 ||
        net_route_add(0x0A000000U, 0x00FF00FFU, 0, 0) == 0) return 0;
    if (net_arp_count() == 0 ||
        net_arp_lookup(0, lo->ipv4, arp_mac) < 0 ||
        !memeq((const char *)arp_mac, (const char *)lo->mac, 6) ||
        net_arp_learn(NET_MAX_IFACES, lo->ipv4, lo->mac) == 0 ||
        net_arp_learn(0, 0, lo->mac) == 0 ||
        net_arp_learn(0, 0x7F000003U, zero_mac) == 0 ||
        net_arp_learn(0, 0x7F000004U, multicast_mac) == 0) return 0;
    if (net_arp_learn(0, 0x7F000002U, peer_mac) < 0 ||
        net_arp_lookup(0, 0x7F000002U, arp_mac) < 0 ||
        !memeq((const char *)arp_mac, (const char *)peer_mac, 6)) return 0;
    if (net_register_if("", lo->mac, lo->ipv4, lo->mtu, 0, 0, 0) >= 0) return 0;
    if (net_register_if("bad/name", lo->mac, lo->ipv4, lo->mtu, 0, 0, 0) >= 0) return 0;
    if (net_register_if("abcdefghijklmnop", lo->mac, lo->ipv4, lo->mtu, 0, 0, 0) >= 0) return 0;
    if (net_register_if("lo0", lo->mac, lo->ipv4, lo->mtu, 0, 0, 0) >= 0) return 0;
    if (find_netif_by_name("tmpnet") < 0 && netif_count() < NET_MAX_IFACES) {
        int copy_idx = net_register_if(copied_name, copy_mac, 0x0A000002U, 128, 0, 0, 0);
        const netif_t *copy_net;
        if (copy_idx < 0) return 0;
        copied_name[0] = 'x';
        copy_net = netif_at((uint32_t)copy_idx);
        if (!copy_net || !streq(copy_net->name, "tmpnet")) return 0;
    }
    if (vfs_stat("/dev/net0", &net_ent) < 0 ||
        net_ent.type != VFS_NODE_DEV ||
        net_ent.size != lo->mtu) return 0;
    if (vfs_stat("/dev/net4294967296", &net_ent) == 0) return 0;
    fd = vfs_open("/dev/net4294967296", VFS_O_RDONLY);
    if (fd >= 0) {
        vfs_close(fd);
        return 0;
    }
    dyn_net = find_netif_by_name("selfnet");
    if (dyn_net < 0 && netif_count() < NET_MAX_IFACES) {
        dyn_net = net_register_if("selfnet", dyn_mac, 0x0A000001U, 128, 0, 0, 0);
    }
    if (dyn_net < 0 && netif_count() > 1) dyn_net = 1;
    if (dyn_net < 0) return 0;
    dyn = netif_at((uint32_t)dyn_net);
    if (!dyn ||
        make_selftest_net_path((uint32_t)dyn_net, dyn_path, sizeof(dyn_path)) < 0 ||
        make_selftest_net_name((uint32_t)dyn_net, dyn_name, sizeof(dyn_name)) < 0 ||
        !dir_has_entry("/dev", dyn_name, VFS_NODE_DEV) ||
        vfs_stat(dyn_path, &net_ent) < 0 ||
        net_ent.type != VFS_NODE_DEV ||
        net_ent.size != dyn->mtu) return 0;
    dyn_original_ip = dyn->ipv4;
    dyn_route_if = 0;
    dyn_route_gateway = 1;
    if (dyn_original_ip) {
        if (netif_set_ipv4((uint32_t)dyn_net, 0x0A630101U) < 0 ||
            net_route_lookup4(0x0A63014DU, &dyn_route_if, &dyn_route_gateway) < 0 ||
            dyn_route_if != (uint32_t)dyn_net ||
            dyn_route_gateway != 0 ||
            netif_set_ipv4((uint32_t)dyn_net, dyn_original_ip) < 0) return 0;
    }
    for (i = 0; i < sizeof(oversize); i++) oversize[i] = 0;
    tx_before = lo->tx_packets;
    rx_before = lo->rx_packets;
    if (net_send(0, oversize, lo->mtu + 1U) >= 0) return 0;
    if (lo->tx_packets != tx_before) return 0;
    if (net_receive_from_driver(0, oversize, lo->mtu + 1U) >= 0) return 0;
    if (lo->rx_packets != rx_before) return 0;
    for (i = 0; i < sizeof(bad_udp); i++) bad_udp[i] = 0;
    bad_udp[0] = 0x45;
    bad_udp[3] = 20;
    bad_udp[9] = 17;
    bad_udp[12] = 127;
    bad_udp[15] = 1;
    bad_udp[16] = 127;
    bad_udp[19] = 1;
    bad_udp[20] = 0x04;
    bad_udp[21] = 0xD2;
    bad_udp[22] = 0x04;
    bad_udp[23] = 0xD3;
    bad_udp[25] = 8;
    ip_sum = selftest_checksum16(bad_udp, 20);
    bad_udp[10] = (uint8_t)(ip_sum >> 8);
    bad_udp[11] = (uint8_t)(ip_sum & 0xFF);
    udp_before = net_udp_queue_count();
    if (net_receive_from_driver(0, bad_udp, sizeof(bad_udp)) != (int)sizeof(bad_udp)) return 0;
    net_poll();
    if (net_udp_queue_count() != udp_before) return 0;
    for (i = 0; i < sizeof(bad_udp); i++) bad_udp[i] = 0;
    bad_udp[0] = 0x45;
    bad_udp[3] = 28;
    bad_udp[9] = 17;
    bad_udp[12] = 127;
    bad_udp[15] = 1;
    bad_udp[16] = 127;
    bad_udp[19] = 1;
    bad_udp[20] = 0x04;
    bad_udp[21] = 0xD2;
    bad_udp[22] = 0x04;
    bad_udp[23] = 0xD3;
    bad_udp[25] = 8;
    udp_before = net_udp_queue_count();
    if (net_receive_from_driver(0, bad_udp, sizeof(bad_udp)) != (int)sizeof(bad_udp)) return 0;
    net_poll();
    if (net_udp_queue_count() != udp_before) return 0;
    bad_udp[26] = 0x12;
    bad_udp[27] = 0x34;
    udp_before = net_udp_queue_count();
    if (net_receive_from_driver(0, bad_udp, sizeof(bad_udp)) != (int)sizeof(bad_udp)) return 0;
    net_poll();
    if (net_udp_queue_count() != udp_before) return 0;
    for (i = 0; i < sizeof(bad_icmp); i++) bad_icmp[i] = 0;
    bad_icmp[0] = 0x45;
    bad_icmp[3] = 28;
    bad_icmp[9] = 1;
    bad_icmp[12] = 127;
    bad_icmp[15] = 1;
    bad_icmp[16] = 127;
    bad_icmp[19] = 1;
    bad_icmp[20] = 8;
    ip_sum = selftest_checksum16(bad_icmp, 20);
    bad_icmp[10] = (uint8_t)(ip_sum >> 8);
    bad_icmp[11] = (uint8_t)(ip_sum & 0xFF);
    rxq_before = net_rx_queue_count();
    if (net_receive_from_driver(0, bad_icmp, sizeof(bad_icmp)) != (int)sizeof(bad_icmp)) return 0;
    net_poll();
    if (net_rx_queue_count() != rxq_before) return 0;
    if (net_recv(0, 0, 0) != 0 || net_udp_recv4(1001, 0, 0, 0, 0) != 0) return 0;
    original_ip = lo->ipv4;
    if (net_ping4(0, original_ip) != 0) return 0;
    if (lo->rx_packets <= rx_before) return 0;
    rx_before = lo->rx_packets;
    if (netif_set_up(0, 0) < 0) return 0;
    down_ok = net_ping4(0, original_ip) < 0;
    if (net_receive_from_driver(0, payload, sizeof(payload) - 1) >= 0) down_ok = 0;
    if (lo->rx_packets != rx_before) down_ok = 0;
    if (netif_set_up(0, 1) < 0) return 0;
    if (!down_ok) return 0;
    if (netif_set_ipv4(0, 0) < 0) return 0;
    if (net_ping4(0, original_ip) >= 0 ||
        net_udp_send4(0, original_ip, 1000, 1001, payload, sizeof(payload) - 1) >= 0) {
        (void)netif_set_ipv4(0, original_ip);
        return 0;
    }
    if (netif_set_ipv4(0, original_ip) < 0) return 0;
    if (netif_set_ipv4(0, 0x7F000002U) < 0) return 0;
    if (net_ping4(0, 0x7F000002U) != 0) {
        (void)netif_set_ipv4(0, original_ip);
        return 0;
    }
    if (netif_set_ipv4(0, original_ip) < 0) return 0;
    if (net_ping4(0, original_ip) != 0) return 0;
    fd = vfs_open("/dev/net0", VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_read(fd, 0, 0) != 0 || vfs_write(fd, 0, 0) != 0) {
        vfs_close(fd);
        return 0;
    }
    if (vfs_write(fd, payload, sizeof(payload) - 1) != (int)(sizeof(payload) - 1)) {
        vfs_close(fd);
        return 0;
    }
    n = vfs_read(fd, buf, sizeof(buf));
    vfs_close(fd);
    if (n != (int)(sizeof(payload) - 1) || !memeq(buf, payload, sizeof(payload) - 1)) return 0;
    if (net_udp_send4(0, original_ip, 1000, 1001, "udp", 3) != 3) return 0;
    n = net_udp_recv4(1001, buf, sizeof(buf), &original_ip, 0);
    if (n != 3 || !memeq(buf, "udp", 3) || original_ip != lo->ipv4) return 0;
    original_ip = 0;
    if (net_udp_send4(0, lo->ipv4, 1999, 2000, 0, 0) != 0) return 0;
    if (net_udp_recv4(2000, 0, 0, &original_ip, 0) != 0 ||
        original_ip != lo->ipv4 ||
        net_udp_queue_count() != 0) return 0;
    if (net_udp_send4(0, original_ip, 2000, 2001, 0, 0) != 0) return 0;
    if (net_udp_queue_count() == 0) return 0;
    n = net_udp_recv4(2001, buf, sizeof(buf), &original_ip, 0);
    return n == 0 &&
           original_ip == lo->ipv4 &&
           net_rx_queue_count() == 0 &&
           net_udp_queue_count() == 0;
}

static int test_debug_log(void) {
    char buf[16];
    vfs_dirent_t ent;
    int fd;
    int n;
    uint32_t pos = 0;
    if (!debug_log_buffer() || debug_log_size() == 0) return 0;
    if (debug_log_read(0, buf, 1) >= 0 ||
        debug_log_read(&pos, 0, 1) >= 0 ||
        debug_log_read(&pos, buf, 0) != 0) {
        return 0;
    }
    pos = 0;
    if (debug_log_read(&pos, buf, sizeof(buf)) <= 0) return 0;
    if (vfs_stat("/dev/kmsg", &ent) < 0 || ent.type != VFS_NODE_DEV) return 0;
    fd = vfs_open("/dev/kmsg", VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_read(fd, 0, 0) != 0 || vfs_write(fd, 0, 0) != 0) {
        vfs_close(fd);
        return 0;
    }
    n = vfs_read(fd, buf, sizeof(buf));
    if (n <= 0) {
        vfs_close(fd);
        return 0;
    }
    if (vfs_write(fd, "selftest-kmsg\n", 14) != 14) {
        vfs_close(fd);
        return 0;
    }
    return vfs_close(fd) == 0;
}

static int test_procfs_children(void) {
    uint32_t old_pid = task_current_process_id();
    process_t *parent = process_create_kernel("proc-children-parent", selftest_sleep_entry);
    process_t *child;
    uint32_t parent_pid;
    uint32_t child_pid = 0;
    char path[64];
    char expected[24];
    char buf[96];
    uint32_t pos = 0;
    uint32_t expected_pos = 0;
    int fd;
    int n;
    int code = 0;
    int ok = 0;
    if (!parent || parent->pid == 0) return 0;
    parent_pid = parent->pid;
    task_set_current_process_id(parent_pid);
    child = process_create_kernel("proc-children-child", selftest_sleep_entry);
    task_set_current_process_id(old_pid);
    if (!child || child->pid == 0) {
        if (child) {
            (void)process_kill(child->pid, -9);
            (void)process_wait(child->pid, &code);
        }
        (void)process_kill(parent_pid, -9);
        (void)process_wait(parent_pid, &code);
        return 0;
    }
    child_pid = child->pid;
    append_str(path, &pos, sizeof(path), "/proc/");
    append_dec(path, &pos, sizeof(path), parent_pid);
    append_str(path, &pos, sizeof(path), "/children");
    append_dec(expected, &expected_pos, sizeof(expected), child_pid);
    append_char(expected, &expected_pos, sizeof(expected), '\n');
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd >= 0) {
        n = vfs_read(fd, buf, sizeof(buf) - 1);
        vfs_close(fd);
        if (n > 0) {
            buf[n] = 0;
            ok = contains_text(buf, expected);
        }
    }
    if (process_find(child_pid)) {
        (void)process_kill(child_pid, -9);
        (void)process_wait(child_pid, &code);
    }
    if (process_find(parent_pid)) {
        (void)process_kill(parent_pid, -9);
        (void)process_wait(parent_pid, &code);
    }
    return ok && process_find(child_pid) == 0 && process_find(parent_pid) == 0;
}

static int test_shell_commands(void) {
    vfs_dirent_t ent;
    char mountbuf[160];
    int fd;
    int n;
    uint32_t i;
    uint32_t exec_pid = 0;
    uint32_t wait_pid = 0;
    uint32_t wait_pos = 0;
    uint32_t kill_pid = 0;
    uint32_t group_pid = 0;
    uint32_t kill_pos = 0;
    int exec_code = 0;
    process_t *wait_proc;
    process_t *kill_proc;
    process_t *group_proc;
    char wait_cmd[32];
    char kill_cmd[32];
    char old_host[UTS_FIELD_MAX];
    uint32_t old_umask = process_umask_get(process_current());
    if (uts_copy_nodename(old_host, sizeof(old_host)) < 0) return 0;
    (void)vfs_unlink("/tmp/shell-mv.txt");
    (void)vfs_unlink("/tmp/shell-moved.txt");
    (void)vfs_unlink("/tmp/shell-write.txt");
    (void)vfs_unlink("/tmp/shell-link.txt");
    (void)vfs_unlink("/tmp/shell-pwrite.txt");
    (void)vfs_unlink("/tmp/shell-redir.txt");
    (void)vfs_unlink("/tmp/shell-pipe.txt");
    (void)vfs_unlink("/tmp/shell-sort.txt");
    (void)vfs_unlink("/tmp/shell-umask.txt");
    (void)vfs_unlink("/tmp/shell-rmdir");
    (void)process_env_unset(process_current(), "SHELLTEST");
    shell_capture_reset();
    if (shell_execute_line("hostname", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "myos-machine\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("uname", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "MyOS i386\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("uname -a", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "MyOS myos-machine 0.1 kernel-dev i386\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("hostname node-shell", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) {
        (void)uts_set_nodename(old_host);
        return 0;
    }
    shell_capture_reset();
    if (shell_execute_line("hostname", shell_capture_line) != 1) {
        (void)uts_set_nodename(old_host);
        return 0;
    }
    if (!streq(g_shell_capture, "node-shell\n")) {
        (void)uts_set_nodename(old_host);
        return 0;
    }
    shell_capture_reset();
    if (shell_execute_line("uname -a", shell_capture_line) != 1) {
        (void)uts_set_nodename(old_host);
        return 0;
    }
    if (!streq(g_shell_capture, "MyOS node-shell 0.1 kernel-dev i386\n")) {
        (void)uts_set_nodename(old_host);
        return 0;
    }
    if (uts_set_nodename(old_host) < 0) return 0;
    (void)process_env_set(process_current(), "HOSTNAME", uts_nodename(), 1);
    shell_capture_reset();
    if (shell_execute_line("uptime", shell_capture_line) != 1) return 0;
    if (!memeq(g_shell_capture, "ticks ", 6) ||
        !contains_text(g_shell_capture, " ms ")) return 0;
    shell_capture_reset();
    if (shell_execute_line("clock", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "hz ") ||
        !contains_text(g_shell_capture, " ticks ") ||
        !contains_text(g_shell_capture, " ms ")) return 0;
    shell_capture_reset();
    if (shell_execute_line("time", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "sec ") ||
        !contains_text(g_shell_capture, " usec ")) return 0;
    shell_capture_reset();
    if (shell_execute_line("random 4", shell_capture_line) != 1) return 0;
    if (slen(g_shell_capture) != 9) return 0;
    shell_capture_reset();
    if (shell_execute_line("limits", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "page=") ||
        !contains_text(g_shell_capture, " process=") ||
        !contains_text(g_shell_capture, " hz=") ||
        !contains_text(g_shell_capture, " uts=")) return 0;
    shell_capture_reset();
    if (shell_execute_line("env", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "PATH=/bin\n") ||
        !contains_text(g_shell_capture, "HOME=/home/root\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("getenv HOME", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "/home/root\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("export SHELLTEST=value", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("getenv SHELLTEST", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "value\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("env", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "SHELLTEST=value\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("unset SHELLTEST", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("getenv SHELLTEST", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "getenv: not set\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("sysinfo", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "heap_free=") ||
        !contains_text(g_shell_capture, "heap_used=") ||
        !contains_text(g_shell_capture, "managed_pages=") ||
        !contains_text(g_shell_capture, "heap_blocks=")) return 0;
    shell_capture_reset();
    if (shell_execute_line("cd /home/root", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0 || !streq(shell_cwd(), "/home/root")) return 0;
    shell_capture_reset();
    if (shell_execute_line("stat ../../etc/hostname", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "hostname type=file")) return 0;
    shell_capture_reset();
    if (shell_execute_line("cd ../../tmp", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0 || !streq(shell_cwd(), "/tmp")) return 0;
    shell_capture_reset();
    if (shell_execute_line("cd ../home/root", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0 || !streq(shell_cwd(), "/home/root")) return 0;
    shell_capture_reset();
    if (shell_execute_line("write /tmp/shell-write.txt abcdef", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("write /tmp/shell-write.txt x", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("mkdir /tmp/shell-rmdir", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("rmdir /tmp/shell-rmdir", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("cat /tmp/shell-write.txt", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "x\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("pwrite /tmp/shell-pwrite.txt 0 abcdef", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("pread /tmp/shell-pwrite.txt 2 3", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "cde\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("pwrite /tmp/shell-pwrite.txt 3 XY", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("cat /tmp/shell-pwrite.txt", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "abcXYf\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("utime /tmp/shell-pwrite.txt 1234", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("stat /tmp/shell-pwrite.txt", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "mtime=1234")) return 0;
    shell_capture_reset();
    if (shell_execute_line("ln -s /tmp/shell-write.txt /tmp/shell-link.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("readlink /tmp/shell-link.txt", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "/tmp/shell-write.txt\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("cat /tmp/shell-link.txt", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "x\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("lstat /tmp/shell-link.txt", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "shell-link.txt type=link") ||
        !contains_text(g_shell_capture, "size=20")) return 0;
    shell_capture_reset();
    if (shell_execute_line("ls /tmp", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "link 20 shell-link.txt\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("grep myos /etc/hostname", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "myos-machine\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("cat /etc/hostname | grep myos", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "myos-machine\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("echo hello | wc", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "1 1 6\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("echo b > /tmp/shell-sort.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("echo a >> /tmp/shell-sort.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("echo a >> /tmp/shell-sort.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("sort /tmp/shell-sort.txt", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "a\na\nb\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("uniq /tmp/shell-sort.txt", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "b\na\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("cat /tmp/shell-sort.txt | sort", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "a\na\nb\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("cat /tmp/shell-sort.txt | uniq", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "b\na\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("head 1 /home/root/readme.txt", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "Welcome to MyOS.")) return 0;
    shell_capture_reset();
    if (shell_execute_line("cat /home/root/readme.txt | tail 1", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "This file lives in ramfs.")) return 0;
    shell_capture_reset();
    if (shell_execute_line("cat /etc/hostname | grep myos > /tmp/shell-pipe.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("cat /tmp/shell-pipe.txt", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "myos-machine\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("echo hello > /tmp/shell-redir.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("echo world >> /tmp/shell-redir.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("cat /tmp/shell-redir.txt", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "hello\nworld\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("fsync /tmp/shell-redir.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("fdatasync /tmp/shell-redir.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("sync", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("write /tmp/shell-trunc.txt abcdef", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("truncate /tmp/shell-trunc.txt 3", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("stat /tmp/shell-trunc.txt", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "size=3") ||
        !contains_text(g_shell_capture, "mode=") ||
        !contains_text(g_shell_capture, "mtime=")) return 0;
    shell_capture_reset();
    if (shell_execute_line("chmod 0400 /tmp/shell-trunc.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("access /tmp/shell-trunc.txt r", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "ok\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("access /tmp/shell-trunc.txt w", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "denied\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("stat /tmp/shell-trunc.txt", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "mode=0400")) return 0;
    shell_capture_reset();
    if (shell_execute_line("chown 55 6 /tmp/shell-trunc.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("stat /tmp/shell-trunc.txt", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "uid=55") ||
        !contains_text(g_shell_capture, "gid=6")) return 0;
    shell_capture_reset();
    if (shell_execute_line("chown bad 6 /tmp/shell-trunc.txt", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "chown: usage UID GID PATH\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("chmod 0600 /tmp/shell-trunc.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("umask", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "0022\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("umask 0077", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) {
        (void)process_umask_set(process_current(), old_umask);
        return 0;
    }
    shell_capture_reset();
    if (shell_execute_line("umask", shell_capture_line) != 1) {
        (void)process_umask_set(process_current(), old_umask);
        return 0;
    }
    if (!streq(g_shell_capture, "0077\n")) {
        (void)process_umask_set(process_current(), old_umask);
        return 0;
    }
    shell_capture_reset();
    if (shell_execute_line("touch /tmp/shell-umask.txt", shell_capture_line) != 1) {
        (void)process_umask_set(process_current(), old_umask);
        return 0;
    }
    if (g_shell_capture[0] != 0) {
        (void)process_umask_set(process_current(), old_umask);
        return 0;
    }
    shell_capture_reset();
    if (shell_execute_line("stat /tmp/shell-umask.txt", shell_capture_line) != 1) {
        (void)process_umask_set(process_current(), old_umask);
        return 0;
    }
    if (!contains_text(g_shell_capture, "mode=0600")) {
        (void)process_umask_set(process_current(), old_umask);
        return 0;
    }
    if (process_umask_set(process_current(), old_umask) < 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("df /", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "FS BS BLOCKS FREE FILES FFREE\n") ||
        !contains_text(g_shell_capture, "ramfs ")) return 0;
    shell_capture_reset();
    if (shell_execute_line("df /disk", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "simplefs ")) return 0;
    shell_capture_reset();
    if (shell_execute_line("umount /disk", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("df /disk", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "ramfs ")) return 0;
    shell_capture_reset();
    if (shell_execute_line("mountfs ram0 /disk", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "mountfs: mounted on /disk\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("truncate /tmp/shell-trunc.txt bad", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "truncate: usage PATH SIZE\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("echo nope > /proc/meminfo", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "redirect: open failed\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("ps", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "PID PPID PGID SID UID GID STATE ARGC CMD\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("proc 0", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "Name: kernel\n") ||
        !contains_text(g_shell_capture, "Pid: 0\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("proc self", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "Name: kernel\n") ||
        !contains_text(g_shell_capture, "Pid: 0\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("proc 0 cmdline", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "kernel\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("proc 0 exe", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "kernel\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("proc 0 stat", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "PID PPID PGID SID UID GID STATE HEAP ARGC ENVC FD NAME\n") ||
        !contains_text(g_shell_capture, "kernel\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("proc self maps", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "START END FLAGS NAME\n") ||
        !contains_text(g_shell_capture, "Kernel: inaccessible\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("proc 0 nope", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "proc: usage PID [status|stat|cmdline|children|exe|cwd|fds|maps|environ]\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("cat /proc/self/cwd", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "/home/root\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("tasks", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "TID PID STATE PRIO CR3 NAME\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("prio 0", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "0 10\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("prio 0 12", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("prio 0", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "0 12\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("prio 0 10", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("stty", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "echo ") ||
        !contains_text(g_shell_capture, "canon ")) return 0;
    shell_capture_reset();
    if (shell_execute_line("stty size", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "25 80\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("stty raw", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0 || tty_get_mode() != 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("stty sane", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0 ||
        tty_get_mode() != (TTY_MODE_ECHO | TTY_MODE_CANON)) return 0;
    shell_capture_reset();
    if (shell_execute_line("sched", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "CurrentTask: ") ||
        !contains_text(g_shell_capture, "TID PID STATE PRIO CR3 SLEEP NAME\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("drivers", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "platform")) return 0;
    shell_capture_reset();
    if (shell_execute_line("lsblk", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "NAME SECTORS SIZE\n") ||
        !contains_text(g_shell_capture, "ram0 ")) return 0;
    shell_capture_reset();
    if (shell_execute_line("route", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "IDX DEST MASK GATEWAY IFACE\n") ||
        !contains_text(g_shell_capture, "lo0\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("route add 172.16.0.0 255.255.0.0 0.0.0.0 lo0", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "route: added\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("route add 10.2.0.0 0.255.0.255 0.0.0.0 lo0", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "route: failed\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("route", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "172.16.0.0 255.255.0.0 0.0.0.0 lo0\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("route del 172.16.0.0 255.255.0.0 0.0.0.0 lo0", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "route: deleted\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("route del 172.16.0.0 255.255.0.0 0.0.0.0 lo0", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "route: failed\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("arp", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "IDX IFACE IPV4 MAC\n") ||
        !contains_text(g_shell_capture, "lo0 127.0.0.1")) return 0;
    shell_capture_reset();
    if (shell_execute_line("arp add lo0 172.16.0.1 02:00:00:00:00:44", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "arp: added\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("arp add lo0 172.16.0.2 bad-mac", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "arp: usage add IFACE IP MAC\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("arp", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "lo0 172.16.0.1 02:00:00:00:00:44")) return 0;
    shell_capture_reset();
    if (shell_execute_line("arp del lo0 172.16.0.1", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "arp: deleted\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("arp del lo0 172.16.0.1", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "arp: failed\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("ifconfig lo0", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "lo0 ether 00:00:00:00:00:01 inet 127.0.0.1 up")) return 0;
    shell_capture_reset();
    if (shell_execute_line("netstat", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "IDX NAME IPV4 STATE MTU TX RX\n") ||
        !contains_text(g_shell_capture, "lo0 127.0.0.1 up")) return 0;
    shell_capture_reset();
    if (shell_execute_line("netstat -r", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "IDX DEST MASK GATEWAY IFACE\n") ||
        !contains_text(g_shell_capture, "lo0\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("netstat -u", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "VFD PROTO LOCAL PEER\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("netstat -s", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "RXQueue: ") ||
        !contains_text(g_shell_capture, "UDPSockets: ")) return 0;
    shell_capture_reset();
    if (shell_execute_line("netstat bad", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "netstat: usage [-i|-r|-u|-s|-a]\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("blkread 0x 0", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "blkread: unknown device")) return 0;
    shell_capture_reset();
    if (shell_execute_line("blkread ram0 0 trailing", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "blkread: bad lba")) return 0;
    shell_capture_reset();
    if (shell_execute_line("lspci", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] == 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("exceptions", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "VECTOR COUNT\n") ||
        !contains_text(g_shell_capture, "LastVector: ")) return 0;
    shell_capture_reset();
    if (shell_execute_line("vm", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "UserBase: 0x08000000\n") ||
        !contains_text(g_shell_capture, "UserMmapBase: 0x70000000\n") ||
        !contains_text(g_shell_capture, "KernelUserAccess: no\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("exec /bin/hello one two", shell_capture_line) != 1) return 0;
    if (!memeq(g_shell_capture, "spawned pid ", 12)) return 0;
    for (i = 0; i < PROCESS_MAX; i++) {
        const process_t *proc = process_at(i);
        if (proc && streq(proc->name, "/bin/hello") && proc->argc == 3) {
            exec_pid = proc->pid;
            break;
        }
    }
    if (exec_pid == 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("jobs", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "PID PGID STATE CMD\n") ||
        !contains_text(g_shell_capture, "/bin/hello\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("pgrp 0", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "0 0 0\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("pgrp bad", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "pgrp: usage PID [PGID]\n")) return 0;
    if (process_kill(exec_pid, -9) < 0) return 0;
    if (process_wait(exec_pid, &exec_code) != 1 || exec_code != -9) return 0;
    wait_proc = process_spawn_path("/bin/hello");
    if (!wait_proc) return 0;
    wait_pid = wait_proc->pid;
    if (process_kill(wait_pid, -9) < 0) return 0;
    append_str(wait_cmd, &wait_pos, sizeof(wait_cmd), "wait ");
    append_dec(wait_cmd, &wait_pos, sizeof(wait_cmd), wait_pid);
    shell_capture_reset();
    if (shell_execute_line(wait_cmd, shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, " exit -9\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("wait 123abc", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "wait: bad pid\n")) return 0;
    wait_proc = process_spawn_path("/bin/hello");
    if (!wait_proc) return 0;
    wait_pid = wait_proc->pid;
    if (process_kill(wait_pid, -9) < 0) return 0;
    shell_capture_reset();
    if (shell_execute_line("wait", shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, "reaped pid ") ||
        !contains_text(g_shell_capture, " exit -9\n") ||
        process_find(wait_pid) != 0) return 0;
    kill_proc = process_spawn_path("/bin/hello");
    if (!kill_proc) return 0;
    kill_pid = kill_proc->pid;
    append_str(kill_cmd, &kill_pos, sizeof(kill_cmd), "kill -15 ");
    append_dec(kill_cmd, &kill_pos, sizeof(kill_cmd), kill_pid);
    shell_capture_reset();
    if (shell_execute_line(kill_cmd, shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "kill: terminated\n")) return 0;
    wait_pos = 0;
    wait_cmd[0] = 0;
    append_str(wait_cmd, &wait_pos, sizeof(wait_cmd), "wait ");
    append_dec(wait_cmd, &wait_pos, sizeof(wait_cmd), kill_pid);
    shell_capture_reset();
    if (shell_execute_line(wait_cmd, shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, " exit -15\n")) return 0;
    group_proc = process_spawn_path("/bin/hello");
    if (!group_proc) return 0;
    group_pid = group_proc->pid;
    kill_pos = 0;
    kill_cmd[0] = 0;
    append_str(kill_cmd, &kill_pos, sizeof(kill_cmd), "kill -15 -");
    append_dec(kill_cmd, &kill_pos, sizeof(kill_cmd), group_pid);
    shell_capture_reset();
    if (shell_execute_line(kill_cmd, shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "kill: terminated\n")) return 0;
    wait_pos = 0;
    wait_cmd[0] = 0;
    append_str(wait_cmd, &wait_pos, sizeof(wait_cmd), "wait ");
    append_dec(wait_cmd, &wait_pos, sizeof(wait_cmd), group_pid);
    shell_capture_reset();
    if (shell_execute_line(wait_cmd, shell_capture_line) != 1) return 0;
    if (!contains_text(g_shell_capture, " exit -15\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("kill -0 1", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "kill: bad signal\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("udp 9000hello", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "udp: usage [IP] PORT MESSAGE\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("udp 9000 hello", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "udp 9001: hello\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("udp 127.0.0.1 9002 hi", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "udp 9003: hi\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("mountfs ram0 /mnt", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "mountfs: mounted on /mnt\n")) return 0;
    if (vfs_stat("/mnt/welcome.txt", &ent) < 0 || ent.type != VFS_NODE_FILE) return 0;
    fd = vfs_open("/proc/mounts", VFS_O_RDONLY);
    if (fd < 0) return 0;
    n = vfs_read(fd, mountbuf, sizeof(mountbuf) - 1);
    vfs_close(fd);
    if (n <= 0) return 0;
    mountbuf[n] = 0;
    if (!contains_text(mountbuf, "simplefs /mnt simplefs rw\n")) return 0;
    shell_capture_reset();
    if (shell_execute_line("mountfs ram0 /disk", shell_capture_line) != 1) return 0;
    if (!streq(g_shell_capture, "mountfs: mounted on /disk\n")) return 0;
    fd = vfs_open("/tmp/shell-mv.txt", VFS_O_CREAT | VFS_O_RDWR);
    if (fd < 0) return 0;
    if (vfs_write(fd, "x", 1) != 1) {
        vfs_close(fd);
        return 0;
    }
    vfs_close(fd);
    shell_capture_reset();
    if (shell_execute_line("mv /tmp/shell-mv.txt /tmp/shell-moved.txt", shell_capture_line) != 1) return 0;
    if (g_shell_capture[0] != 0) return 0;
    if (vfs_stat("/tmp/shell-mv.txt", &ent) == 0) return 0;
    if (vfs_stat("/tmp/shell-moved.txt", &ent) < 0 || ent.size != 1) return 0;
    (void)vfs_unlink("/tmp/shell-moved.txt");
    (void)vfs_unlink("/tmp/shell-link.txt");
    (void)vfs_unlink("/tmp/shell-pwrite.txt");
    (void)vfs_unlink("/tmp/shell-redir.txt");
    (void)vfs_unlink("/tmp/shell-trunc.txt");
    (void)vfs_unlink("/tmp/shell-pipe.txt");
    (void)vfs_unlink("/tmp/shell-sort.txt");
    (void)vfs_unlink("/tmp/shell-umask.txt");
    (void)vfs_unlink("/tmp/shell-rmdir");
    (void)process_env_unset(process_current(), "SHELLTEST");
    (void)process_umask_set(process_current(), old_umask);
    return 1;
}

static int test_procfs_observability(void) {
    char buf[160];
    char sysbuf[4096];
    char procbuf[160];
    char envbuf[320];
    char uptimebuf[160];
    char statbuf[320];
    char versionbuf[160];
    char schedbuf[320];
    char netbuf[160];
    char netudpbuf[160];
    char netsnmpbuf[160];
    char netroutebuf[240];
    char netarpbuf[240];
    char socketsbuf[160];
    char blockbuf[240];
    char vmbuf[512];
    char excbuf[320];
    char routesbuf[240];
    char arpbuf[240];
    char ttybuf[160];
    char pid_status[320];
    char pid_maps[320];
    char pid_fds[160];
    char pid_environ[320];
    vfs_dirent_t ent;
    int fd = vfs_open("/proc/meminfo", VFS_O_RDONLY);
    int n;
    int sys_fd;
    int sys_n;
    int proc_fd;
    int proc_n;
    int env_fd;
    int env_n;
    int uptime_fd;
    int uptime_n;
    int stat_fd;
    int stat_n;
    int version_fd;
    int version_n;
    int sched_fd;
    int sched_n;
    int net_fd;
    int net_n;
    int netudp_fd;
    int netudp_n;
    int netsnmp_fd;
    int netsnmp_n;
    int netroute_fd;
    int netroute_n;
    int netarp_fd;
    int netarp_n;
    int sockets_fd;
    int sockets_n;
    int block_fd;
    int block_n;
    int vm_fd;
    int vm_n;
    int exc_fd;
    int exc_n;
    int routes_fd;
    int routes_n;
    int arp_fd;
    int arp_n;
    int tty_fd;
    int tty_n;
    int pid_status_fd;
    int pid_status_n;
    int pid_maps_fd;
    int pid_maps_n;
    int pid_fds_fd;
    int pid_fds_n;
    int pid_environ_fd;
    int pid_environ_n;
    if (fd < 0) return 0;
    n = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (n <= 0) return 0;
    buf[n] = 0;
    sys_fd = vfs_open("/proc/syscalls", VFS_O_RDONLY);
    if (sys_fd < 0) return 0;
    sys_n = vfs_read(sys_fd, sysbuf, sizeof(sysbuf) - 1);
    vfs_close(sys_fd);
    if (sys_n <= 0) return 0;
    sysbuf[sys_n] = 0;
    proc_fd = vfs_open("/proc/processes", VFS_O_RDONLY);
    if (proc_fd < 0) return 0;
    proc_n = vfs_read(proc_fd, procbuf, sizeof(procbuf) - 1);
    vfs_close(proc_fd);
    if (proc_n <= 0) return 0;
    procbuf[proc_n] = 0;
    env_fd = vfs_open("/proc/env", VFS_O_RDONLY);
    if (env_fd < 0) return 0;
    env_n = vfs_read(env_fd, envbuf, sizeof(envbuf) - 1);
    vfs_close(env_fd);
    if (env_n <= 0) return 0;
    envbuf[env_n] = 0;
    uptime_fd = vfs_open("/proc/uptime", VFS_O_RDONLY);
    if (uptime_fd < 0) return 0;
    uptime_n = vfs_read(uptime_fd, uptimebuf, sizeof(uptimebuf) - 1);
    vfs_close(uptime_fd);
    if (uptime_n <= 0) return 0;
    uptimebuf[uptime_n] = 0;
    stat_fd = vfs_open("/proc/stat", VFS_O_RDONLY);
    if (stat_fd < 0) return 0;
    stat_n = vfs_read(stat_fd, statbuf, sizeof(statbuf) - 1);
    vfs_close(stat_fd);
    if (stat_n <= 0) return 0;
    statbuf[stat_n] = 0;
    version_fd = vfs_open("/proc/version", VFS_O_RDONLY);
    if (version_fd < 0) return 0;
    version_n = vfs_read(version_fd, versionbuf, sizeof(versionbuf) - 1);
    vfs_close(version_fd);
    if (version_n <= 0) return 0;
    versionbuf[version_n] = 0;
    sched_fd = vfs_open("/proc/sched", VFS_O_RDONLY);
    if (sched_fd < 0) return 0;
    sched_n = vfs_read(sched_fd, schedbuf, sizeof(schedbuf) - 1);
    vfs_close(sched_fd);
    if (sched_n <= 0) return 0;
    schedbuf[sched_n] = 0;
    net_fd = vfs_open("/proc/net/dev", VFS_O_RDONLY);
    if (net_fd < 0) return 0;
    net_n = vfs_read(net_fd, netbuf, sizeof(netbuf) - 1);
    vfs_close(net_fd);
    if (net_n <= 0) return 0;
    netbuf[net_n] = 0;
    netudp_fd = vfs_open("/proc/net/udp", VFS_O_RDONLY);
    if (netudp_fd < 0) return 0;
    netudp_n = vfs_read(netudp_fd, netudpbuf, sizeof(netudpbuf) - 1);
    vfs_close(netudp_fd);
    if (netudp_n <= 0) return 0;
    netudpbuf[netudp_n] = 0;
    netsnmp_fd = vfs_open("/proc/net/snmp", VFS_O_RDONLY);
    if (netsnmp_fd < 0) return 0;
    netsnmp_n = vfs_read(netsnmp_fd, netsnmpbuf, sizeof(netsnmpbuf) - 1);
    vfs_close(netsnmp_fd);
    if (netsnmp_n <= 0) return 0;
    netsnmpbuf[netsnmp_n] = 0;
    netroute_fd = vfs_open("/proc/net/route", VFS_O_RDONLY);
    if (netroute_fd < 0) return 0;
    netroute_n = vfs_read(netroute_fd, netroutebuf, sizeof(netroutebuf) - 1);
    vfs_close(netroute_fd);
    if (netroute_n <= 0) return 0;
    netroutebuf[netroute_n] = 0;
    netarp_fd = vfs_open("/proc/net/arp", VFS_O_RDONLY);
    if (netarp_fd < 0) return 0;
    netarp_n = vfs_read(netarp_fd, netarpbuf, sizeof(netarpbuf) - 1);
    vfs_close(netarp_fd);
    if (netarp_n <= 0) return 0;
    netarpbuf[netarp_n] = 0;
    sockets_fd = vfs_open("/proc/sockets", VFS_O_RDONLY);
    if (sockets_fd < 0) return 0;
    sockets_n = vfs_read(sockets_fd, socketsbuf, sizeof(socketsbuf) - 1);
    vfs_close(sockets_fd);
    if (sockets_n <= 0) return 0;
    socketsbuf[sockets_n] = 0;
    block_fd = vfs_open("/proc/block", VFS_O_RDONLY);
    if (block_fd < 0) return 0;
    block_n = vfs_read(block_fd, blockbuf, sizeof(blockbuf) - 1);
    vfs_close(block_fd);
    if (block_n <= 0) return 0;
    blockbuf[block_n] = 0;
    vm_fd = vfs_open("/proc/vm", VFS_O_RDONLY);
    if (vm_fd < 0) return 0;
    vm_n = vfs_read(vm_fd, vmbuf, sizeof(vmbuf) - 1);
    vfs_close(vm_fd);
    if (vm_n <= 0) return 0;
    vmbuf[vm_n] = 0;
    exc_fd = vfs_open("/proc/exceptions", VFS_O_RDONLY);
    if (exc_fd < 0) return 0;
    exc_n = vfs_read(exc_fd, excbuf, sizeof(excbuf) - 1);
    vfs_close(exc_fd);
    if (exc_n <= 0) return 0;
    excbuf[exc_n] = 0;
    routes_fd = vfs_open("/proc/routes", VFS_O_RDONLY);
    if (routes_fd < 0) return 0;
    routes_n = vfs_read(routes_fd, routesbuf, sizeof(routesbuf) - 1);
    vfs_close(routes_fd);
    if (routes_n <= 0) return 0;
    routesbuf[routes_n] = 0;
    arp_fd = vfs_open("/proc/arp", VFS_O_RDONLY);
    if (arp_fd < 0) return 0;
    arp_n = vfs_read(arp_fd, arpbuf, sizeof(arpbuf) - 1);
    vfs_close(arp_fd);
    if (arp_n <= 0) return 0;
    arpbuf[arp_n] = 0;
    tty_fd = vfs_open("/proc/tty", VFS_O_RDONLY);
    if (tty_fd < 0) return 0;
    tty_n = vfs_read(tty_fd, ttybuf, sizeof(ttybuf) - 1);
    vfs_close(tty_fd);
    if (tty_n <= 0) return 0;
    ttybuf[tty_n] = 0;
    pid_status_fd = vfs_open("/proc/0/status", VFS_O_RDONLY);
    if (pid_status_fd < 0) return 0;
    pid_status_n = vfs_read(pid_status_fd, pid_status, sizeof(pid_status) - 1);
    vfs_close(pid_status_fd);
    if (pid_status_n <= 0) return 0;
    pid_status[pid_status_n] = 0;
    pid_maps_fd = vfs_open("/proc/0/maps", VFS_O_RDONLY);
    if (pid_maps_fd < 0) return 0;
    pid_maps_n = vfs_read(pid_maps_fd, pid_maps, sizeof(pid_maps) - 1);
    vfs_close(pid_maps_fd);
    if (pid_maps_n <= 0) return 0;
    pid_maps[pid_maps_n] = 0;
    pid_fds_fd = vfs_open("/proc/0/fds", VFS_O_RDONLY);
    if (pid_fds_fd < 0) return 0;
    pid_fds_n = vfs_read(pid_fds_fd, pid_fds, sizeof(pid_fds) - 1);
    vfs_close(pid_fds_fd);
    if (pid_fds_n <= 0) return 0;
    pid_fds[pid_fds_n] = 0;
    pid_environ_fd = vfs_open("/proc/0/environ", VFS_O_RDONLY);
    if (pid_environ_fd < 0) return 0;
    pid_environ_n = vfs_read(pid_environ_fd, pid_environ, sizeof(pid_environ) - 1);
    vfs_close(pid_environ_fd);
    if (pid_environ_n <= 0) return 0;
    pid_environ[pid_environ_n] = 0;
    return vfs_stat("/proc/tasks", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/fds", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/env", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/interrupts", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/version", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/stat", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/sched", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/exceptions", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/net", &ent) == 0 &&
           ent.type == VFS_NODE_DIR &&
           dir_has_entry("/proc/net", "dev", VFS_NODE_FILE) &&
           dir_has_entry("/proc/net", "route", VFS_NODE_FILE) &&
           dir_has_entry("/proc/net", "arp", VFS_NODE_FILE) &&
           dir_has_entry("/proc/net", "udp", VFS_NODE_FILE) &&
           dir_has_entry("/proc/net", "snmp", VFS_NODE_FILE) &&
           vfs_stat("/proc/net/dev", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/net/udp", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/net/snmp", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/net/route", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/net/arp", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/sockets", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/drivers", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/block", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/pci", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/filesystems", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/syscalls", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/vm", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/routes", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/arp", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/tty", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           dir_has_entry("/proc", "self", VFS_NODE_DIR) &&
           vfs_stat("/proc/0", &ent) == 0 &&
           ent.type == VFS_NODE_DIR &&
           vfs_stat("/proc/0/status", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/0/environ", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/0/cwd", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/0/cmdline", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/0/stat", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/0/children", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/0/exe", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/self", &ent) == 0 &&
           ent.type == VFS_NODE_DIR &&
           vfs_stat("/proc/self/status", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/self/cwd", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/self/cmdline", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/self/stat", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/self/children", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_stat("/proc/self/exe", &ent) == 0 &&
           ent.type == VFS_NODE_FILE &&
           ent.size > 0 &&
           vfs_readdir("/proc/0", 0, &ent) == 0 &&
           streq(ent.name, "status") &&
           vfs_readdir("/proc/0", 1, &ent) == 0 &&
           streq(ent.name, "fds") &&
           vfs_readdir("/proc/0", 2, &ent) == 0 &&
           streq(ent.name, "environ") &&
           vfs_readdir("/proc/0", 3, &ent) == 0 &&
           streq(ent.name, "maps") &&
           vfs_readdir("/proc/0", 4, &ent) == 0 &&
           streq(ent.name, "cwd") &&
           vfs_readdir("/proc/0", 5, &ent) == 0 &&
           streq(ent.name, "cmdline") &&
           vfs_readdir("/proc/0", 6, &ent) == 0 &&
           streq(ent.name, "stat") &&
           vfs_readdir("/proc/self", 0, &ent) == 0 &&
           streq(ent.name, "status") &&
           vfs_readdir("/proc/self", 6, &ent) == 0 &&
           streq(ent.name, "stat") &&
           vfs_readdir("/proc/self", 7, &ent) == 0 &&
           streq(ent.name, "children") &&
           vfs_readdir("/proc/self", 8, &ent) == 0 &&
           streq(ent.name, "exe") &&
           contains_text(sysbuf, "24 spawn_args\n") &&
           contains_text(sysbuf, "29 udp_recv4\n") &&
           contains_text(sysbuf, "40 pci_info\n") &&
           contains_text(sysbuf, "44 simplefs_mount\n") &&
           contains_text(sysbuf, "48 arp_add4\n") &&
           contains_text(sysbuf, "52 arp_info\n") &&
           contains_text(sysbuf, "53 fstat\n") &&
           contains_text(sysbuf, "55 munmap\n") &&
           contains_text(sysbuf, "56 mprotect\n") &&
           contains_text(sysbuf, "57 truncate\n") &&
           contains_text(sysbuf, "58 ftruncate\n") &&
           contains_text(sysbuf, "60 task_priority\n") &&
           contains_text(sysbuf, "61 statfs\n") &&
           contains_text(sysbuf, "62 poll\n") &&
           contains_text(sysbuf, "63 dup2\n") &&
           contains_text(sysbuf, "65 tty_set_mode\n") &&
           contains_text(sysbuf, "66 ioctl\n") &&
           contains_text(sysbuf, "67 clock_gettime\n") &&
           contains_text(sysbuf, "68 clock_ms\n") &&
           contains_text(sysbuf, "69 chmod\n") &&
           contains_text(sysbuf, "70 access\n") &&
           contains_text(sysbuf, "71 uname\n") &&
           contains_text(sysbuf, "72 sysconf\n") &&
           contains_text(sysbuf, "73 wait_any\n") &&
           contains_text(sysbuf, "74 getenv\n") &&
           contains_text(sysbuf, "75 setenv\n") &&
           contains_text(sysbuf, "76 unsetenv\n") &&
           contains_text(sysbuf, "77 env_count\n") &&
           contains_text(sysbuf, "78 env_info\n") &&
           contains_text(sysbuf, "79 poll_many\n") &&
           contains_text(sysbuf, "80 gethostname\n") &&
           contains_text(sysbuf, "81 sethostname\n") &&
           contains_text(sysbuf, "82 umask\n") &&
           contains_text(sysbuf, "83 rmdir\n") &&
           contains_text(sysbuf, "84 fchmod\n") &&
           contains_text(sysbuf, "85 symlink\n") &&
           contains_text(sysbuf, "86 readlink\n") &&
           contains_text(sysbuf, "87 lstat\n") &&
           contains_text(sysbuf, "88 pread\n") &&
           contains_text(sysbuf, "89 pwrite\n") &&
           contains_text(sysbuf, "90 readv\n") &&
           contains_text(sysbuf, "91 writev\n") &&
           contains_text(sysbuf, "92 fcntl\n") &&
           contains_text(sysbuf, "93 utime\n") &&
           contains_text(sysbuf, "94 futime\n") &&
           contains_text(sysbuf, "95 simplefs_unmount\n") &&
           contains_text(sysbuf, "96 fsync\n") &&
           contains_text(sysbuf, "97 fdatasync\n") &&
           contains_text(sysbuf, "98 sync\n") &&
           contains_text(sysbuf, "99 fchdir\n") &&
           contains_text(sysbuf, "100 freaddir\n") &&
           contains_text(sysbuf, "101 time\n") &&
           contains_text(sysbuf, "102 gettimeofday\n") &&
           contains_text(sysbuf, "103 nanosleep\n") &&
           contains_text(sysbuf, "104 getrandom\n") &&
           contains_text(sysbuf, "105 openat\n") &&
           contains_text(sysbuf, "106 fstatat\n") &&
           contains_text(sysbuf, "107 mkdirat\n") &&
           contains_text(sysbuf, "108 unlinkat\n") &&
           contains_text(sysbuf, "109 renameat\n") &&
           contains_text(sysbuf, "110 accessat\n") &&
           contains_text(sysbuf, "111 route_del4\n") &&
           contains_text(sysbuf, "112 arp_del4\n") &&
           contains_text(sysbuf, "113 socket_udp4\n") &&
           contains_text(sysbuf, "114 bind_udp4\n") &&
           contains_text(sysbuf, "115 sendto_udp4\n") &&
           contains_text(sysbuf, "116 recvfrom_udp4\n") &&
           contains_text(sysbuf, "117 connect_udp4\n") &&
           contains_text(sysbuf, "118 getsockname_udp4\n") &&
           contains_text(sysbuf, "119 getpeername_udp4\n") &&
           contains_text(sysbuf, "120 select\n") &&
           contains_text(sysbuf, "121 getuid\n") &&
           contains_text(sysbuf, "122 getgid\n") &&
           contains_text(sysbuf, "123 setuid\n") &&
           contains_text(sysbuf, "124 setgid\n") &&
           contains_text(sysbuf, "125 chown\n") &&
           contains_text(sysbuf, "126 fchown\n") &&
           contains_text(sysbuf, "127 exec\n") &&
           contains_text(sysbuf, "128 exec_args\n") &&
           contains_text(sysbuf, "129 fork\n") &&
           contains_text(sysbuf, "130 pipe2\n") &&
           contains_text(sysbuf, "131 kill_signal\n") &&
           contains_text(sysbuf, "132 getpgid\n") &&
           contains_text(sysbuf, "133 setpgid\n") &&
           contains_text(sysbuf, "134 getsid\n") &&
           contains_text(sysbuf, "135 setsid\n") &&
           contains_text(sysbuf, "136 kill_group\n") &&
           contains_text(buf, "HeapFreeBytes: ") &&
           contains_text(buf, "MemManagedPages: ") &&
           contains_text(buf, "MemManagedStart: 0x") &&
           contains_text(buf, "MemManagedEnd: 0x") &&
           contains_text(buf, "HeapUsedBytes: ") &&
           contains_text(buf, "HeapBlocks: ") &&
           contains_text(buf, "HeapFreeBlocks: ") &&
           contains_text(procbuf, "PID PPID PGID SID UID GID STATE HEAP ARGC ENVC NAME\n") &&
           contains_text(envbuf, "PATH=/bin\n") &&
           contains_text(envbuf, "HOME=/home/root\n") &&
           contains_text(uptimebuf, "TimerHz: ") &&
           contains_text(uptimebuf, "UptimeMs: ") &&
           contains_text(uptimebuf, "QuantumTicks: ") &&
           contains_text(uptimebuf, "PreemptPending: ") &&
           contains_text(statbuf, "cpu ") &&
           contains_text(statbuf, "intr ") &&
           contains_text(statbuf, "ctxt ") &&
           contains_text(statbuf, "processes ") &&
           contains_text(statbuf, "procs_running ") &&
           contains_text(statbuf, "tasks ") &&
           contains_text(versionbuf, "MyOS 0.1 kernel-dev i386\n") &&
           contains_text(versionbuf, "NodeName: myos-machine\n") &&
           contains_text(schedbuf, "CurrentTask: ") &&
           contains_text(schedbuf, "TID PID STATE PRIO CR3 SLEEP NAME\n") &&
           contains_text(excbuf, "LastVector: ") &&
           contains_text(excbuf, "LastEIP: 0x") &&
           contains_text(excbuf, "LastCR2: 0x") &&
           contains_text(netbuf, "IDX NAME IPV4 STATE MTU TX RX\n") &&
           contains_text(netbuf, "lo0 127.0.0.1") &&
           contains_text(netudpbuf, "VFD PROTO LOCAL PEER\n") &&
           contains_text(netsnmpbuf, "RXQueue: ") &&
           contains_text(netsnmpbuf, "UDPSockets: ") &&
           contains_text(netroutebuf, "IDX DEST MASK GATEWAY IFACE\n") &&
           contains_text(netroutebuf, "lo0\n") &&
           contains_text(netarpbuf, "IDX IFACE IPV4 MAC\n") &&
           contains_text(netarpbuf, "lo0 127.0.0.1") &&
           contains_text(routesbuf, "IDX DEST MASK GATEWAY IFACE\n") &&
           contains_text(routesbuf, "lo0\n") &&
           contains_text(arpbuf, "IDX IFACE IPV4 MAC\n") &&
           contains_text(arpbuf, "lo0 127.0.0.1") &&
           contains_text(ttybuf, "Mode: ") &&
           contains_text(ttybuf, "Echo: ") &&
           contains_text(ttybuf, "Canonical: ") &&
           contains_text(ttybuf, "Rows: 25\n") &&
           contains_text(ttybuf, "Columns: 80\n") &&
           contains_text(pid_status, "Name: kernel\n") &&
           contains_text(pid_status, "Pid: 0\n") &&
           contains_text(pid_status, "Uid: 0\n") &&
           contains_text(pid_status, "Gid: 0\n") &&
           contains_text(pid_status, "EnvCount: ") &&
           contains_text(pid_status, "Umask: ") &&
           contains_text(pid_status, "PGid: ") &&
           contains_text(pid_status, "Sid: ") &&
           contains_text(pid_status, "PageDirectory: 0x") &&
           contains_text(pid_environ, "PATH=/bin\n") &&
           contains_text(pid_environ, "HOME=/home/root\n") &&
           file_text_contains("/proc/0/cwd", "/home/root\n") &&
           file_text_contains("/proc/0/cmdline", "kernel\n") &&
           file_text_contains("/proc/0/exe", "kernel\n") &&
           file_text_contains("/proc/0/stat", "PID PPID PGID SID UID GID STATE HEAP ARGC ENVC FD NAME\n") &&
           file_text_contains("/proc/0/stat", "kernel\n") &&
           file_text_contains("/proc/self/status", "Name: kernel\n") &&
           file_text_contains("/proc/self/status", "Pid: 0\n") &&
           file_text_contains("/proc/self/cwd", "/home/root\n") &&
           file_text_contains("/proc/self/cmdline", "kernel\n") &&
           file_text_contains("/proc/self/exe", "kernel\n") &&
           file_text_contains("/proc/self/stat", "PID PPID PGID SID UID GID STATE HEAP ARGC ENVC FD NAME\n") &&
           file_text_contains("/proc/self/stat", "kernel\n") &&
           contains_text(pid_maps, "UserMmapBase: 0x70000000\n") &&
           contains_text(pid_maps, "UserMmapTop: 0x7FFFC000\n") &&
           contains_text(pid_maps, "Kernel: inaccessible\n") &&
           contains_text(pid_fds, "FD VFSFD FLAGS\n") &&
           contains_text(netbuf, "RXQueue: ") &&
           contains_text(netbuf, "UDPQueue: ") &&
           contains_text(netbuf, "UDPSockets: ") &&
           contains_text(netbuf, "UDPChecksum: required\n") &&
           contains_text(socketsbuf, "VFD PROTO LOCAL PEER\n") &&
           contains_text(blockbuf, "IDX NAME SECTORS SIZE RW ROPS WOPS RSECT WSECT\n") &&
           contains_text(blockbuf, "ram0 ") &&
           contains_text(vmbuf, "KernelDirectory: 0x") &&
           contains_text(vmbuf, "CurrentDirectory: 0x") &&
           contains_text(vmbuf, "UserBase: 0x08000000\n") &&
           contains_text(vmbuf, "UserTop: 0x80000000\n") &&
           contains_text(vmbuf, "UserMmapBase: 0x70000000\n") &&
           contains_text(vmbuf, "UserMmapTop: 0x7FFFC000\n") &&
           contains_text(vmbuf, "UserStackSize: 0x00004000\n") &&
           contains_text(vmbuf, "KernelUserAccess: no\n") &&
           vfs_open("/proc/new", VFS_O_CREAT | VFS_O_RDWR) < 0 &&
           vfs_unlink("/proc/meminfo") < 0;
}

static void run_case(selftest_write_fn out,
                     const char *name,
                     int (*fn)(void),
                     uint32_t *pass,
                     uint32_t *fail) {
    int ok = fn();
    emit_result(out, name, ok);
    if (ok) (*pass)++;
    else (*fail)++;
}

int selftest_run(selftest_write_fn out) {
    uint32_t pass = 0;
    uint32_t fail = 0;
    char line[96];
    uint32_t pos = 0;
    if (!out) return -1;
    out("selftest: start");
    run_case(out, "vfs-ramfs", test_vfs_ramfs, &pass, &fail);
    run_case(out, "tty-device", test_tty_device, &pass, &fail);
    run_case(out, "basic-char-devices", test_basic_char_devices, &pass, &fail);
    run_case(out, "simplefs-disk", test_simplefs_disk, &pass, &fail);
    run_case(out, "vfs-umask", test_vfs_umask, &pass, &fail);
    run_case(out, "vfs-rmdir", test_vfs_rmdir, &pass, &fail);
    run_case(out, "vfs-fchmod", test_vfs_fchmod, &pass, &fail);
    run_case(out, "vfs-ownership", test_vfs_ownership, &pass, &fail);
    run_case(out, "vfs-symlink", test_vfs_symlink, &pass, &fail);
    run_case(out, "vfs-pread-pwrite", test_vfs_pread_pwrite, &pass, &fail);
    run_case(out, "vfs-utime", test_vfs_utime, &pass, &fail);
    run_case(out, "vfs-sync", test_vfs_sync, &pass, &fail);
    run_case(out, "vfs-dirfd", test_vfs_dirfd, &pass, &fail);
    run_case(out, "block-ram0", test_block_ram0, &pass, &fail);
    run_case(out, "elf-user-image", test_elf_user_image, &pass, &fail);
    run_case(out, "paging-user-map", test_paging_user_map, &pass, &fail);
    run_case(out, "pmm-allocator", test_pmm_allocator, &pass, &fail);
    run_case(out, "kernel-heap", test_kernel_heap, &pass, &fail);
    run_case(out, "process-heap", test_process_heap, &pass, &fail);
    run_case(out, "process-mmap", test_process_mmap, &pass, &fail);
    run_case(out, "process-argv", test_process_argv, &pass, &fail);
    run_case(out, "process-exec-replace", test_process_exec_replace, &pass, &fail);
    run_case(out, "process-fork-user", test_process_fork_user, &pass, &fail);
    run_case(out, "process-env", test_process_env, &pass, &fail);
    run_case(out, "process-umask", test_process_umask, &pass, &fail);
    run_case(out, "user-process-exit", test_user_process_exit, &pass, &fail);
    run_case(out, "user-fault-isolation", test_user_fault_isolation, &pass, &fail);
    run_case(out, "usermode-entry-validation", test_usermode_entry_validation, &pass, &fail);
    run_case(out, "process-table", test_process_table, &pass, &fail);
    run_case(out, "process-fd-table", test_process_fd_table, &pass, &fail);
    run_case(out, "process-cwd", test_process_cwd, &pass, &fail);
    run_case(out, "process-pipe", test_process_pipe, &pass, &fail);
    run_case(out, "process-kill", test_process_kill, &pass, &fail);
    run_case(out, "process-return-exit", test_process_return_exit, &pass, &fail);
    run_case(out, "process-wait-permissions", test_process_wait_permissions, &pass, &fail);
    run_case(out, "process-wait-any", test_process_wait_any, &pass, &fail);
    run_case(out, "process-kill-permissions", test_process_kill_permissions, &pass, &fail);
    run_case(out, "process-groups", test_process_groups, &pass, &fail);
    run_case(out, "process-kill-group", test_process_kill_group, &pass, &fail);
    run_case(out, "process-orphan-reparent", test_process_orphan_reparent, &pass, &fail);
    run_case(out, "syscall-validation", test_syscall_validation, &pass, &fail);
    run_case(out, "syscall-path-copy", test_syscall_path_copy, &pass, &fail);
    run_case(out, "syscall-dirfd", test_syscall_dirfd, &pass, &fail);
    run_case(out, "syscall-at-paths", test_syscall_at_paths, &pass, &fail);
    run_case(out, "syscall-spawn-args", test_syscall_spawn_args, &pass, &fail);
    run_case(out, "syscall-wait-any", test_syscall_wait_any, &pass, &fail);
    run_case(out, "syscall-kill-signal", test_syscall_kill_signal, &pass, &fail);
    run_case(out, "syscall-process-groups", test_syscall_process_groups, &pass, &fail);
    run_case(out, "syscall-kill-group", test_syscall_kill_group, &pass, &fail);
    run_case(out, "syscall-fstat", test_syscall_fstat, &pass, &fail);
    run_case(out, "syscall-mmap", test_syscall_mmap, &pass, &fail);
    run_case(out, "syscall-truncate", test_syscall_truncate, &pass, &fail);
    run_case(out, "syscall-poll-many", test_syscall_poll_many, &pass, &fail);
    run_case(out, "syscall-chmod-access", test_syscall_chmod_access, &pass, &fail);
    run_case(out, "syscall-fchmod", test_syscall_fchmod, &pass, &fail);
    run_case(out, "syscall-credentials", test_syscall_credentials, &pass, &fail);
    run_case(out, "syscall-symlink-readlink", test_syscall_symlink_readlink, &pass, &fail);
    run_case(out, "syscall-pread-pwrite", test_syscall_pread_pwrite, &pass, &fail);
    run_case(out, "syscall-readv-writev", test_syscall_readv_writev, &pass, &fail);
    run_case(out, "syscall-fcntl", test_syscall_fcntl, &pass, &fail);
    run_case(out, "syscall-utime-futime", test_syscall_utime_futime, &pass, &fail);
    run_case(out, "syscall-sync", test_syscall_sync, &pass, &fail);
    run_case(out, "syscall-env", test_syscall_env, &pass, &fail);
    run_case(out, "syscall-hostname", test_syscall_hostname, &pass, &fail);
    run_case(out, "syscall-umask", test_syscall_umask, &pass, &fail);
    run_case(out, "syscall-rmdir", test_syscall_rmdir, &pass, &fail);
    run_case(out, "syscall-task-priority", test_syscall_task_priority, &pass, &fail);
    run_case(out, "syscall-statfs", test_syscall_statfs, &pass, &fail);
    run_case(out, "syscall-tty-mode", test_syscall_tty_mode, &pass, &fail);
    run_case(out, "syscall-clock", test_syscall_clock, &pass, &fail);
    run_case(out, "syscall-getrandom", test_syscall_getrandom, &pass, &fail);
	    run_case(out, "syscall-network", test_syscall_network, &pass, &fail);
    run_case(out, "syscall-udp-socket", test_syscall_udp_socket, &pass, &fail);
    run_case(out, "syscall-network-control", test_syscall_network_control, &pass, &fail);
	    run_case(out, "syscall-observability", test_syscall_observability, &pass, &fail);
    run_case(out, "syscall-block-simplefs", test_syscall_block_simplefs, &pass, &fail);
	    run_case(out, "scheduler-process-state", test_scheduler_process_state, &pass, &fail);
    run_case(out, "scheduler-address-space", test_scheduler_address_space, &pass, &fail);
    run_case(out, "scheduler-task-slots", test_scheduler_task_slots, &pass, &fail);
    run_case(out, "scheduler-task-destroy", test_scheduler_task_destroy, &pass, &fail);
    run_case(out, "scheduler-task-create-metadata", test_scheduler_task_create_metadata, &pass, &fail);
    run_case(out, "scheduler-task-priority", test_scheduler_task_priority, &pass, &fail);
    run_case(out, "scheduler-sleep-zero-state", test_scheduler_sleep_zero_state, &pass, &fail);
    run_case(out, "scheduler-task-reuse", test_scheduler_task_reuse, &pass, &fail);
    run_case(out, "scheduler-irq-preempt", test_scheduler_irq_preemption, &pass, &fail);
    run_case(out, "driver-registry", test_driver_registry, &pass, &fail);
    run_case(out, "pci-registry", test_pci_registry, &pass, &fail);
    run_case(out, "net-loopback", test_net_loopback, &pass, &fail);
    run_case(out, "debug-log", test_debug_log, &pass, &fail);
    run_case(out, "procfs-children", test_procfs_children, &pass, &fail);
    run_case(out, "shell-commands", test_shell_commands, &pass, &fail);
    run_case(out, "procfs-observability", test_procfs_observability, &pass, &fail);
    append_str(line, &pos, sizeof(line), "selftest: pass=");
    append_dec(line, &pos, sizeof(line), pass);
    append_str(line, &pos, sizeof(line), " fail=");
    append_dec(line, &pos, sizeof(line), fail);
    out(line);
    return fail ? -1 : 0;
}
