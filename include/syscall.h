#ifndef SYSCALL_H
#define SYSCALL_H
#include <stdint.h>

/* System call numbers */
#define SYS_YIELD   0
#define SYS_SLEEP   1
#define SYS_EXIT    2
#define SYS_GETPID  3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_READ    6
#define SYS_CLOSE   7
#define SYS_SEEK    8
#define SYS_STAT    9
#define SYS_READDIR 10
#define SYS_SPAWN   11
#define SYS_WAIT    12
#define SYS_MKDIR   13
#define SYS_UNLINK  14
#define SYS_KILL    15
#define SYS_GETPPID 16
#define SYS_DUP     17
#define SYS_CHDIR   18
#define SYS_GETCWD  19
#define SYS_RENAME  20
#define SYS_PIPE    21
#define SYS_SBRK    22
#define SYS_UPTIME  23
#define SYS_SPAWN_ARGS 24
#define SYS_NETIF_COUNT 25
#define SYS_NETIF_INFO  26
#define SYS_NET_PING4   27
#define SYS_UDP_SEND4   28
#define SYS_UDP_RECV4   29
#define SYS_SYSINFO     30
#define SYS_PROCESS_COUNT 31
#define SYS_PROCESS_INFO  32
#define SYS_BLOCK_COUNT   33
#define SYS_BLOCK_INFO    34
#define SYS_DRIVER_COUNT  35
#define SYS_DRIVER_INFO   36
#define SYS_TASK_COUNT    37
#define SYS_TASK_INFO     38
#define SYS_PCI_COUNT     39
#define SYS_PCI_INFO      40
#define SYS_BLOCK_READ    41
#define SYS_BLOCK_WRITE   42
#define SYS_SIMPLEFS_FORMAT 43
#define SYS_SIMPLEFS_MOUNT  44
#define SYS_NETIF_SET_UP    45
#define SYS_NETIF_SET_IPV4  46
#define SYS_ROUTE_ADD4      47
#define SYS_ARP_ADD4        48
#define SYS_ROUTE_COUNT     49
#define SYS_ROUTE_INFO      50
#define SYS_ARP_COUNT       51
#define SYS_ARP_INFO        52
#define SYS_FSTAT           53
#define SYS_MMAP            54
#define SYS_MUNMAP          55
#define SYS_MPROTECT        56
#define SYS_TRUNCATE        57
#define SYS_FTRUNCATE       58
#define SYS_TASK_SET_PRIORITY 59
#define SYS_TASK_PRIORITY     60
#define SYS_STATFS            61
#define SYS_POLL              62
#define SYS_DUP2              63
#define SYS_TTY_GET_MODE      64
#define SYS_TTY_SET_MODE      65
#define SYS_IOCTL             66
#define SYS_CLOCK_GETTIME     67
#define SYS_CLOCK_MS          68
#define SYS_CHMOD             69
#define SYS_ACCESS            70
#define SYS_UNAME             71
#define SYS_SYSCONF           72
#define SYS_WAIT_ANY          73
#define SYS_GETENV            74
#define SYS_SETENV            75
#define SYS_UNSETENV          76
#define SYS_ENV_COUNT         77
#define SYS_ENV_INFO          78
#define SYS_POLL_MANY         79
#define SYS_GETHOSTNAME       80
#define SYS_SETHOSTNAME       81
#define SYS_UMASK             82
#define SYS_RMDIR             83
#define SYS_FCHMOD            84
#define SYS_SYMLINK           85
#define SYS_READLINK          86
#define SYS_LSTAT             87
#define SYS_PREAD             88
#define SYS_PWRITE            89
#define SYS_READV             90
#define SYS_WRITEV            91
#define SYS_FCNTL             92
#define SYS_UTIME             93
#define SYS_FUTIME            94
#define SYS_SIMPLEFS_UNMOUNT  95
#define SYS_FSYNC             96
#define SYS_FDATASYNC         97
#define SYS_SYNC              98
#define SYS_FCHDIR            99
#define SYS_FREADDIR          100
#define SYS_TIME              101
#define SYS_GETTIMEOFDAY      102
#define SYS_NANOSLEEP         103
#define SYS_GETRANDOM         104
#define SYS_OPENAT            105
#define SYS_FSTATAT           106
#define SYS_MKDIRAT           107
#define SYS_UNLINKAT          108
#define SYS_RENAMEAT          109
#define SYS_ACCESSAT          110
#define SYS_ROUTE_DEL4        111
#define SYS_ARP_DEL4          112
#define SYS_SOCKET_UDP4       113
#define SYS_BIND_UDP4         114
#define SYS_SENDTO_UDP4       115
#define SYS_RECVFROM_UDP4     116
#define SYS_CONNECT_UDP4      117
#define SYS_GETSOCKNAME_UDP4  118
#define SYS_GETPEERNAME_UDP4  119
#define SYS_SELECT            120
#define SYS_GETUID            121
#define SYS_GETGID            122
#define SYS_SETUID            123
#define SYS_SETGID            124
#define SYS_CHOWN             125
#define SYS_FCHOWN            126
#define SYS_EXEC              127
#define SYS_EXEC_ARGS         128
#define SYS_FORK              129
#define SYS_PIPE2             130
#define SYS_KILL_SIGNAL       131
#define SYS_GETPGID           132
#define SYS_SETPGID           133
#define SYS_GETSID            134
#define SYS_SETSID            135
#define SYS_KILL_GROUP        136

