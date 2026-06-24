#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_FDS      16
#define VFS_MAX_NAME     32
#define VFS_MAX_PATH     96
#define VFS_FS_NAME_MAX  16
#define VFS_O_RDONLY     0x0001
#define VFS_O_WRONLY     0x0002
#define VFS_O_RDWR       0x0003
#define VFS_O_CREAT      0x0100
#define VFS_O_TRUNC      0x0200
#define VFS_O_APPEND     0x0400
#define VFS_O_CLOEXEC    0x0800
#define VFS_O_NONBLOCK   0x1000
#define VFS_POLL_READ    0x0001
#define VFS_POLL_WRITE   0x0002
#define VFS_IOCTL_TTY_GET_MODE 0x5401U
#define VFS_IOCTL_TTY_SET_MODE 0x5402U
#define VFS_IOCTL_TTY_GET_SIZE 0x5403U
#define VFS_SEEK_SET     0
#define VFS_SEEK_CUR     1
#define VFS_SEEK_END     2
#define VFS_MODE_IFREG   0100000U
#define VFS_MODE_IFDIR   0040000U
#define VFS_MODE_IFDEV   0020000U
#define VFS_MODE_IFLNK   0120000U
#define VFS_MODE_TYPE_MASK 0170000U
#define VFS_MODE_PERM_MASK 0000777U
#define VFS_MODE_READ_MASK 0000444U
#define VFS_MODE_WRITE_MASK 0000222U
#define VFS_MODE_EXEC_MASK 0000111U
#define VFS_ACCESS_READ  0x1U
#define VFS_ACCESS_WRITE 0x2U
#define VFS_ACCESS_EXEC  0x4U

typedef enum {
    VFS_NODE_FILE = 1,
    VFS_NODE_DIR  = 2,
    VFS_NODE_DEV  = 3,
    VFS_NODE_SYMLINK = 4
} vfs_node_type_t;

typedef struct {
    char name[VFS_MAX_NAME];
    vfs_node_type_t type;
    uint32_t size;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t created_ms;
    uint32_t modified_ms;
    uint32_t accessed_ms;
} vfs_dirent_t;

typedef struct {
    char name[VFS_FS_NAME_MAX];
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t total_files;
    uint32_t free_files;
} vfs_fsinfo_t;

void vfs_init(void);
int  vfs_mount_ramfs(void);
int  vfs_open(const char *path, int flags);
int  vfs_pipe(int out_fds[2]);
int  vfs_socket_udp4(void);
int  vfs_socket_bind_udp4(int fd, uint16_t port);
int  vfs_socket_connect_udp4(int fd, uint32_t dst_ipv4, uint16_t dst_port);
int  vfs_socket_getname_udp4(int fd, uint32_t *local_ipv4, uint16_t *local_port);
int  vfs_socket_getpeer_udp4(int fd, uint32_t *dst_ipv4, uint16_t *dst_port);
int  vfs_socket_sendto_udp4(int fd,
                            uint32_t dst_ipv4,
                            uint16_t dst_port,
                            const void *payload,
                            uint32_t len);
int  vfs_socket_recvfrom_udp4(int fd,
                              uint32_t *src_ipv4,
                              uint16_t *src_port,
                              void *payload,
                              uint32_t max);
int  vfs_dup(int fd);
int  vfs_close(int fd);
int  vfs_fsync(int fd);
int  vfs_fdatasync(int fd);
int  vfs_sync(void);
int  vfs_fd_flags(int fd);
int  vfs_set_fd_flags(int fd, int flags);
int  vfs_read(int fd, void *buf, uint32_t len);
int  vfs_write(int fd, const void *buf, uint32_t len);
int  vfs_pread(int fd, void *buf, uint32_t len, uint32_t offset);
int  vfs_pwrite(int fd, const void *buf, uint32_t len, uint32_t offset);
int  vfs_seek(int fd, int32_t off, int whence);
int  vfs_fstat(int fd, vfs_dirent_t *out);
int  vfs_ftruncate(int fd, uint32_t size);
int  vfs_fchmod(int fd, uint32_t mode);
int  vfs_fchown(int fd, uint32_t uid, uint32_t gid);
int  vfs_futime(int fd, uint32_t accessed_ms, uint32_t modified_ms);
int  vfs_poll(int fd, uint32_t events);
int  vfs_ioctl(int fd, uint32_t request, uint32_t arg);
int  vfs_readdir(const char *path, uint32_t index, vfs_dirent_t *out);
int  vfs_readdir_fd(int fd, uint32_t index, vfs_dirent_t *out);
int  vfs_fd_path(int fd, char *out, uint32_t max);
int  vfs_getrandom(void *buf, uint32_t len);
int  vfs_stat(const char *path, vfs_dirent_t *out);
int  vfs_lstat(const char *path, vfs_dirent_t *out);
int  vfs_statfs(const char *path, vfs_fsinfo_t *out);
int  vfs_access(const char *path, uint32_t mask);
int  vfs_chmod(const char *path, uint32_t mode);
int  vfs_chown(const char *path, uint32_t uid, uint32_t gid);
int  vfs_utime(const char *path, uint32_t accessed_ms, uint32_t modified_ms);
int  vfs_truncate(const char *path, uint32_t size);
int  vfs_create(const char *path);
int  vfs_mkdir(const char *path);
int  vfs_rmdir(const char *path);
int  vfs_symlink(const char *target, const char *link_path);
int  vfs_readlink(const char *path, char *out, uint32_t max);
int  vfs_unlink(const char *path);
int  vfs_rename(const char *old_path, const char *new_path);

#endif
