#include "simplefs.h"
#include "block.h"
#include "debug.h"
#include "process.h"
#include "timer.h"

#define SFS_MAGIC       0x3153464DU
#define SFS_VERSION     1
#define SFS_DIR_START   1
#define SFS_DIR_SECTORS 3
#define SFS_DATA_START  (SFS_DIR_START + SFS_DIR_SECTORS)
#define SFS_FILE_SECTORS 4
#define SFS_MAX_OPEN    8
#define SFS_TYPE_FILE   1
#define SFS_TYPE_DIR    2
#define SFS_OWNER_MAX   255U
#define SFS_UID_SHIFT   16
#define SFS_GID_SHIFT   24
#define SFS_MODE_STORED_MASK (VFS_MODE_TYPE_MASK | VFS_MODE_PERM_MASK | 0xFFFF0000U)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sector_size;
    uint32_t max_files;
    uint32_t dir_start;
    uint32_t dir_sectors;
    uint32_t data_start;
    uint32_t file_sectors;
    uint32_t total_sectors;
    uint32_t reserved[119];
} __attribute__((packed)) sfs_super_t;

typedef struct {
    uint32_t used;
    char name[SIMPLEFS_NAME_MAX];
    uint32_t size;
    uint32_t first_sector;
    uint32_t sector_count;
    uint32_t reserved[4];
} __attribute__((packed)) sfs_entry_t;

typedef char sfs_super_must_fill_one_sector[(sizeof(sfs_super_t) == BLOCK_SECTOR_SIZE) ? 1 : -1];
typedef char sfs_dir_must_fill_dir_sectors[
    (sizeof(sfs_entry_t) * SIMPLEFS_MAX_FILES == SFS_DIR_SECTORS * BLOCK_SECTOR_SIZE) ? 1 : -1
];

typedef struct {
    int used;
    uint32_t entry;
    uint32_t pos;
    int flags;
} sfs_fd_t;

static uint32_t g_dev;
static int g_mounted;
static char g_mount[SIMPLEFS_MOUNT_MAX];
static sfs_super_t g_super;
static sfs_entry_t g_entries[SIMPLEFS_MAX_FILES];
static sfs_fd_t g_fds[SFS_MAX_OPEN];

static void seed_default_file(void);

static uint32_t simplefs_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\npop %0\ncli" : "=r"(flags) : : "memory");
    return flags;
}

static void simplefs_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0\npopfl" : : "r"(flags) : "memory");
}

static void clear_sfs_fd(sfs_fd_t *fd) {
    fd->used = 0;
    fd->entry = 0;
    fd->pos = 0;
    fd->flags = 0;
}

static int entry_has_open_fd(uint32_t entry) {
    uint32_t i;
    for (i = 0; i < SFS_MAX_OPEN; i++) {
        if (g_fds[i].used && g_fds[i].entry == entry) return 1;
    }
    return 0;
}

static int any_open_fd(void) {
    uint32_t i;
    for (i = 0; i < SFS_MAX_OPEN; i++) {
        if (g_fds[i].used) return 1;
    }
    return 0;
}

static void close_all_fds(void) {
    uint32_t i;
    for (i = 0; i < SFS_MAX_OPEN; i++) clear_sfs_fd(&g_fds[i]);
}

static int reserve_sfs_fd(void) {
    uint32_t flags = simplefs_irq_save();
    uint32_t i;
    for (i = 0; i < SFS_MAX_OPEN; i++) {
        if (!g_fds[i].used) {
            clear_sfs_fd(&g_fds[i]);
            g_fds[i].used = 1;
            simplefs_irq_restore(flags);
            return (int)i;
        }
    }
    simplefs_irq_restore(flags);
    return -1;
}

static void release_reserved_sfs_fd(int fd) {
    uint32_t flags;
    if (fd < 0 || fd >= SFS_MAX_OPEN) return;
    flags = simplefs_irq_save();
    if (g_fds[fd].used && g_fds[fd].flags == 0) clear_sfs_fd(&g_fds[fd]);
    simplefs_irq_restore(flags);
}