#define SYS_NET_NAME_MAX 16
#define SYS_NAME_MAX     32
#define SYS_FS_NAME_MAX  16
#define SYS_UTS_FIELD_MAX 32
#define SYS_POLL_MAX     8U
#define SYS_IOV_MAX      8U
#define SYS_CLOCK_UPTIME_MONOTONIC 0U
#define SYS_FCNTL_DUPFD 0U
#define SYS_FCNTL_GETFL 1U
#define SYS_FCNTL_SETFL 2U
#define SYS_FCNTL_GETFD 3U
#define SYS_FCNTL_SETFD 4U
#define SYS_FD_CLOEXEC  0x1U
#define SYS_AT_FDCWD 0xFFFFFF9CU
#define SYS_ACCESS_READ  0x1U
#define SYS_ACCESS_WRITE 0x2U
#define SYS_ACCESS_EXEC  0x4U
#define SYS_CONF_PAGE_SIZE     1U
#define SYS_CONF_PROCESS_MAX   2U
#define SYS_CONF_TASK_MAX      3U
#define SYS_CONF_FD_MAX        4U
#define SYS_CONF_ARG_MAX       5U
#define SYS_CONF_PATH_MAX      6U
#define SYS_CONF_NAME_MAX      7U
#define SYS_CONF_NETIF_MAX     8U
#define SYS_CONF_BLOCK_MAX     9U
#define SYS_CONF_DRIVER_MAX    10U
#define SYS_CONF_TIMER_HZ      11U
#define SYS_CONF_ENV_MAX       12U
#define SYS_CONF_ENV_VALUE_MAX 13U
#define SYS_CONF_UTS_FIELD_MAX 14U

typedef struct {
    uint32_t ticks;
    uint32_t free_pages;
    uint32_t managed_pages;
    uint32_t heap_free_bytes;
    uint32_t heap_used_bytes;
    uint32_t heap_blocks;
    uint32_t heap_free_blocks;
    uint32_t process_count;
    uint32_t task_count;
    uint32_t task_free_slots;
    uint32_t irq_switches;
    uint32_t coop_switches;
    uint32_t pci_count;
    uint32_t block_count;
    uint32_t netif_count;
    uint32_t driver_count;
} sys_sysinfo_t;

typedef struct {
    char name[SYS_NAME_MAX];
    uint32_t pid;
    uint32_t ppid;
    uint32_t state;
    uint32_t entry;
    uint32_t user_stack;
    uint32_t heap_start;
    uint32_t heap_break;
    uint32_t argc;
    uint32_t fd_count;
    uint32_t uid;
    uint32_t gid;
    uint32_t pgid;
    uint32_t sid;
} sys_process_info_t;

