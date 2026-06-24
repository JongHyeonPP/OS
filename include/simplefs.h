#ifndef SIMPLEFS_H
#define SIMPLEFS_H

#include "vfs.h"
#include <stdint.h>

#define SIMPLEFS_MAX_FILES 24
#define SIMPLEFS_NAME_MAX  32
#define SIMPLEFS_MOUNT_MAX VFS_MAX_PATH

void simplefs_init(void);
int simplefs_format(uint32_t dev);
int simplefs_mount(uint32_t dev, const char *mount_path);
int simplefs_unmount(void);
int simplefs_mounted(void);
const char *simplefs_mount_path(void);
int simplefs_open(const char *path, int flags);
int simplefs_close(int fd);
int simplefs_fd_flags(int fd);
int simplefs_set_fd_flags(int fd, int flags);
int simplefs_fsync(int fd);
int simplefs_sync(void);
int simplefs_read(int fd, void *buf, uint32_t len);
int simplefs_write(int fd, const void *buf, uint32_t len);
int simplefs_seek(int fd, int32_t off, int whence);
int simplefs_ftruncate(int fd, uint32_t size);
int simplefs_truncate(const char *path, uint32_t size);
int simplefs_poll(int fd, uint32_t events);
int simplefs_readdir(const char *path, uint32_t index, vfs_dirent_t *out);
int simplefs_stat(const char *path, vfs_dirent_t *out);
int simplefs_statfs(const char *path, vfs_fsinfo_t *out);
int simplefs_access(const char *path, uint32_t mask);
int simplefs_chmod(const char *path, uint32_t mode);
int simplefs_chown(const char *path, uint32_t uid, uint32_t gid);
int simplefs_utime(const char *path, uint32_t accessed_ms, uint32_t modified_ms);
int simplefs_create(const char *path);
int simplefs_mkdir(const char *path);
int simplefs_unlink(const char *path);
int simplefs_rename(const char *old_path, const char *new_path);

#endif