static int streq(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static uint32_t slen(const char *s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void scopy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    if (!max) return;
    while (src && src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void memzero(void *buf, uint32_t n) {
    uint8_t *p = (uint8_t *)buf;
    uint32_t i;
    for (i = 0; i < n; i++) p[i] = 0;
}

static void memcopy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}

static int path_under_mount(const char *path, const char **name) {
    uint32_t ml = slen(g_mount);
    if (!g_mounted || !path || !name) return 0;
    if (streq(path, g_mount)) {
        *name = "";
        return 1;
    }
    if (ml == 1) {
        if (path[0] != '/' || path[1] == 0) return 0;
        *name = path + 1;
        return 1;
    }
    {
        uint32_t i;
        for (i = 0; i < ml; i++) {
            if (path[i] != g_mount[i]) return 0;
        }
    }
    if (path[ml] != '/' || path[ml + 1] == 0) return 0;
    *name = path + ml + 1;
    return 1;
}

static int name_valid(const char *name) {
    uint32_t i;
    uint32_t component = 0;
    uint32_t component_start = 0;
    if (!name || !name[0] || slen(name) >= SIMPLEFS_NAME_MAX) return 0;
    for (i = 0; name[i]; i++) {
        if (name[i] == '/') {
            if (component == 0 || component >= VFS_MAX_NAME) return 0;
            if ((component == 1 && name[component_start] == '.') ||
                (component == 2 && name[component_start] == '.' && name[component_start + 1] == '.')) return 0;
            component = 0;
            component_start = i + 1;
        } else {
            component++;
        }
    }
    if (component == 0 || component >= VFS_MAX_NAME) return 0;
    if ((component == 1 && name[component_start] == '.') ||
        (component == 2 && name[component_start] == '.' && name[component_start + 1] == '.')) return 0;
    return 1;
}

static int mount_path_valid(const char *path) {
    uint32_t i;
    uint32_t len;
    uint32_t component = 0;
    uint32_t component_start = 1;
    if (!path || path[0] != '/') return 0;
    len = slen(path);
    if (len <= 1 || len >= SIMPLEFS_MOUNT_MAX) return 0;
    for (i = 1; i < len; i++) {
        if (path[i] == '/') {
            if (component == 0 || component >= VFS_MAX_NAME) return 0;
            if ((component == 1 && path[component_start] == '.') ||
                (component == 2 && path[component_start] == '.' && path[component_start + 1] == '.')) return 0;
            component = 0;
            component_start = i + 1;
        } else {
            component++;
        }
    }
    if (component == 0 || component >= VFS_MAX_NAME) return 0;
    if ((component == 1 && path[component_start] == '.') ||
        (component == 2 && path[component_start] == '.' && path[component_start + 1] == '.')) return 0;
    return 1;
}

static int mount_path_reserved(const char *path) {
    return path &&
           path[0] == '/' &&
           ((path[1] == 'd' && path[2] == 'e' && path[3] == 'v' &&
             (path[4] == 0 || path[4] == '/')) ||
            (path[1] == 'p' && path[2] == 'r' && path[3] == 'o' &&
             path[4] == 'c' && (path[5] == 0 || path[5] == '/')));
}

static int flags_valid(int flags) {
    int access = flags & VFS_O_RDWR;
    int allowed = VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC | VFS_O_APPEND | VFS_O_NONBLOCK;
    if ((flags & ~allowed) != 0) return 0;
    if ((flags & VFS_O_TRUNC) && access == VFS_O_RDONLY) return 0;
    if ((flags & VFS_O_APPEND) && access == VFS_O_RDONLY) return 0;
    return access == VFS_O_RDONLY ||
           access == VFS_O_WRONLY ||
           access == VFS_O_RDWR;
}

static int read_super(uint32_t dev, sfs_super_t *out) {
    return block_read(dev, 0, out, 1) == 1 ? 0 : -1;
}

static int write_super(uint32_t dev, const sfs_super_t *in) {
    return block_write(dev, 0, in, 1) == 1 ? 0 : -1;
}

static int read_dir_into(uint32_t dev, sfs_entry_t *entries) {
    return block_read(dev, SFS_DIR_START, entries, SFS_DIR_SECTORS) == SFS_DIR_SECTORS ? 0 : -1;
}

static int read_dir(uint32_t dev) {
    return read_dir_into(dev, g_entries);
}

static int write_dir(void) {
    return block_write(g_dev, SFS_DIR_START, g_entries, SFS_DIR_SECTORS) == SFS_DIR_SECTORS ? 0 : -1;
}

static int super_valid(const sfs_super_t *s, const block_device_t *b) {
    if (!s || !b) return 0;
    if (s->magic != SFS_MAGIC || s->version != SFS_VERSION) return 0;
    if (s->sector_size != BLOCK_SECTOR_SIZE || s->max_files != SIMPLEFS_MAX_FILES) return 0;
    if (s->dir_start != SFS_DIR_START || s->dir_sectors != SFS_DIR_SECTORS) return 0;
    if (s->data_start != SFS_DATA_START || s->file_sectors != SFS_FILE_SECTORS) return 0;
    if (s->data_start + s->max_files * s->file_sectors > b->sector_count) return 0;
    return 1;
}

static int find_entry(const char *name) {
    uint32_t i;
    for (i = 0; i < SIMPLEFS_MAX_FILES; i++) {
        if (g_entries[i].used && streq(g_entries[i].name, name)) return (int)i;
    }
    return -1;
}

static uint32_t entry_type(const sfs_entry_t *entry) {
    if (!entry || !entry->used) return 0;
    return entry->reserved[0] == SFS_TYPE_DIR ? SFS_TYPE_DIR : SFS_TYPE_FILE;
}

static uint32_t mode_type_bits(uint32_t type) {
    return type == SFS_TYPE_DIR ? VFS_MODE_IFDIR : VFS_MODE_IFREG;
}

static uint32_t default_entry_mode(uint32_t type) {
    return mode_type_bits(type) | (type == SFS_TYPE_DIR ? 0000755U : 0000644U);
}

static uint32_t entry_mode(const sfs_entry_t *entry) {
    uint32_t type = entry_type(entry);
    uint32_t mode;
    if (!entry || !entry->used) return 0;
    mode = entry->reserved[1] & (VFS_MODE_TYPE_MASK | VFS_MODE_PERM_MASK);
    if (mode == 0) return default_entry_mode(type);
    return mode_type_bits(type) | (mode & VFS_MODE_PERM_MASK);
}

static uint32_t entry_uid(const sfs_entry_t *entry) {
    if (!entry || !entry->used) return 0;
    return (entry->reserved[1] >> SFS_UID_SHIFT) & SFS_OWNER_MAX;
}

static uint32_t entry_gid(const sfs_entry_t *entry) {
    if (!entry || !entry->used) return 0;
    return (entry->reserved[1] >> SFS_GID_SHIFT) & SFS_OWNER_MAX;
}

static uint32_t owner_bits(uint32_t uid, uint32_t gid) {
    if (uid > SFS_OWNER_MAX || gid > SFS_OWNER_MAX) return 0xFFFFFFFFU;
    return (uid << SFS_UID_SHIFT) | (gid << SFS_GID_SHIFT);
}

static uint32_t sanitized_entry_mode(uint32_t type, uint32_t mode, uint32_t uid, uint32_t gid) {
    return owner_bits(uid, gid) | mode_type_bits(type) | (mode & VFS_MODE_PERM_MASK);
}

static int mode_allows(uint32_t mode, uint32_t mask) {
    if (mask & ~(VFS_ACCESS_READ | VFS_ACCESS_WRITE | VFS_ACCESS_EXEC)) return 0;
    if ((mask & VFS_ACCESS_READ) && (mode & VFS_MODE_READ_MASK) == 0) return 0;
    if ((mask & VFS_ACCESS_WRITE) && (mode & VFS_MODE_WRITE_MASK) == 0) return 0;
    if ((mask & VFS_ACCESS_EXEC) && (mode & VFS_MODE_EXEC_MASK) == 0) return 0;
    return 1;
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

static int flags_allowed_by_entry(int flags, const sfs_entry_t *entry) {
    int access = flags & VFS_O_RDWR;
    uint32_t mode = entry_mode(entry);
    if (access == VFS_O_RDONLY)
        return mode_allows_user(mode, entry_uid(entry), entry_gid(entry), VFS_ACCESS_READ);
    if (access == VFS_O_WRONLY)
        return mode_allows_user(mode, entry_uid(entry), entry_gid(entry), VFS_ACCESS_WRITE);
    if (access == VFS_O_RDWR)
        return mode_allows_user(mode,
                                entry_uid(entry),
                                entry_gid(entry),
                                VFS_ACCESS_READ | VFS_ACCESS_WRITE);
    return 0;
}

static const char *entry_basename(const char *name) {
    const char *base = name;
    while (name && *name) {
        if (*name == '/') base = name + 1;
        name++;
    }
    return base;
}

static int parent_name(const char *name, char *parent, uint32_t max) {
    uint32_t len = slen(name);
    uint32_t i = len;
    if (!name || !parent || max == 0) return -1;
    parent[0] = 0;
    while (i > 0 && name[i - 1] != '/') i--;
    if (i == 0) return 0;
    if (i - 1 >= max) return -1;
    for (len = 0; len + 1 < i; len++) parent[len] = name[len];
    parent[len] = 0;
    return 0;
}

static int parent_exists(const char *name) {
    char parent[SIMPLEFS_NAME_MAX];
    int idx;
    if (parent_name(name, parent, sizeof(parent)) < 0) return 0;
    if (parent[0] == 0) return 1;
    idx = find_entry(parent);
    return idx >= 0 && entry_type(&g_entries[idx]) == SFS_TYPE_DIR;
}

static int entry_name_terminated(const sfs_entry_t *entry);

static int find_entry_in(const sfs_entry_t entries[SIMPLEFS_MAX_FILES], const char *name) {
    uint32_t i;
    for (i = 0; i < SIMPLEFS_MAX_FILES; i++) {
        if (entries[i].used &&
            entry_name_terminated(&entries[i]) &&
            streq(entries[i].name, name)) return (int)i;
    }
    return -1;
}

static int parent_exists_in(const sfs_entry_t entries[SIMPLEFS_MAX_FILES], const char *name) {
    char parent[SIMPLEFS_NAME_MAX];
    int idx;
    if (parent_name(name, parent, sizeof(parent)) < 0) return 0;
    if (parent[0] == 0) return 1;
    idx = find_entry_in(entries, parent);
    return idx >= 0 && entry_type(&entries[idx]) == SFS_TYPE_DIR;
}

static int entry_name_terminated(const sfs_entry_t *entry) {
    uint32_t i;
    if (!entry) return 0;
    for (i = 0; i < SIMPLEFS_NAME_MAX; i++) {
        if (entry->name[i] == 0) return 1;
    }
    return 0;
}

static int entries_valid(const sfs_entry_t entries[SIMPLEFS_MAX_FILES]) {
    uint32_t i;
    if (!entries) return 0;
    for (i = 0; i < SIMPLEFS_MAX_FILES; i++) {
        const sfs_entry_t *entry = &entries[i];
        if (entry->used == 0) continue;
        if (entry->used != 1) return 0;
        if (!entry_name_terminated(entry) || !name_valid(entry->name)) return 0;
        if (find_entry_in(entries, entry->name) != (int)i) return 0;
        if (entry->reserved[0] != SFS_TYPE_FILE && entry->reserved[0] != SFS_TYPE_DIR) return 0;
        if (entry->reserved[1] &&
            ((entry->reserved[1] & VFS_MODE_TYPE_MASK) != mode_type_bits(entry->reserved[0]) ||
             (entry->reserved[1] & ~SFS_MODE_STORED_MASK) != 0)) return 0;
        if (entry->first_sector != SFS_DATA_START + i * SFS_FILE_SECTORS) return 0;
        if (entry->sector_count != SFS_FILE_SECTORS) return 0;
        if (entry->size > SFS_FILE_SECTORS * BLOCK_SECTOR_SIZE) return 0;
    }
    for (i = 0; i < SIMPLEFS_MAX_FILES; i++) {
        if (entries[i].used && !parent_exists_in(entries, entries[i].name)) return 0;
    }
    return 1;
}

static int direct_child_name(const char *dir, const char *name, const char **child) {
    uint32_t dl;
    const char *rest;
    if (!dir || !name || !child) return 0;
    if (dir[0] == 0) {
        rest = name;
    } else {
        dl = slen(dir);
        if (slen(name) <= dl || name[dl] != '/') return 0;
        {
            uint32_t i;
            for (i = 0; i < dl; i++) if (name[i] != dir[i]) return 0;
        }
        rest = name + dl + 1;
    }
    if (!rest[0]) return 0;
    {
        const char *p = rest;
        while (*p) {
            if (*p == '/') return 0;
            p++;
        }
    }
    *child = rest;
    return 1;
}

static int directory_empty(const char *name) {
    uint32_t i;
    const char *child;
    for (i = 0; i < SIMPLEFS_MAX_FILES; i++) {
        if (g_entries[i].used && direct_child_name(name, g_entries[i].name, &child)) return 0;
    }
    return 1;
}

static int name_is_descendant(const char *dir, const char *name) {
    uint32_t dir_len;
    uint32_t i;
    if (!dir || !name) return 0;
    dir_len = slen(dir);
    if (dir_len == 0) return 0;
    for (i = 0; i < dir_len; i++) {
        if (dir[i] != name[i]) return 0;
    }
    return name[dir_len] == '/';
}

static int renamed_descendant_name(const char *old_dir,
                                   const char *new_dir,
                                   const char *old_name,
                                   char *out,
                                   uint32_t max) {
    uint32_t old_len = slen(old_dir);
    uint32_t new_len = slen(new_dir);
    const char *suffix = old_name + old_len;
    uint32_t suffix_len = slen(suffix);
    uint32_t i;
    uint32_t pos = 0;
    if (!name_is_descendant(old_dir, old_name) || new_len + suffix_len >= max) return -1;
    for (i = 0; i < new_len; i++) out[pos++] = new_dir[i];
    for (i = 0; i <= suffix_len; i++) out[pos++] = suffix[i];
    return 0;
}

static int alloc_entry(const char *name, uint32_t type) {
    uint32_t i;
    uint32_t now = timer_ms();
    if (!parent_exists(name)) return -1;
    for (i = 0; i < SIMPLEFS_MAX_FILES; i++) {
        if (!g_entries[i].used) {
            memzero(&g_entries[i], sizeof(g_entries[i]));
            g_entries[i].used = 1;
            scopy(g_entries[i].name, name, SIMPLEFS_NAME_MAX);
            g_entries[i].first_sector = SFS_DATA_START + i * SFS_FILE_SECTORS;
            g_entries[i].sector_count = SFS_FILE_SECTORS;
            g_entries[i].size = 0;
            g_entries[i].reserved[0] = type;
            g_entries[i].reserved[1] = sanitized_entry_mode(type,
                                                            default_entry_mode(type),
                                                            process_uid_get(process_current()),
                                                            process_gid_get(process_current()));
            g_entries[i].reserved[2] = now;
            g_entries[i].reserved[3] = now;
            if (write_dir() < 0) {
                memzero(&g_entries[i], sizeof(g_entries[i]));
                return -1;
            }
            return (int)i;
        }
    }
    return -1;
}

static int entry_read_bytes(uint32_t entry, uint32_t pos, void *buf, uint32_t len) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    sfs_entry_t *e = &g_entries[entry];
    uint32_t done = 0;
    if (pos >= e->size) return 0;
    if (len > e->size - pos) len = e->size - pos;
    while (done < len) {
        uint32_t rel = pos + done;
        uint32_t lba = e->first_sector + rel / BLOCK_SECTOR_SIZE;
        uint32_t off = rel % BLOCK_SECTOR_SIZE;
        uint32_t n = BLOCK_SECTOR_SIZE - off;
        uint32_t i;
        if (n > len - done) n = len - done;
        if (block_read(g_dev, lba, sector, 1) != 1) break;
        for (i = 0; i < n; i++) ((uint8_t *)buf)[done + i] = sector[off + i];
        done += n;
    }
    return (int)done;
}

static int entry_write_bytes(uint32_t entry, uint32_t pos, const void *buf, uint32_t len) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    sfs_entry_t *e = &g_entries[entry];
    uint32_t max_size = e->sector_count * BLOCK_SECTOR_SIZE;
    uint32_t old_size = e->size;
    uint32_t done = 0;
    if (pos >= max_size) return 0;
    if (len > max_size - pos) len = max_size - pos;
    while (done < len) {
        uint32_t rel = pos + done;
        uint32_t lba = e->first_sector + rel / BLOCK_SECTOR_SIZE;
        uint32_t off = rel % BLOCK_SECTOR_SIZE;
        uint32_t n = BLOCK_SECTOR_SIZE - off;
        uint32_t i;
        if (n > len - done) n = len - done;
        if (block_read(g_dev, lba, sector, 1) != 1) break;
        for (i = 0; i < n; i++) sector[off + i] = ((const uint8_t *)buf)[done + i];
        if (block_write(g_dev, lba, sector, 1) != 1) break;
        done += n;
    }
    if (pos + done > old_size) {
        uint32_t old_mtime = e->reserved[3];
        e->size = pos + done;
        e->reserved[3] = timer_ms();
        if (write_dir() < 0) {
            e->size = old_size;
            e->reserved[3] = old_mtime;
            return -1;
        }
    }
    return (int)done;
}

static int entry_zero_bytes(uint32_t entry, uint32_t pos, uint32_t len) {
    uint8_t sector[BLOCK_SECTOR_SIZE];
    sfs_entry_t *e = &g_entries[entry];
    uint32_t max_size = e->sector_count * BLOCK_SECTOR_SIZE;
    uint32_t done = 0;
    if (pos > max_size || len > max_size - pos) return -1;
    while (done < len) {
        uint32_t rel = pos + done;
        uint32_t lba = e->first_sector + rel / BLOCK_SECTOR_SIZE;
        uint32_t off = rel % BLOCK_SECTOR_SIZE;
        uint32_t n = BLOCK_SECTOR_SIZE - off;
        uint32_t i;
        if (n > len - done) n = len - done;
        if (block_read(g_dev, lba, sector, 1) != 1) return -1;
        for (i = 0; i < n; i++) sector[off + i] = 0;
        if (block_write(g_dev, lba, sector, 1) != 1) return -1;
        done += n;
    }
    return 0;
}

static int resize_entry(uint32_t entry, uint32_t size) {
    sfs_entry_t *e;
    uint32_t max_size;
    uint32_t old_size;
    if (entry >= SIMPLEFS_MAX_FILES || !g_entries[entry].used) return -1;
    if (entry_type(&g_entries[entry]) != SFS_TYPE_FILE) return -1;
    e = &g_entries[entry];
    max_size = e->sector_count * BLOCK_SECTOR_SIZE;
    if (size > max_size) return -1;
    old_size = e->size;
    {
        uint32_t old_mtime = e->reserved[3];
        if (size > old_size && entry_zero_bytes(entry, old_size, size - old_size) < 0) return -1;
        e->size = size;
        e->reserved[3] = timer_ms();
        if (write_dir() < 0) {
            e->size = old_size;
            e->reserved[3] = old_mtime;
            return -1;
        }
    }
    return 0;
}

void simplefs_init(void) {
    g_dev = 0;
    g_mounted = 0;
    scopy(g_mount, "/disk", sizeof(g_mount));
    memzero(&g_super, sizeof(g_super));
    memzero(g_entries, sizeof(g_entries));
    close_all_fds();
    if (block_count() > 0) {
        if (simplefs_mount(0, "/disk") < 0) {
            if (simplefs_format(0) == 0 && simplefs_mount(0, "/disk") == 0) seed_default_file();
        }
    }
}

int simplefs_format(uint32_t dev) {
    const block_device_t *b = block_at(dev);
    sfs_super_t s;
    uint8_t zero[BLOCK_SECTOR_SIZE];
    uint32_t i;
    if (!b || b->sector_count < SFS_DATA_START + SIMPLEFS_MAX_FILES * SFS_FILE_SECTORS) return -1;
    if (g_mounted && g_dev == dev && any_open_fd()) return -1;
    memzero(&s, sizeof(s));
    s.magic = SFS_MAGIC;
    s.version = SFS_VERSION;
    s.sector_size = BLOCK_SECTOR_SIZE;
    s.max_files = SIMPLEFS_MAX_FILES;
    s.dir_start = SFS_DIR_START;
    s.dir_sectors = SFS_DIR_SECTORS;
    s.data_start = SFS_DATA_START;
    s.file_sectors = SFS_FILE_SECTORS;
    s.total_sectors = b->sector_count;
    memzero(zero, sizeof(zero));
    if (write_super(dev, &s) < 0) return -1;
    for (i = SFS_DIR_START; i < SFS_DATA_START + SIMPLEFS_MAX_FILES * SFS_FILE_SECTORS; i++) {
        if (block_write(dev, i, zero, 1) != 1) return -1;
    }
    if (g_mounted && g_dev == dev) {
        memzero(g_entries, sizeof(g_entries));
        close_all_fds();
        g_mounted = 0;
    }
    return 0;
}

int simplefs_mount(uint32_t dev, const char *mount_path) {
    const block_device_t *b = block_at(dev);
    sfs_super_t s;
    sfs_entry_t entries[SIMPLEFS_MAX_FILES];
    if (!b || !mount_path_valid(mount_path) || mount_path_reserved(mount_path)) return -1;
    if (g_mounted && any_open_fd()) return -1;
    if (read_super(dev, &s) < 0 || !super_valid(&s, b)) return -1;
    if (read_dir_into(dev, entries) < 0) return -1;
    if (!entries_valid(entries)) return -1;
    g_dev = dev;
    g_super = s;
    scopy(g_mount, mount_path, SIMPLEFS_MOUNT_MAX);
    memcopy((uint8_t *)g_entries, (const uint8_t *)entries, sizeof(g_entries));
    close_all_fds();
    g_mounted = 1;
    debug_puts("[simplefs] mounted ");
    debug_puts(g_mount);
    debug_puts("\n");
    return 0;
}

int simplefs_unmount(void) {
    if (!g_mounted || any_open_fd()) return -1;
    close_all_fds();
    memzero(&g_super, sizeof(g_super));
    memzero(g_entries, sizeof(g_entries));
    g_dev = 0;
    g_mount[0] = 0;
    g_mounted = 0;
    debug_puts("[simplefs] unmounted\n");
    return 0;
}

int simplefs_mounted(void) {
    return g_mounted;
}

const char *simplefs_mount_path(void) {
    return g_mount;
}

int simplefs_create(const char *path) {
    const char *name;
    int idx;
    if (!path_under_mount(path, &name) || !name_valid(name)) return -1;
    idx = find_entry(name);
    if (idx >= 0) return entry_type(&g_entries[idx]) == SFS_TYPE_FILE ? 0 : -1;
    return alloc_entry(name, SFS_TYPE_FILE) >= 0 ? 0 : -1;
}

int simplefs_mkdir(const char *path) {
    const char *name;
    int idx;
    if (!path_under_mount(path, &name) || !name_valid(name)) return -1;
    idx = find_entry(name);
    if (idx >= 0) return entry_type(&g_entries[idx]) == SFS_TYPE_DIR ? 0 : -1;
    return alloc_entry(name, SFS_TYPE_DIR) >= 0 ? 0 : -1;
}

int simplefs_unlink(const char *path) {
    const char *name;
    int idx;
    sfs_entry_t old;
    if (!path_under_mount(path, &name) || !name_valid(name)) return -1;
    idx = find_entry(name);
    if (idx < 0) return -1;
    if (entry_type(&g_entries[idx]) == SFS_TYPE_DIR && !directory_empty(name)) return -1;
    if (entry_has_open_fd((uint32_t)idx)) return -1;
    old = g_entries[idx];
    memzero(&g_entries[idx], sizeof(g_entries[idx]));
    if (write_dir() < 0) {
        g_entries[idx] = old;
        return -1;
    }
    return 0;
}

int simplefs_rename(const char *old_path, const char *new_path) {
    const char *old_name;
    const char *new_name;
    int idx;
    int moving_dir;
    uint32_t i;
    sfs_entry_t backup[SIMPLEFS_MAX_FILES];
    if (!path_under_mount(old_path, &old_name) || !name_valid(old_name)) return -1;
    if (!path_under_mount(new_path, &new_name) || !name_valid(new_name)) return -1;
    idx = find_entry(old_name);
    if (idx < 0 || find_entry(new_name) >= 0) return -1;
    if (!parent_exists(new_name)) return -1;
    moving_dir = entry_type(&g_entries[idx]) == SFS_TYPE_DIR;
    if (moving_dir && name_is_descendant(old_name, new_name)) return -1;
    if (moving_dir) {
        for (i = 0; i < SIMPLEFS_MAX_FILES; i++) {
            char renamed[SIMPLEFS_NAME_MAX];
            if (!g_entries[i].used || !name_is_descendant(old_name, g_entries[i].name)) continue;
            if (renamed_descendant_name(old_name, new_name, g_entries[i].name, renamed, sizeof(renamed)) < 0)
                return -1;
            if (find_entry(renamed) >= 0) return -1;
        }
    }
    memcopy((uint8_t *)backup, (const uint8_t *)g_entries, sizeof(backup));
    if (moving_dir) {
        for (i = 0; i < SIMPLEFS_MAX_FILES; i++) {
            char renamed[SIMPLEFS_NAME_MAX];
            if (!g_entries[i].used || !name_is_descendant(old_name, g_entries[i].name)) continue;
            if (renamed_descendant_name(old_name, new_name, g_entries[i].name, renamed, sizeof(renamed)) < 0)
                return -1;
            scopy(g_entries[i].name, renamed, SIMPLEFS_NAME_MAX);
        }
    }
    scopy(g_entries[idx].name, new_name, SIMPLEFS_NAME_MAX);
    if (write_dir() < 0) {
        memcopy((uint8_t *)g_entries, (const uint8_t *)backup, sizeof(g_entries));
        return -1;
    }
    return 0;
}

int simplefs_open(const char *path, int flags) {
    const char *name;
    int idx;
    int fd;
    if (!flags_valid(flags)) return -1;
    if (!path_under_mount(path, &name) || !name_valid(name)) return -1;
    fd = reserve_sfs_fd();
    if (fd < 0) return -1;
    idx = find_entry(name);
    if (idx < 0 && (flags & VFS_O_CREAT)) idx = alloc_entry(name, SFS_TYPE_FILE);
    if (idx < 0) {
        release_reserved_sfs_fd(fd);
        return -1;
    }
    if (entry_type(&g_entries[idx]) != SFS_TYPE_FILE) {
        release_reserved_sfs_fd(fd);
        return -1;
    }
    if (!flags_allowed_by_entry(flags, &g_entries[idx])) {
        release_reserved_sfs_fd(fd);
        return -1;
    }
    if (flags & VFS_O_TRUNC) {
        uint32_t old_size = g_entries[idx].size;
        uint32_t old_mtime = g_entries[idx].reserved[3];
        g_entries[idx].size = 0;
        g_entries[idx].reserved[3] = timer_ms();
        if (write_dir() < 0) {
            g_entries[idx].size = old_size;
            g_entries[idx].reserved[3] = old_mtime;
            release_reserved_sfs_fd(fd);
            return -1;
        }
    }
    g_fds[fd].entry = (uint32_t)idx;
    g_fds[fd].pos = (flags & VFS_O_APPEND) ? g_entries[idx].size : 0;
    g_fds[fd].flags = flags;
    return fd;
}

int simplefs_close(int fd) {
    uint32_t flags;
    if (fd < 0 || fd >= SFS_MAX_OPEN || !g_fds[fd].used) return -1;
    flags = simplefs_irq_save();
    if (!g_fds[fd].used) {
        simplefs_irq_restore(flags);
        return -1;
    }
    clear_sfs_fd(&g_fds[fd]);
    simplefs_irq_restore(flags);
    return 0;
}

int simplefs_fd_flags(int fd) {
    int flags_out;
    uint32_t flags = simplefs_irq_save();
    if (fd < 0 || fd >= SFS_MAX_OPEN || !g_fds[fd].used) {
        simplefs_irq_restore(flags);
        return -1;
    }
    flags_out = g_fds[fd].flags & (VFS_O_RDWR | VFS_O_APPEND | VFS_O_NONBLOCK);
    simplefs_irq_restore(flags);
    return flags_out;
}

int simplefs_set_fd_flags(int fd, int new_flags) {
    int access;
    uint32_t flags;
    if (new_flags & ~(VFS_O_RDWR | VFS_O_APPEND | VFS_O_NONBLOCK)) return -1;
    flags = simplefs_irq_save();
    if (fd < 0 || fd >= SFS_MAX_OPEN || !g_fds[fd].used) {
        simplefs_irq_restore(flags);
        return -1;
    }
    access = g_fds[fd].flags & VFS_O_RDWR;
    if ((new_flags & VFS_O_RDWR) != access) {
        simplefs_irq_restore(flags);
        return -1;
    }
    g_fds[fd].flags = (g_fds[fd].flags & ~(VFS_O_APPEND | VFS_O_NONBLOCK)) |
                      (new_flags & (VFS_O_APPEND | VFS_O_NONBLOCK));
    simplefs_irq_restore(flags);
    return 0;
}

int simplefs_fsync(int fd) {
    sfs_fd_t *f;
    if (fd < 0 || fd >= SFS_MAX_OPEN || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if (f->entry >= SIMPLEFS_MAX_FILES || !g_entries[f->entry].used) return -1;
    if (entry_type(&g_entries[f->entry]) != SFS_TYPE_FILE) return -1;
    return write_dir();
}

int simplefs_sync(void) {
    if (!g_mounted) return 0;
    if (write_super(g_dev, &g_super) < 0) return -1;
    return write_dir();
}

int simplefs_read(int fd, void *buf, uint32_t len) {
    sfs_fd_t *f;
    int n;
    if (fd < 0 || fd >= SFS_MAX_OPEN || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if (f->entry >= SIMPLEFS_MAX_FILES || !g_entries[f->entry].used) return -1;
    if (entry_type(&g_entries[f->entry]) != SFS_TYPE_FILE) return -1;
    if ((f->flags & VFS_O_RDONLY) == 0) return -1;
    if (len == 0) return 0;
    if (!buf) return -1;
    n = entry_read_bytes(f->entry, f->pos, buf, len);
    if (n > 0) f->pos += (uint32_t)n;
    return n;
}

int simplefs_write(int fd, const void *buf, uint32_t len) {
    sfs_fd_t *f;
    int n;
    if (fd < 0 || fd >= SFS_MAX_OPEN || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if (f->entry >= SIMPLEFS_MAX_FILES || !g_entries[f->entry].used) return -1;
    if (entry_type(&g_entries[f->entry]) != SFS_TYPE_FILE) return -1;
    if ((f->flags & VFS_O_WRONLY) == 0) return -1;
    if (len == 0) return 0;
    if (!buf) return -1;
    if (f->flags & VFS_O_APPEND) f->pos = g_entries[f->entry].size;
    n = entry_write_bytes(f->entry, f->pos, buf, len);
    if (n > 0) f->pos += (uint32_t)n;
    return n;
}

int simplefs_seek(int fd, int32_t off, int whence) {
    sfs_fd_t *f;
    int32_t base;
    int64_t next;
    if (fd < 0 || fd >= SFS_MAX_OPEN || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if (f->entry >= SIMPLEFS_MAX_FILES || !g_entries[f->entry].used) return -1;
    if (entry_type(&g_entries[f->entry]) != SFS_TYPE_FILE) return -1;
    if (whence == VFS_SEEK_SET) base = 0;
    else if (whence == VFS_SEEK_CUR) base = (int32_t)f->pos;
    else if (whence == VFS_SEEK_END) base = (int32_t)g_entries[f->entry].size;
    else return -1;
    next = (int64_t)base + (int64_t)off;
    if (next < 0 || next > 0x7FFFFFFFLL) return -1;
    f->pos = (uint32_t)next;
    return (int)f->pos;
}

int simplefs_ftruncate(int fd, uint32_t size) {
    sfs_fd_t *f;
    if (fd < 0 || fd >= SFS_MAX_OPEN || !g_fds[fd].used) return -1;
    f = &g_fds[fd];
    if ((f->flags & VFS_O_WRONLY) == 0) return -1;
    if (f->entry >= SIMPLEFS_MAX_FILES || !g_entries[f->entry].used) return -1;
    if (!mode_allows_user(entry_mode(&g_entries[f->entry]),
                          entry_uid(&g_entries[f->entry]),
                          entry_gid(&g_entries[f->entry]),
                          VFS_ACCESS_WRITE)) return -1;
    return resize_entry(f->entry, size);
}

int simplefs_truncate(const char *path, uint32_t size) {
    const char *name;
    int idx;
    if (!path_under_mount(path, &name) || !name_valid(name)) return -1;
    idx = find_entry(name);
    if (idx < 0) return -1;
    if (!mode_allows_user(entry_mode(&g_entries[idx]),
                          entry_uid(&g_entries[idx]),
                          entry_gid(&g_entries[idx]),
                          VFS_ACCESS_WRITE)) return -1;
    return resize_entry((uint32_t)idx, size);
}

int simplefs_poll(int fd, uint32_t events) {
    sfs_fd_t *f;
    uint32_t ready = 0;
    if (fd < 0 || fd >= SFS_MAX_OPEN || !g_fds[fd].used) return -1;
    if (events & ~(VFS_POLL_READ | VFS_POLL_WRITE)) return -1;
    f = &g_fds[fd];
    if (f->entry >= SIMPLEFS_MAX_FILES || !g_entries[f->entry].used) return -1;
    if (entry_type(&g_entries[f->entry]) != SFS_TYPE_FILE) return -1;
    if ((events & VFS_POLL_READ) && (f->flags & VFS_O_RDONLY)) ready |= VFS_POLL_READ;
    if ((events & VFS_POLL_WRITE) && (f->flags & VFS_O_WRONLY)) ready |= VFS_POLL_WRITE;
    return (int)ready;
}

int simplefs_readdir(const char *path, uint32_t index, vfs_dirent_t *out) {
    const char *name;
    uint32_t i;
    uint32_t seen = 0;
    int dir_idx;
    if (!out || !path_under_mount(path, &name)) return -1;
    if (name[0] != 0) {
        if (!name_valid(name)) return -1;
        dir_idx = find_entry(name);
        if (dir_idx < 0 || entry_type(&g_entries[dir_idx]) != SFS_TYPE_DIR) return -1;
    }
    for (i = 0; i < SIMPLEFS_MAX_FILES; i++) {
        const char *child;
        if (!g_entries[i].used) continue;
        if (!direct_child_name(name, g_entries[i].name, &child)) continue;
        if (seen == index) {
            scopy(out->name, child, VFS_MAX_NAME);
            out->type = entry_type(&g_entries[i]) == SFS_TYPE_DIR ? VFS_NODE_DIR : VFS_NODE_FILE;
            out->size = g_entries[i].size;
            out->mode = entry_mode(&g_entries[i]);
            out->uid = entry_uid(&g_entries[i]);
            out->gid = entry_gid(&g_entries[i]);
            out->created_ms = g_entries[i].reserved[2];
            out->modified_ms = g_entries[i].reserved[3];
            out->accessed_ms = g_entries[i].reserved[3];
            return 0;
        }
        seen++;
    }
    return -1;
}

int simplefs_stat(const char *path, vfs_dirent_t *out) {
    const char *name;
    int idx;
    if (!out || !path_under_mount(path, &name)) return -1;
    if (name[0] == 0) {
        scopy(out->name, g_mount, VFS_MAX_NAME);
        out->type = VFS_NODE_DIR;
        out->size = 0;
        out->mode = VFS_MODE_IFDIR | 0000755U;
        out->uid = 0;
        out->gid = 0;
        out->created_ms = 0;
        out->modified_ms = 0;
        out->accessed_ms = 0;
        return 0;
    }
    if (!name_valid(name)) return -1;
    idx = find_entry(name);
    if (idx < 0) return -1;
    scopy(out->name, entry_basename(g_entries[idx].name), VFS_MAX_NAME);
    out->type = entry_type(&g_entries[idx]) == SFS_TYPE_DIR ? VFS_NODE_DIR : VFS_NODE_FILE;
    out->size = g_entries[idx].size;
    out->mode = entry_mode(&g_entries[idx]);
    out->uid = entry_uid(&g_entries[idx]);
    out->gid = entry_gid(&g_entries[idx]);
    out->created_ms = g_entries[idx].reserved[2];
    out->modified_ms = g_entries[idx].reserved[3];
    out->accessed_ms = g_entries[idx].reserved[3];
    return 0;
}

int simplefs_statfs(const char *path, vfs_fsinfo_t *out) {
    const char *name;
    uint32_t used = 0;
    uint32_t i;
    if (!out || !path_under_mount(path, &name)) return -1;
    if (name[0] && !name_valid(name)) return -1;
    for (i = 0; i < SIMPLEFS_MAX_FILES; i++) {
        if (g_entries[i].used) used++;
    }
    scopy(out->name, "simplefs", VFS_FS_NAME_MAX);
    out->block_size = BLOCK_SECTOR_SIZE;
    out->total_blocks = SIMPLEFS_MAX_FILES * SFS_FILE_SECTORS;
    out->free_blocks = (SIMPLEFS_MAX_FILES - used) * SFS_FILE_SECTORS;
    out->total_files = SIMPLEFS_MAX_FILES;
    out->free_files = SIMPLEFS_MAX_FILES - used;
    return 0;
}

int simplefs_access(const char *path, uint32_t mask) {
    const char *name;
    int idx;
    if (mask & ~(VFS_ACCESS_READ | VFS_ACCESS_WRITE | VFS_ACCESS_EXEC)) return -1;
    if (!path_under_mount(path, &name)) return -1;
    if (name[0] == 0) return mode_allows(VFS_MODE_IFDIR | 0000755U, mask) ? 0 : -1;
    if (!name_valid(name)) return -1;
    idx = find_entry(name);
    if (idx < 0) return -1;
    return mode_allows_user(entry_mode(&g_entries[idx]),
                            entry_uid(&g_entries[idx]),
                            entry_gid(&g_entries[idx]),
                            mask) ? 0 : -1;
}

int simplefs_chmod(const char *path, uint32_t mode) {
    const char *name;
    int idx;
    uint32_t old_mode;
    uint32_t old_mtime;
    uint32_t type;
    if (mode & ~VFS_MODE_PERM_MASK) return -1;
    if (!path_under_mount(path, &name) || !name_valid(name)) return -1;
    idx = find_entry(name);
    if (idx < 0) return -1;
    if (process_uid_get(process_current()) != 0 &&
        process_uid_get(process_current()) != entry_uid(&g_entries[idx])) return -1;
    type = entry_type(&g_entries[idx]);
    old_mode = g_entries[idx].reserved[1];
    old_mtime = g_entries[idx].reserved[3];
    g_entries[idx].reserved[1] = sanitized_entry_mode(type,
                                                       mode,
                                                       entry_uid(&g_entries[idx]),
                                                       entry_gid(&g_entries[idx]));
    g_entries[idx].reserved[3] = timer_ms();
    if (write_dir() < 0) {
        g_entries[idx].reserved[1] = old_mode;
        g_entries[idx].reserved[3] = old_mtime;
        return -1;
    }
    return 0;
}

int simplefs_chown(const char *path, uint32_t uid, uint32_t gid) {
    const char *name;
    int idx;
    uint32_t old_mode;
    uint32_t old_mtime;
    uint32_t type;
    if (uid > SFS_OWNER_MAX || gid > SFS_OWNER_MAX) return -1;
    if (process_uid_get(process_current()) != 0) return -1;
    if (!path_under_mount(path, &name) || !name_valid(name)) return -1;
    idx = find_entry(name);
    if (idx < 0) return -1;
    type = entry_type(&g_entries[idx]);
    old_mode = g_entries[idx].reserved[1];
    old_mtime = g_entries[idx].reserved[3];
    g_entries[idx].reserved[1] = sanitized_entry_mode(type, entry_mode(&g_entries[idx]), uid, gid);
    g_entries[idx].reserved[3] = timer_ms();
    if (write_dir() < 0) {
        g_entries[idx].reserved[1] = old_mode;
        g_entries[idx].reserved[3] = old_mtime;
        return -1;
    }
    return 0;
}

int simplefs_utime(const char *path, uint32_t accessed_ms, uint32_t modified_ms) {
    const char *name;
    int idx;
    uint32_t old_mtime;
    (void)accessed_ms;
    if (!path_under_mount(path, &name) || !name_valid(name)) return -1;
    idx = find_entry(name);
    if (idx < 0) return -1;
    old_mtime = g_entries[idx].reserved[3];
    g_entries[idx].reserved[3] = modified_ms;
    if (write_dir() < 0) {
        g_entries[idx].reserved[3] = old_mtime;
        return -1;
    }
    return 0;
}

static void seed_default_file(void) {
    static const char msg[] = "This file is stored on simplefs.\n";
    int fd;
    if (simplefs_create("/disk/welcome.txt") < 0) return;
    fd = simplefs_open("/disk/welcome.txt", VFS_O_RDWR);
    if (fd < 0) return;
    (void)simplefs_write(fd, msg, sizeof(msg) - 1);
    (void)simplefs_close(fd);
}