typedef struct {
    char name[SYS_NAME_MAX];
    uint32_t id;
    uint32_t state;
    uint32_t priority;
    uint32_t process_id;
    uint32_t sleep_until;
    uint32_t cr3;
    uint32_t started;
} sys_task_info_t;

typedef struct {
    char name[SYS_NAME_MAX];
    uint32_t sector_count;
    uint32_t sector_size;
    uint32_t writable;
    uint32_t read_ops;
    uint32_t write_ops;
    uint32_t read_sectors;
    uint32_t write_sectors;
} sys_block_info_t;

typedef struct {
    char name[SYS_FS_NAME_MAX];
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t total_files;
    uint32_t free_files;
} sys_fsinfo_t;

typedef struct {
    char name[SYS_NAME_MAX];
    uint32_t bus;
    uint32_t id0;
    uint32_t id1;
    uint32_t loaded;
} sys_driver_info_t;

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint32_t bar[6];
} sys_pci_info_t;

typedef struct {
    char name[SYS_NET_NAME_MAX];
    uint8_t mac[6];
    uint32_t ipv4;
    uint32_t mtu;
    uint32_t up;
    uint32_t tx_packets;
    uint32_t rx_packets;
} sys_netif_info_t;

typedef struct {
    uint32_t src_ipv4;
    uint16_t src_port;
} sys_udp_peer_t;

typedef struct {
    uint32_t ipv4;
    uint16_t port;
} sys_sockaddr_in4_t;

typedef struct {
    uint32_t dest;
    uint32_t mask;
    uint32_t gateway;
    uint32_t ifindex;
} sys_route_info_t;

typedef struct {
    uint32_t ifindex;
    uint32_t ipv4;
    uint8_t mac[6];
} sys_arp_info_t;

typedef struct {
    uint32_t sec;
    uint32_t nsec;
} sys_timespec_t;

typedef struct {
    uint32_t sec;
    uint32_t usec;
} sys_timeval_t;

typedef struct {
    char sysname[SYS_UTS_FIELD_MAX];
    char nodename[SYS_UTS_FIELD_MAX];
    char release[SYS_UTS_FIELD_MAX];
    char version[SYS_UTS_FIELD_MAX];
    char machine[SYS_UTS_FIELD_MAX];
} sys_utsname_t;

typedef struct {
    int fd;
    uint32_t events;
    uint32_t revents;
} sys_pollfd_t;

typedef struct {
    void *base;
    uint32_t len;
} sys_iovec_t;


void syscall_init(void);

/* Inline syscall helpers using int 0x80 */
static inline int syscall0(uint32_t num) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num) : "memory");
    return ret;
}

static inline int syscall1(uint32_t num, uint32_t a) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num), "b"(a) : "memory");
    return ret;
}

static inline int syscall2(uint32_t num, uint32_t a, uint32_t b) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num), "b"(a), "c"(b) : "memory");
    return ret;
}

static inline int syscall3(uint32_t num, uint32_t a, uint32_t b, uint32_t c) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num), "b"(a), "c"(b), "d"(c) : "memory");
    return ret;
}

static inline int syscall4(uint32_t num, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "0"(num), "b"(a), "c"(b), "d"(c), "S"(d)
                     : "memory");
    return ret;
}

static inline void sys_yield(void) {
    (void)syscall0(SYS_YIELD);
}

static inline void sys_sleep(uint32_t ms) {
    (void)syscall1(SYS_SLEEP, ms);
}

static inline void sys_exit(int code) {
    (void)syscall1(SYS_EXIT, (uint32_t)code);
}

static inline int sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

static inline int sys_getppid(void) {
    return syscall0(SYS_GETPPID);
}

static inline int sys_getpgid(uint32_t pid) {
    return syscall1(SYS_GETPGID, pid);
}

static inline int sys_setpgid(uint32_t pid, uint32_t pgid) {
    return syscall2(SYS_SETPGID, pid, pgid);
}

static inline int sys_getsid(uint32_t pid) {
    return syscall1(SYS_GETSID, pid);
}

static inline int sys_setsid(void) {
    return syscall0(SYS_SETSID);
}

static inline int sys_write(int fd, const void *buf, uint32_t len) {
    return syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, len);
}

static inline int sys_puts(const char *s) {
    uint32_t len = 0;
    while (s && s[len]) len++;
    return syscall3(SYS_WRITE, 1, (uint32_t)s, len);
}

static inline int sys_open(const char *path, int flags) {
    return syscall2(SYS_OPEN, (uint32_t)path, (uint32_t)flags);
}

static inline int sys_openat(int dirfd, const char *path, int flags) {
    return syscall3(SYS_OPENAT, (uint32_t)dirfd, (uint32_t)path, (uint32_t)flags);
}

static inline int sys_read(int fd, void *buf, uint32_t len) {
    return syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, len);
}

static inline int sys_pread(int fd, void *buf, uint32_t len, uint32_t offset) {
    return syscall4(SYS_PREAD, (uint32_t)fd, (uint32_t)buf, len, offset);
}

static inline int sys_pwrite(int fd, const void *buf, uint32_t len, uint32_t offset) {
    return syscall4(SYS_PWRITE, (uint32_t)fd, (uint32_t)buf, len, offset);
}

static inline int sys_readv(int fd, sys_iovec_t *iov, uint32_t count) {
    return syscall3(SYS_READV, (uint32_t)fd, (uint32_t)iov, count);
}

static inline int sys_writev(int fd, const sys_iovec_t *iov, uint32_t count) {
    return syscall3(SYS_WRITEV, (uint32_t)fd, (uint32_t)iov, count);
}

static inline int sys_close(int fd) {
    return syscall1(SYS_CLOSE, (uint32_t)fd);
}

static inline int sys_dup(int fd) {
    return syscall1(SYS_DUP, (uint32_t)fd);
}

static inline int sys_dup2(int old_fd, int new_fd) {
    return syscall2(SYS_DUP2, (uint32_t)old_fd, (uint32_t)new_fd);
}

static inline int sys_fcntl(int fd, uint32_t cmd, uint32_t arg) {
    return syscall3(SYS_FCNTL, (uint32_t)fd, cmd, arg);
}

static inline int sys_chdir(const char *path) {
    return syscall1(SYS_CHDIR, (uint32_t)path);
}

static inline int sys_getcwd(char *buf, uint32_t len) {
    return syscall2(SYS_GETCWD, (uint32_t)buf, len);
}

static inline int sys_rename(const char *old_path, const char *new_path) {
    return syscall2(SYS_RENAME, (uint32_t)old_path, (uint32_t)new_path);
}

static inline int sys_pipe(int fds[2]) {
    return syscall1(SYS_PIPE, (uint32_t)fds);
}

static inline int sys_pipe2(int fds[2], uint32_t flags) {
    return syscall2(SYS_PIPE2, (uint32_t)fds, flags);
}

static inline int sys_kill_signal(uint32_t pid, uint32_t sig) {
    return syscall2(SYS_KILL_SIGNAL, pid, sig);
}

static inline int sys_kill_group(uint32_t pgid, uint32_t sig) {
    return syscall2(SYS_KILL_GROUP, pgid, sig);
}

static inline int sys_sbrk(int32_t increment) {
    return syscall1(SYS_SBRK, (uint32_t)increment);
}

static inline void *sys_mmap(uint32_t length, uint32_t flags) {
    int ret = syscall2(SYS_MMAP, length, flags);
    return ret < 0 ? (void *)0 : (void *)(uint32_t)ret;
}

static inline int sys_munmap(void *addr, uint32_t length) {
    return syscall2(SYS_MUNMAP, (uint32_t)addr, length);
}

static inline int sys_mprotect(void *addr, uint32_t length, uint32_t flags) {
    return syscall3(SYS_MPROTECT, (uint32_t)addr, length, flags);
}

static inline int sys_uptime(void) {
    return syscall0(SYS_UPTIME);
}

static inline int sys_clock_ms(void) {
    return syscall0(SYS_CLOCK_MS);
}

static inline int sys_clock_gettime(uint32_t clock_id, sys_timespec_t *out) {
    return syscall2(SYS_CLOCK_GETTIME, clock_id, (uint32_t)out);
}

static inline int sys_time(uint32_t *out_sec) {
    return syscall1(SYS_TIME, (uint32_t)out_sec);
}

static inline int sys_gettimeofday(sys_timeval_t *out) {
    return syscall1(SYS_GETTIMEOFDAY, (uint32_t)out);
}

static inline int sys_nanosleep(const sys_timespec_t *req, sys_timespec_t *rem) {
    return syscall2(SYS_NANOSLEEP, (uint32_t)req, (uint32_t)rem);
}

static inline int sys_getrandom(void *buf, uint32_t len, uint32_t flags) {
    return syscall3(SYS_GETRANDOM, (uint32_t)buf, len, flags);
}

static inline int sys_seek(int fd, int32_t off, int whence) {
    return syscall3(SYS_SEEK, (uint32_t)fd, (uint32_t)off, (uint32_t)whence);
}

static inline int sys_fstat(int fd, void *out) {
    return syscall2(SYS_FSTAT, (uint32_t)fd, (uint32_t)out);
}

static inline int sys_ftruncate(int fd, uint32_t size) {
    return syscall2(SYS_FTRUNCATE, (uint32_t)fd, size);
}

static inline int sys_fchmod(int fd, uint32_t mode) {
    return syscall2(SYS_FCHMOD, (uint32_t)fd, mode);
}

static inline int sys_futime(int fd, uint32_t accessed_ms, uint32_t modified_ms) {
    return syscall3(SYS_FUTIME, (uint32_t)fd, accessed_ms, modified_ms);
}

static inline int sys_fsync(int fd) {
    return syscall1(SYS_FSYNC, (uint32_t)fd);
}

static inline int sys_fdatasync(int fd) {
    return syscall1(SYS_FDATASYNC, (uint32_t)fd);
}

static inline int sys_sync(void) {
    return syscall0(SYS_SYNC);
}

static inline int sys_poll(int fd, uint32_t events) {
    return syscall2(SYS_POLL, (uint32_t)fd, events);
}

static inline int sys_poll_many(sys_pollfd_t *fds, uint32_t count) {
    return syscall2(SYS_POLL_MANY, (uint32_t)fds, count);
}

static inline int sys_ioctl(int fd, uint32_t request, uint32_t arg) {
    return syscall3(SYS_IOCTL, (uint32_t)fd, request, arg);
}

static inline int sys_tty_get_mode(void) {
    return syscall0(SYS_TTY_GET_MODE);
}

static inline int sys_tty_set_mode(uint32_t mode) {
    return syscall1(SYS_TTY_SET_MODE, mode);
}

static inline int sys_truncate(const char *path, uint32_t size) {
    return syscall2(SYS_TRUNCATE, (uint32_t)path, size);
}

static inline int sys_chmod(const char *path, uint32_t mode) {
    return syscall2(SYS_CHMOD, (uint32_t)path, mode);
}

static inline int sys_utime(const char *path, uint32_t accessed_ms, uint32_t modified_ms) {
    return syscall3(SYS_UTIME, (uint32_t)path, accessed_ms, modified_ms);
}

static inline int sys_access(const char *path, uint32_t mask) {
    return syscall2(SYS_ACCESS, (uint32_t)path, mask);
}

static inline int sys_accessat(int dirfd, const char *path, uint32_t mask) {
    return syscall3(SYS_ACCESSAT, (uint32_t)dirfd, (uint32_t)path, mask);
}

static inline int sys_uname(sys_utsname_t *out) {
    return syscall1(SYS_UNAME, (uint32_t)out);
}

static inline int sys_sysconf(uint32_t key) {
    return syscall1(SYS_SYSCONF, key);
}

static inline int sys_getenv(const char *name, char *buf, uint32_t len) {
    return syscall3(SYS_GETENV, (uint32_t)name, (uint32_t)buf, len);
}

static inline int sys_setenv(const char *name, const char *value, int overwrite) {
    return syscall3(SYS_SETENV, (uint32_t)name, (uint32_t)value, overwrite ? 1U : 0U);
}

static inline int sys_unsetenv(const char *name) {
    return syscall1(SYS_UNSETENV, (uint32_t)name);
}

static inline int sys_env_count(void) {
    return syscall0(SYS_ENV_COUNT);
}

static inline int sys_env_info(uint32_t index, char *buf, uint32_t len) {
    return syscall3(SYS_ENV_INFO, index, (uint32_t)buf, len);
}

static inline int sys_gethostname(char *buf, uint32_t len) {
    return syscall2(SYS_GETHOSTNAME, (uint32_t)buf, len);
}

static inline int sys_sethostname(const char *name) {
    return syscall1(SYS_SETHOSTNAME, (uint32_t)name);
}

static inline int sys_umask(uint32_t mask) {
    return syscall1(SYS_UMASK, mask);
}

static inline int sys_statfs(const char *path, sys_fsinfo_t *out) {
    return syscall2(SYS_STATFS, (uint32_t)path, (uint32_t)out);
}

static inline int sys_stat(const char *path, void *out) {
    return syscall2(SYS_STAT, (uint32_t)path, (uint32_t)out);
}

static inline int sys_fstatat(int dirfd, const char *path, void *out) {
    return syscall3(SYS_FSTATAT, (uint32_t)dirfd, (uint32_t)path, (uint32_t)out);
}

static inline int sys_lstat(const char *path, void *out) {
    return syscall2(SYS_LSTAT, (uint32_t)path, (uint32_t)out);
}

static inline int sys_readdir(const char *path, uint32_t index, void *out) {
    return syscall3(SYS_READDIR, (uint32_t)path, index, (uint32_t)out);
}

static inline int sys_freaddir(int fd, uint32_t index, void *out) {
    return syscall3(SYS_FREADDIR, (uint32_t)fd, index, (uint32_t)out);
}

static inline int sys_fchdir(int fd) {
    return syscall1(SYS_FCHDIR, (uint32_t)fd);
}

static inline int sys_spawn(const char *path) {
    return syscall1(SYS_SPAWN, (uint32_t)path);
}

static inline int sys_spawn_args(const char *path, uint32_t argc, const char *const argv[]) {
    return syscall4(SYS_SPAWN_ARGS, (uint32_t)path, argc, (uint32_t)argv, 0);
}

static inline int sys_wait(uint32_t pid, int *code) {
    return syscall2(SYS_WAIT, pid, (uint32_t)code);
}

static inline int sys_wait_any(int *code) {
    return syscall1(SYS_WAIT_ANY, (uint32_t)code);
}

static inline int sys_mkdir(const char *path) {
    return syscall1(SYS_MKDIR, (uint32_t)path);
}

static inline int sys_mkdirat(int dirfd, const char *path) {
    return syscall2(SYS_MKDIRAT, (uint32_t)dirfd, (uint32_t)path);
}

static inline int sys_rmdir(const char *path) {
    return syscall1(SYS_RMDIR, (uint32_t)path);
}

static inline int sys_symlink(const char *target, const char *link_path) {
    return syscall2(SYS_SYMLINK, (uint32_t)target, (uint32_t)link_path);
}

static inline int sys_readlink(const char *path, char *buf, uint32_t len) {
    return syscall3(SYS_READLINK, (uint32_t)path, (uint32_t)buf, len);
}

static inline int sys_unlink(const char *path) {
    return syscall1(SYS_UNLINK, (uint32_t)path);
}

static inline int sys_unlinkat(int dirfd, const char *path) {
    return syscall2(SYS_UNLINKAT, (uint32_t)dirfd, (uint32_t)path);
}

static inline int sys_renameat(int old_dirfd,
                               const char *old_path,
                               int new_dirfd,
                               const char *new_path) {
    return syscall4(SYS_RENAMEAT,
                    (uint32_t)old_dirfd,
                    (uint32_t)old_path,
                    (uint32_t)new_dirfd,
                    (uint32_t)new_path);
}

static inline int sys_kill(uint32_t pid) {
    return syscall1(SYS_KILL, pid);
}

static inline int sys_netif_count(void) {
    return syscall0(SYS_NETIF_COUNT);
}

static inline int sys_netif_info(uint32_t index, sys_netif_info_t *out) {
    return syscall2(SYS_NETIF_INFO, index, (uint32_t)out);
}

static inline int sys_net_ping4(uint32_t dst_ipv4) {
    return syscall1(SYS_NET_PING4, dst_ipv4);
}

static inline int sys_udp_send4(uint32_t dst_ipv4,
                                uint16_t dst_port,
                                const void *payload,
                                uint32_t len) {
    return syscall4(SYS_UDP_SEND4, dst_ipv4, dst_port, (uint32_t)payload, len);
}

static inline int sys_udp_recv4(uint16_t port,
                                void *payload,
                                uint32_t max,
                                sys_udp_peer_t *peer) {
    return syscall4(SYS_UDP_RECV4, port, (uint32_t)payload, max, (uint32_t)peer);
}

static inline int sys_socket_udp4(void) {
    return syscall0(SYS_SOCKET_UDP4);
}

static inline int sys_bind_udp4(int fd, uint16_t port) {
    return syscall2(SYS_BIND_UDP4, (uint32_t)fd, port);
}

static inline int sys_sendto_udp4(int fd,
                                  const sys_sockaddr_in4_t *dst,
                                  const void *payload,
                                  uint32_t len) {
    return syscall4(SYS_SENDTO_UDP4, (uint32_t)fd, (uint32_t)dst, (uint32_t)payload, len);
}

static inline int sys_recvfrom_udp4(int fd,
                                    sys_sockaddr_in4_t *src,
                                    void *payload,
                                    uint32_t max) {
    return syscall4(SYS_RECVFROM_UDP4, (uint32_t)fd, (uint32_t)src, (uint32_t)payload, max);
}

static inline int sys_connect_udp4(int fd, const sys_sockaddr_in4_t *dst) {
    return syscall2(SYS_CONNECT_UDP4, (uint32_t)fd, (uint32_t)dst);
}

static inline int sys_getsockname_udp4(int fd, sys_sockaddr_in4_t *out) {
    return syscall2(SYS_GETSOCKNAME_UDP4, (uint32_t)fd, (uint32_t)out);
}

static inline int sys_getpeername_udp4(int fd, sys_sockaddr_in4_t *out) {
    return syscall2(SYS_GETPEERNAME_UDP4, (uint32_t)fd, (uint32_t)out);
}

static inline int sys_select(uint32_t *readfds, uint32_t *writefds, uint32_t nfds, uint32_t timeout_ms) {
    return syscall4(SYS_SELECT, (uint32_t)readfds, (uint32_t)writefds, nfds, timeout_ms);
}

static inline int sys_getuid(void) {
    return syscall0(SYS_GETUID);
}

static inline int sys_getgid(void) {
    return syscall0(SYS_GETGID);
}

static inline int sys_setuid(uint32_t uid) {
    return syscall1(SYS_SETUID, uid);
}

static inline int sys_setgid(uint32_t gid) {
    return syscall1(SYS_SETGID, gid);
}

static inline int sys_chown(const char *path, uint32_t uid, uint32_t gid) {
    return syscall3(SYS_CHOWN, (uint32_t)path, uid, gid);
}

static inline int sys_fchown(int fd, uint32_t uid, uint32_t gid) {
    return syscall3(SYS_FCHOWN, (uint32_t)fd, uid, gid);
}

static inline int sys_exec(const char *path) {
    return syscall1(SYS_EXEC, (uint32_t)path);
}

static inline int sys_exec_args(const char *path, uint32_t argc, const char *const argv[]) {
    return syscall4(SYS_EXEC_ARGS, (uint32_t)path, argc, (uint32_t)argv, 0);
}

static inline int sys_fork(void) {
    return syscall0(SYS_FORK);
}

static inline int sys_sysinfo(sys_sysinfo_t *out) {
    return syscall1(SYS_SYSINFO, (uint32_t)out);
}

static inline int sys_process_count(void) {
    return syscall0(SYS_PROCESS_COUNT);
}

static inline int sys_process_info(uint32_t index, sys_process_info_t *out) {
    return syscall2(SYS_PROCESS_INFO, index, (uint32_t)out);
}

static inline int sys_block_count(void) {
    return syscall0(SYS_BLOCK_COUNT);
}

static inline int sys_block_info(uint32_t index, sys_block_info_t *out) {
    return syscall2(SYS_BLOCK_INFO, index, (uint32_t)out);
}

static inline int sys_driver_count(void) {
    return syscall0(SYS_DRIVER_COUNT);
}

static inline int sys_driver_info(uint32_t index, sys_driver_info_t *out) {
    return syscall2(SYS_DRIVER_INFO, index, (uint32_t)out);
}

static inline int sys_task_count(void) {
    return syscall0(SYS_TASK_COUNT);
}

static inline int sys_task_info(uint32_t index, sys_task_info_t *out) {
    return syscall2(SYS_TASK_INFO, index, (uint32_t)out);
}

static inline int sys_task_set_priority(uint32_t task_id, uint32_t priority) {
    return syscall2(SYS_TASK_SET_PRIORITY, task_id, priority);
}

static inline int sys_task_priority(uint32_t task_id) {
    return syscall1(SYS_TASK_PRIORITY, task_id);
}

static inline int sys_pci_count(void) {
    return syscall0(SYS_PCI_COUNT);
}

static inline int sys_pci_info(uint32_t index, sys_pci_info_t *out) {
    return syscall2(SYS_PCI_INFO, index, (uint32_t)out);
}

static inline int sys_block_read(uint32_t dev, uint32_t lba, void *buf, uint32_t sectors) {
    return syscall4(SYS_BLOCK_READ, dev, lba, (uint32_t)buf, sectors);
}

static inline int sys_block_write(uint32_t dev,
                                  uint32_t lba,
                                  const void *buf,
                                  uint32_t sectors) {
    return syscall4(SYS_BLOCK_WRITE, dev, lba, (uint32_t)buf, sectors);
}

static inline int sys_simplefs_format(uint32_t dev) {
    return syscall1(SYS_SIMPLEFS_FORMAT, dev);
}

static inline int sys_simplefs_mount(uint32_t dev, const char *mount_path) {
    return syscall2(SYS_SIMPLEFS_MOUNT, dev, (uint32_t)mount_path);
}

static inline int sys_simplefs_unmount(void) {
    return syscall0(SYS_SIMPLEFS_UNMOUNT);
}


static inline int sys_netif_set_up(uint32_t index, int up) {
    return syscall2(SYS_NETIF_SET_UP, index, up ? 1U : 0U);
}

static inline int sys_netif_set_ipv4(uint32_t index, uint32_t ipv4) {
    return syscall2(SYS_NETIF_SET_IPV4, index, ipv4);
}

static inline int sys_route_add4(uint32_t dest, uint32_t mask, uint32_t gateway, uint32_t ifindex) {
    return syscall4(SYS_ROUTE_ADD4, dest, mask, gateway, ifindex);
}

static inline int sys_route_del4(uint32_t dest, uint32_t mask, uint32_t gateway, uint32_t ifindex) {
    return syscall4(SYS_ROUTE_DEL4, dest, mask, gateway, ifindex);
}

static inline int sys_arp_add4(uint32_t ifindex, uint32_t ipv4, const uint8_t mac[6]) {
    return syscall3(SYS_ARP_ADD4, ifindex, ipv4, (uint32_t)mac);
}

static inline int sys_arp_del4(uint32_t ifindex, uint32_t ipv4) {
    return syscall2(SYS_ARP_DEL4, ifindex, ipv4);
}


static inline int sys_route_count(void) {
    return syscall0(SYS_ROUTE_COUNT);
}

static inline int sys_route_info(uint32_t index, sys_route_info_t *out) {
    return syscall2(SYS_ROUTE_INFO, index, (uint32_t)out);
}

static inline int sys_arp_count(void) {
    return syscall0(SYS_ARP_COUNT);
}

static inline int sys_arp_info(uint32_t index, sys_arp_info_t *out) {
    return syscall2(SYS_ARP_INFO, index, (uint32_t)out);
}

#endif /* SYSCALL_H */
