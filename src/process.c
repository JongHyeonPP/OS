#include "process.h"
#include "debug.h"
#include "elf.h"
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "task.h"
#include "uts.h"
#include "usermode.h"
#include "vfs.h"

static process_t g_proc[PROCESS_MAX];
static uint32_t g_next_pid = 1;

static void user_task_trampoline(void);

static uint32_t process_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\npop %0\ncli" : "=r"(flags) : : "memory");
    return flags;
}

static void process_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0\npopfl" : : "r"(flags) : "memory");
}

static void copy_name(char *dst, const char *src) {
    int i = 0;
    while (src && src[i] && i + 1 < PROCESS_NAME_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void copy_path(char *dst, const char *src) {
    uint32_t i = 0;
    while (src && src[i] && i + 1 < PROCESS_CWD_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static uint32_t path_len(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void append_path_char(char *buf, uint32_t *pos, uint32_t max, char ch) {
    if (*pos + 1 >= max) return;
    buf[*pos] = ch;
    (*pos)++;
    buf[*pos] = 0;
}

static void append_path_part(char *buf, uint32_t *pos, uint32_t max,
                             const char *s, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) append_path_char(buf, pos, max, s[i]);
}

static int component_is(const char *s, uint32_t len, const char *lit) {
    uint32_t i = 0;
    while (i < len && lit[i] && s[i] == lit[i]) i++;
    return i == len && lit[i] == 0;
}

static void pop_path_component(char *buf, uint32_t *pos) {
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

static uint32_t page_round_up(uint32_t value) {
    return (value + PAGE_SIZE - 1U) & ~(PAGE_SIZE - 1U);
}

static uint32_t page_round_down(uint32_t value) {
    return value & ~(PAGE_SIZE - 1U);
}

static void zero_page(void *page) {
    uint32_t *p = (uint32_t *)page;
    uint32_t i;
    for (i = 0; i < PAGE_SIZE / sizeof(uint32_t); i++) p[i] = 0;
}

static void reset_fds(process_t *proc) {
    uint32_t i;
    for (i = 0; i < PROCESS_MAX_FDS; i++) {
        proc->fds[i] = -1;
        proc->fd_flags[i] = 0;
    }
}

static void clear_env(process_t *proc) {
    uint32_t i;
    uint32_t j;
    if (!proc) return;
    for (i = 0; i < PROCESS_MAX_ENV; i++) {
        for (j = 0; j < PROCESS_ENV_MAX; j++) proc->env[i][j] = 0;
    }
}

static uint32_t bounded_env_len(const char *s) {
    uint32_t n = 0;
    while (s && n < PROCESS_ENV_MAX && s[n]) n++;
    return n;
}

static int env_name_valid(const char *name, uint32_t *len) {
    uint32_t i;
    if (!name || !name[0]) return 0;
    for (i = 0; i < PROCESS_ENV_MAX; i++) {
        char ch = name[i];
        if (ch == 0) {
            if (len) *len = i;
            return i > 0;
        }
        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_')) return 0;
        if (i == 0 && ch >= '0' && ch <= '9') return 0;
    }
    return 0;
}

static int env_key_matches(const char *entry, const char *name, uint32_t name_len) {
    uint32_t i;
    if (!entry || !entry[0]) return 0;
    for (i = 0; i < name_len; i++) {
        if (entry[i] != name[i]) return 0;
    }
    return entry[name_len] == '=';
}

static int env_find_index(const process_t *proc, const char *name, uint32_t name_len) {
    uint32_t i;
    if (!proc) return -1;
    for (i = 0; i < PROCESS_MAX_ENV; i++) {
        if (env_key_matches(proc->env[i], name, name_len)) return (int)i;
    }
    return -1;
}

static int env_find_free_index(const process_t *proc) {
    uint32_t i;
    if (!proc) return -1;
    for (i = 0; i < PROCESS_MAX_ENV; i++) {
        if (!proc->env[i][0]) return (int)i;
    }
    return -1;
}

static void env_copy_entry(char *dst, const char *name, uint32_t name_len, const char *value) {
    uint32_t i;
    uint32_t pos = 0;
    for (i = 0; i < name_len && pos + 1U < PROCESS_ENV_MAX; i++) dst[pos++] = name[i];
    if (pos + 1U < PROCESS_ENV_MAX) dst[pos++] = '=';
    for (i = 0; value && value[i] && pos + 1U < PROCESS_ENV_MAX; i++) dst[pos++] = value[i];
    dst[pos] = 0;
}

static void copy_env(process_t *dst, const process_t *src) {
    uint32_t i;
    if (!dst) return;
    clear_env(dst);
    if (!src) return;
    for (i = 0; i < PROCESS_MAX_ENV; i++) {
        uint32_t j;
        for (j = 0; j < PROCESS_ENV_MAX; j++) dst->env[i][j] = src->env[i][j];
    }
}

static int pid_in_use(uint32_t pid) {
    int i;
    for (i = 0; i < PROCESS_MAX; i++) {
        if (g_proc[i].state != PROC_UNUSED && g_proc[i].pid == pid) return 1;
    }
    return 0;
}

static uint32_t alloc_pid(void) {
    uint32_t attempts = 0;
    while (attempts < 0xFFFFFFFFU) {
        uint32_t pid = g_next_pid++;
        if (g_next_pid == 0) g_next_pid = 1;
        attempts++;
        if (pid != 0 && !pid_in_use(pid)) return pid;
    }
    return 0;
}

static void close_process_fds(process_t *proc) {
    uint32_t i;
    for (i = 3; i < PROCESS_MAX_FDS; i++) {
        int vfs_fd = -1;
        uint32_t flags = process_irq_save();
        if (proc->fds[i] >= 0) {
            vfs_fd = proc->fds[i];
            proc->fds[i] = -1;
            proc->fd_flags[i] = 0;
        }
        process_irq_restore(flags);
        if (vfs_fd >= 0) vfs_close(vfs_fd);
    }
}

static void close_exec_fds(process_t *proc) {
    uint32_t i;
    if (!proc) return;
    for (i = 3; i < PROCESS_MAX_FDS; i++) {
        int vfs_fd = -1;
        uint32_t flags = process_irq_save();
        if (proc->fds[i] >= 0 && (proc->fd_flags[i] & PROCESS_FD_CLOEXEC)) {
            vfs_fd = proc->fds[i];
            proc->fds[i] = -1;
            proc->fd_flags[i] = 0;
        }
        process_irq_restore(flags);
        if (vfs_fd >= 0) vfs_close(vfs_fd);
    }
}

static void reparent_children(uint32_t old_ppid) {
    uint32_t i;
    if (old_ppid == 0) return;
    for (i = 0; i < PROCESS_MAX; i++) {
        if (g_proc[i].state != PROC_UNUSED &&
            g_proc[i].state != PROC_CREATING &&
            g_proc[i].ppid == old_ppid) {
            g_proc[i].ppid = 0;
        }
    }
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}

static uint32_t bounded_arg_len(const char *s) {
    uint32_t n = 0;
    while (s && n < PROCESS_ARG_MAX && s[n]) n++;
    return n;
}

static int write_user_bytes(uint32_t *dir, uint32_t virt, const void *src, uint32_t len) {
    uint32_t copied = 0;
    const uint8_t *bytes = (const uint8_t *)src;
    if (len == 0) return 0;
    if (!bytes || virt > 0xFFFFFFFFU - (len - 1U)) return -1;
    while (copied < len) {
        uint32_t cur = virt + copied;
        uint32_t phys = paging_get_phys_in_directory(dir, cur);
        uint32_t page_left;
        uint32_t chunk;
        if (!phys) return -1;
        page_left = PAGE_SIZE - (cur & (PAGE_SIZE - 1U));
        chunk = len - copied;
        if (chunk > page_left) chunk = page_left;
        copy_bytes((uint8_t *)phys, bytes + copied, chunk);
        copied += chunk;
    }
    return 0;
}

static int write_user_u32(uint32_t *dir, uint32_t virt, uint32_t value) {
    return write_user_bytes(dir, virt, &value, sizeof(value));
}

static int setup_user_stack(process_t *proc,
                            uint32_t *dir,
                            uint32_t argc,
                            const char *const argv[]) {
    const char *local_argv[PROCESS_MAX_ARGS];
    uint32_t user_arg_ptrs[PROCESS_MAX_ARGS];
    const char *local_env[PROCESS_MAX_ENV];
    uint32_t user_env_ptrs[PROCESS_MAX_ENV];
    uint32_t i;
    uint32_t envc = 0;
    uint32_t sp = USER_STACK_TOP;
    uint32_t argv_addr;
    uint32_t envp_addr;
    uint32_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    if (!proc || !dir) return -1;
    if (argc == 0) {
        argc = 1;
        local_argv[0] = proc->name;
    } else {
        if (!argv || argc > PROCESS_MAX_ARGS) return -1;
        for (i = 0; i < argc; i++) {
            if (!argv[i]) return -1;
            local_argv[i] = argv[i];
        }
    }
    for (i = 0; i < PROCESS_MAX_ENV; i++) {
        const char *entry = process_env_entry(proc, i);
        if (!entry) break;
        local_env[envc++] = entry;
    }
    for (i = envc; i > 0; i--) {
        uint32_t env_index = i - 1;
        uint32_t len = bounded_env_len(local_env[env_index]);
        if (len >= PROCESS_ENV_MAX) return -1;
        if (sp < stack_base + len + 1U) return -1;
        sp -= len + 1U;
        if (write_user_bytes(dir, sp, local_env[env_index], len + 1U) < 0) return -1;
        user_env_ptrs[env_index] = sp;
    }
    for (i = argc; i > 0; i--) {
        uint32_t arg_index = i - 1;
        uint32_t len = bounded_arg_len(local_argv[arg_index]);
        if (len >= PROCESS_ARG_MAX) return -1;
        if (sp < stack_base + len + 1U) return -1;
        sp -= len + 1U;
        if (write_user_bytes(dir, sp, local_argv[arg_index], len + 1U) < 0) return -1;
        user_arg_ptrs[arg_index] = sp;
    }
    sp &= ~3U;
    if (sp < stack_base + 4U) return -1;
    sp -= 4U;
    if (write_user_u32(dir, sp, 0) < 0) return -1;
    for (i = envc; i > 0; i--) {
        uint32_t env_index = i - 1;
        if (sp < stack_base + 4U) return -1;
        sp -= 4U;
        if (write_user_u32(dir, sp, user_env_ptrs[env_index]) < 0) return -1;
    }
    envp_addr = sp;
    if (sp < stack_base + 4U) return -1;
    sp -= 4U;
    if (write_user_u32(dir, sp, 0) < 0) return -1;
    for (i = argc; i > 0; i--) {
        uint32_t arg_index = i - 1;
        if (sp < stack_base + 4U) return -1;
        sp -= 4U;
        if (write_user_u32(dir, sp, user_arg_ptrs[arg_index]) < 0) return -1;
    }
    argv_addr = sp;
    if (sp < stack_base + 12U) return -1;
    sp -= 4U;
    if (write_user_u32(dir, sp, envp_addr) < 0) return -1;
    sp -= 4U;
    if (write_user_u32(dir, sp, argv_addr) < 0) return -1;
    sp -= 4U;
    if (write_user_u32(dir, sp, argc) < 0) return -1;
    proc->argc = argc;
    proc->user_stack = sp;
    return 0;
}

typedef struct {
    uint32_t *page_dir;
} user_load_ctx_t;

typedef struct {
    char name[PROCESS_NAME_MAX];
    uint32_t *dir;
    uint32_t entry;
    uint32_t user_stack;
    uint32_t heap_start;
    uint32_t heap_break;
    uint32_t argc;
} prepared_user_image_t;

static int map_segment_page(user_load_ctx_t *ctx,
                            uint32_t virt_page,
                            uint32_t page_flags,
                            const uint8_t *src,
                            uint32_t src_off,
                            uint32_t src_len,
                            uint32_t dst_off) {
    void *phys;
    if (paging_get_phys_in_directory(ctx->page_dir, virt_page)) return -1;
    phys = pmm_alloc();
    if (!phys) return -1;
    zero_page(phys);
    if (src && src_len) copy_bytes((uint8_t *)phys + dst_off, src + src_off, src_len);
    if (paging_map_user_page(ctx->page_dir, virt_page, (uint32_t)phys, page_flags) < 0) {
        pmm_free(phys);
        return -1;
    }
    return 0;
}

static int load_user_segment(void *opaque,
                             uint32_t vaddr,
                             uint32_t memsz,
                             uint32_t flags,
                             const uint8_t *data,
                             uint32_t filesz) {
    user_load_ctx_t *ctx = (user_load_ctx_t *)opaque;
    uint32_t start;
    uint32_t end;
    uint32_t seg_end;
    uint32_t file_end;
    uint32_t rounded_end;
    uint32_t page_flags = PAGE_USER;
    uint32_t page;
    if (memsz == 0) return 0;
    if (vaddr > 0xFFFFFFFFU - memsz) return -1;
    seg_end = vaddr + memsz;
    if (vaddr > 0xFFFFFFFFU - filesz) return -1;
    file_end = vaddr + filesz;
    if (seg_end > 0xFFFFFFFFU - (PAGE_SIZE - 1)) return -1;
    rounded_end = seg_end + PAGE_SIZE - 1;
    start = vaddr & ~(PAGE_SIZE - 1);
    end = rounded_end & ~(PAGE_SIZE - 1);
    if (flags & ELF_PF_W) page_flags |= PAGE_WRITE;
    if (vaddr < USER_BASE || end > (USER_STACK_TOP - USER_STACK_SIZE)) return -1;
    for (page = start; page < end; page += PAGE_SIZE) {
        uint32_t copy_start = page;
        uint32_t copy_end = page + PAGE_SIZE;
        uint32_t src_off = 0;
        uint32_t src_len = 0;
        uint32_t dst_off = 0;
        if (copy_start < vaddr) copy_start = vaddr;
        if (copy_end > file_end) copy_end = file_end;
        if (copy_end > copy_start) {
            src_off = copy_start - vaddr;
            src_len = copy_end - copy_start;
            dst_off = copy_start - page;
        }
        if (map_segment_page(ctx, page, page_flags, data, src_off, src_len, dst_off) < 0) return -1;
    }
    return 0;
}

static int map_user_stack(uint32_t *dir) {
    uint32_t page;
    for (page = USER_STACK_TOP - USER_STACK_SIZE; page < USER_STACK_TOP; page += PAGE_SIZE) {
        void *phys = pmm_alloc();
        if (!phys) return -1;
        zero_page(phys);
        if (paging_map_user_page(dir, page, (uint32_t)phys, PAGE_USER | PAGE_WRITE) < 0) {
            pmm_free(phys);
            return -1;
        }
    }
    return 0;
}

static int validate_user_argv(uint32_t argc, const char *const argv[]) {
    uint32_t i;
    if (argc > PROCESS_MAX_ARGS) return -1;
    if (argc > 0 && !argv) return -1;
    for (i = 0; i < argc; i++) {
        if (!argv[i] || bounded_arg_len(argv[i]) >= PROCESS_ARG_MAX) return -1;
    }
    return 0;
}

static int prepare_user_image_for_process(const process_t *base,
                                          const char *name,
                                          const void *image,
                                          uint32_t size,
                                          uint32_t argc,
                                          const char *const argv[],
                                          prepared_user_image_t *out) {
    elf_image_t meta;
    process_t temp;
    user_load_ctx_t ctx;
    uint32_t *dir;
    if (!base || !out || validate_user_argv(argc, argv) < 0) return -1;
    if (elf_load32_metadata(image, size, &meta) < 0) return -1;
    if (meta.low_vaddr < USER_BASE || meta.high_vaddr > (USER_STACK_TOP - USER_STACK_SIZE)) {
        debug_puts("[proc] rejected user image outside user range\n");
        return -1;
    }
    dir = paging_clone_kernel_directory();
    if (!dir) return -1;
    ctx.page_dir = dir;
    if (elf_load32_segments(image, size, load_user_segment, &ctx) < 0 ||
        map_user_stack(dir) < 0) {
        paging_destroy_user_directory(dir);
        return -1;
    }
    temp = *base;
    copy_name(temp.name, name ? name : "user");
    if (meta.entry < meta.low_vaddr || meta.entry >= meta.high_vaddr) {
        paging_destroy_user_directory(dir);
        return -1;
    }
    temp.entry = meta.entry;
    temp.page_dir = (uint32_t)dir;
    temp.heap_start = page_round_up(meta.high_vaddr);
    temp.heap_break = temp.heap_start;
    if (setup_user_stack(&temp, dir, argc, argv) < 0) {
        paging_destroy_user_directory(dir);
        return -1;
    }
    copy_name(out->name, temp.name);
    out->dir = dir;
    out->entry = temp.entry;
    out->user_stack = temp.user_stack;
    out->heap_start = temp.heap_start;
    out->heap_break = temp.heap_break;
    out->argc = temp.argc;
    return 0;
}

static void *read_exec_file(const char *path, uint32_t *size_out) {
    vfs_dirent_t st;
    int fd;
    int n;
    void *buf;
    if (!path || !size_out ||
        vfs_stat(path, &st) < 0 ||
        st.type != VFS_NODE_FILE ||
        st.size == 0 ||
        (st.mode & VFS_MODE_EXEC_MASK) == 0 ||
        vfs_access(path, VFS_ACCESS_EXEC) < 0 ||
        st.size > 64 * 1024) return 0;
    buf = kmalloc(st.size);
    if (!buf) return 0;
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        kfree(buf);
        return 0;
    }
    n = vfs_read(fd, buf, st.size);
    vfs_close(fd);
    if (n != (int)st.size) {
        kfree(buf);
        return 0;
    }
    *size_out = st.size;
    return buf;
}

static void update_process_tasks_address_space(uint32_t pid, uint32_t cr3) {
    uint32_t i;
    for (i = 0; i < task_table_size(); i++) {
        const task_t *task = task_at(i);
        if (task && task->process_id == pid) (void)task_set_address_space((int)i, cr3);
    }
}

static process_t *alloc_proc(void) {
    int i;
    uint32_t flags = process_irq_save();
    process_t *parent = process_current();
    for (i = 0; i < PROCESS_MAX; i++) {
        if (g_proc[i].state == PROC_UNUSED) {
            uint32_t pid = alloc_pid();
            if (pid == 0) {
                process_irq_restore(flags);
                return 0;
            }
            g_proc[i].pid = pid;
            g_proc[i].ppid = parent ? parent->pid : 0;
            g_proc[i].pgid = (parent && parent->pid != 0) ? parent->pgid : pid;
            g_proc[i].sid = (parent && parent->pid != 0) ? parent->sid : pid;
            g_proc[i].state = PROC_CREATING;
            g_proc[i].entry = 0;
            g_proc[i].user_stack = 0;
            g_proc[i].page_dir = 0;
            g_proc[i].heap_start = 0;
            g_proc[i].heap_break = 0;
            g_proc[i].argc = 0;
            g_proc[i].umask = parent ? parent->umask : 0000022U;
            g_proc[i].uid = parent ? parent->uid : 0;
            g_proc[i].gid = parent ? parent->gid : 0;
            g_proc[i].exit_code = 0;
            reset_fds(&g_proc[i]);
            copy_path(g_proc[i].cwd, parent ? parent->cwd : "/");
            copy_env(&g_proc[i], parent);
            g_proc[i].name[0] = 0;
            process_irq_restore(flags);
            return &g_proc[i];
        }
    }
    process_irq_restore(flags);
    return 0;
}

static int create_ready_task(process_t *proc, void (*entry)(void), uint32_t cr3) {
    uint32_t flags;
    int task_id;
    if (!proc || !entry || proc->state != PROC_CREATING) return -1;
    flags = process_irq_save();
    task_id = task_create_for_process(proc->name, entry, proc->pid, cr3);
    if (task_id >= 0) proc->state = PROC_READY;
    process_irq_restore(flags);
    return task_id;
}

static void user_task_trampoline(void) {
    process_t *p = process_find(task_current_process_id());
    if (p && p->state == PROC_READY && p->page_dir && p->entry)
        (void)process_enter_user(p);
    task_exit();
}

void process_init(void) {
    int i;
    for (i = 0; i < PROCESS_MAX; i++) g_proc[i].state = PROC_UNUSED;
    g_next_pid = 1;
    g_proc[0].pid = 0;
    g_proc[0].ppid = 0;
    g_proc[0].pgid = 0;
    g_proc[0].sid = 0;
    g_proc[0].state = PROC_RUNNING;
    g_proc[0].entry = 0;
    g_proc[0].user_stack = 0;
    g_proc[0].page_dir = 0;
    g_proc[0].heap_start = 0;
    g_proc[0].heap_break = 0;
    g_proc[0].argc = 0;
    g_proc[0].umask = 0000022U;
    g_proc[0].uid = 0;
    g_proc[0].gid = 0;
    g_proc[0].exit_code = 0;
    reset_fds(&g_proc[0]);
    copy_path(g_proc[0].cwd, "/home/root");
    clear_env(&g_proc[0]);
    (void)process_env_set(&g_proc[0], "PATH", "/bin", 1);
    (void)process_env_set(&g_proc[0], "HOME", "/home/root", 1);
    (void)process_env_set(&g_proc[0], "USER", "root", 1);
    (void)process_env_set(&g_proc[0], "SHELL", "/bin/sh", 1);
    (void)process_env_set(&g_proc[0], "HOSTNAME", uts_nodename(), 1);
    copy_name(g_proc[0].name, "kernel");
    debug_puts("[proc] table initialized\n");
}

process_t *process_create_kernel(const char *name, void (*entry)(void)) {
    process_t *p;
    int task_id;
    if (!entry) return 0;
    p = alloc_proc();
    if (!p) return 0;
    copy_name(p->name, name ? name : "kthread");
    p->entry = (uint32_t)entry;
    task_id = create_ready_task(p, entry, 0);
    if (task_id < 0) {
        p->state = PROC_UNUSED;
        return 0;
    }
    return p;
}

process_t *process_create_user_image(const char *name, const void *image, uint32_t size) {
    const char *argv[1];
    argv[0] = name ? name : "user";
    return process_create_user_image_args(name, image, size, 1, argv);
}

process_t *process_create_user_image_args(const char *name,
                                          const void *image,
                                          uint32_t size,
                                          uint32_t argc,
                                          const char *const argv[]) {
    process_t *p;
    prepared_user_image_t prepared;
    p = alloc_proc();
    if (!p) {
        return 0;
    }
    if (prepare_user_image_for_process(p, name, image, size, argc, argv, &prepared) < 0) {
        p->state = PROC_UNUSED;
        return 0;
    }
    copy_name(p->name, prepared.name);
    p->entry = prepared.entry;
    p->page_dir = (uint32_t)prepared.dir;
    p->heap_start = prepared.heap_start;
    p->heap_break = prepared.heap_break;
    p->user_stack = prepared.user_stack;
    p->argc = prepared.argc;
    {
        int task_id = create_ready_task(p, user_task_trampoline, (uint32_t)prepared.dir);
        if (task_id < 0) {
            p->state = PROC_UNUSED;
            paging_destroy_user_directory(prepared.dir);
            return 0;
        }
    }
    debug_puts("[proc] user image accepted: ");
    debug_puts(p->name);
    debug_puts(" entry=");
    debug_hex32(p->entry);
    debug_puts("\n");
    return p;
}

process_t *process_spawn_path(const char *path) {
    const char *argv[1];
    argv[0] = path;
    return process_spawn_path_args(path, 1, argv);
}

process_t *process_spawn_path_args(const char *path, uint32_t argc, const char *const argv[]) {
    uint32_t size = 0;
    void *buf;
    process_t *proc;
    buf = read_exec_file(path, &size);
    if (!buf) return 0;
    proc = process_create_user_image_args(path, buf, size, argc, argv);
    kfree(buf);
    return proc;
}

int process_replace_path_args(process_t *proc, const char *path, uint32_t argc, const char *const argv[]) {
    uint32_t size = 0;
    void *buf;
    prepared_user_image_t prepared;
    uint32_t *old_dir;
    int is_current;
    if (!proc ||
        proc->pid == 0 ||
        proc->state == PROC_UNUSED ||
        proc->state == PROC_CREATING ||
        proc->state == PROC_ZOMBIE) return -1;
    buf = read_exec_file(path, &size);
    if (!buf) return -1;
    if (prepare_user_image_for_process(proc, path, buf, size, argc, argv, &prepared) < 0) {
        kfree(buf);
        return -1;
    }
    kfree(buf);
    old_dir = (uint32_t *)proc->page_dir;
    copy_name(proc->name, prepared.name);
    proc->entry = prepared.entry;
    proc->page_dir = (uint32_t)prepared.dir;
    proc->heap_start = prepared.heap_start;
    proc->heap_break = prepared.heap_break;
    proc->user_stack = prepared.user_stack;
    proc->argc = prepared.argc;
    proc->exit_code = 0;
    close_exec_fds(proc);
    update_process_tasks_address_space(proc->pid, (uint32_t)prepared.dir);
    is_current = process_current() == proc;
    if (is_current) {
        task_set_current_address_space((uint32_t)prepared.dir);
        paging_switch_directory(prepared.dir);
    }
    if (old_dir && old_dir != prepared.dir) paging_destroy_user_directory(old_dir);
    return 0;
}

int process_exec_path_args(const char *path,
                           uint32_t argc,
                           const char *const argv[],
                           uint32_t *entry_out,
                           uint32_t *stack_out) {
    process_t *proc = process_current();
    if (!entry_out || !stack_out || process_replace_path_args(proc, path, argc, argv) < 0) return -1;
    *entry_out = proc->entry;
    *stack_out = proc->user_stack;
    return 0;
}

process_t *process_current(void) {
    process_t *p = process_find(task_current_process_id());
    return p ? p : &g_proc[0];
}

process_t *process_find(uint32_t pid) {
    int i;
    for (i = 0; i < PROCESS_MAX; i++) {
        if (g_proc[i].state != PROC_UNUSED &&
            g_proc[i].state != PROC_CREATING &&
            g_proc[i].pid == pid) return &g_proc[i];
    }
    return 0;
}

const process_t *process_at(uint32_t index) {
    if (index >= PROCESS_MAX ||
        g_proc[index].state == PROC_UNUSED ||
        g_proc[index].state == PROC_CREATING) return 0;
    return &g_proc[index];
}

const char *process_state_name(process_state_t state) {
    switch (state) {
        case PROC_UNUSED: return "unused";
        case PROC_CREATING: return "creating";
        case PROC_READY: return "ready";
        case PROC_RUNNING: return "running";
        case PROC_SLEEPING: return "sleeping";
        case PROC_ZOMBIE: return "zombie";
        default: return "unknown";
    }
}

void process_exit(int code) {
    process_t *exiting = process_current();
    if (!exiting || exiting->pid == 0) return;
    reparent_children(exiting->pid);
    exiting->exit_code = code;
    close_process_fds(exiting);
    exiting->state = PROC_ZOMBIE;
    task_set_current_process_id(0);
    task_set_current_address_space((uint32_t)paging_kernel_directory());
    paging_switch_directory(paging_kernel_directory());
}

static int process_can_signal(const process_t *current, const process_t *target) {
    if (!current || !target) return 0;
    if (current->pid == 0) return 1;
    if (current->pid == target->pid) return 1;
    return target->ppid == current->pid;
}

int process_kill(uint32_t pid, int code) {
    process_t *target;
    process_t *current;
    if (pid == 0) return -1;
    target = process_find(pid);
    if (!target || target->state == PROC_UNUSED) return -1;
    if (target->state == PROC_ZOMBIE) return 0;
    current = process_current();
    if (!process_can_signal(current, target)) return -1;
    if (current && current->pid == pid) {
        process_exit(code);
        return 0;
    }
    reparent_children(pid);
    close_process_fds(target);
    target->exit_code = code;
    target->state = PROC_ZOMBIE;
    (void)task_kill_process(pid);
    return 0;
}

int process_kill_group(uint32_t pgid, int code) {
    process_t *current = process_current();
    uint32_t self_pid = current ? current->pid : 0;
    uint32_t i;
    int found = 0;
    int self_match = 0;
    if (!current) return -1;
    if (pgid == 0) pgid = current->pgid;
    if (pgid == 0) return -1;
    for (i = 0; i < PROCESS_MAX; i++) {
        process_t *target = &g_proc[i];
        if (target->state == PROC_UNUSED ||
            target->state == PROC_CREATING ||
            target->state == PROC_ZOMBIE ||
            target->pid == 0 ||
            target->pgid != pgid) continue;
        if (!process_can_signal(current, target)) return -1;
        found = 1;
        if (target->pid == self_pid) self_match = 1;
    }
    if (!found) return -1;
    for (i = 0; i < PROCESS_MAX; i++) {
        process_t *target = &g_proc[i];
        if (target->state == PROC_UNUSED ||
            target->state == PROC_CREATING ||
            target->state == PROC_ZOMBIE ||
            target->pid == 0 ||
            target->pid == self_pid ||
            target->pgid != pgid) continue;
        if (process_kill(target->pid, code) < 0) return -1;
    }
    if (self_match) process_exit(code);
    return 0;
}

static int reap_zombie_process(process_t *p, int *code) {
    if (!p || p->state != PROC_ZOMBIE) return -1;
    if (code) *code = p->exit_code;
    if (p->page_dir) {
        paging_destroy_user_directory((uint32_t *)p->page_dir);
        p->page_dir = 0;
    }
    close_process_fds(p);
    p->state = PROC_UNUSED;
    return 1;
}

int process_wait(uint32_t pid, int *code) {
    process_t *p;
    process_t *current;
    if (pid == 0) return -1;
    p = process_find(pid);
    current = process_current();
    if (!p) return -1;
    if (current && current->pid == pid) return -1;
    if (current && current->pid != 0 && p->ppid != current->pid) return -1;
    if (p->state != PROC_ZOMBIE) return 0;
    return reap_zombie_process(p, code);
}

int process_wait_any(int *code) {
    process_t *current = process_current();
    uint32_t current_pid = current ? current->pid : 0;
    int i;
    for (i = 0; i < PROCESS_MAX; i++) {
        process_t *p = &g_proc[i];
        if (p->state != PROC_ZOMBIE) continue;
        if (p->pid == current_pid) continue;
        if (current_pid != 0 && p->ppid != current_pid) continue;
        if (reap_zombie_process(p, code) == 1) return (int)p->pid;
    }
    return 0;
}

int process_fork_user(const process_user_context_t *ctx) {
    process_t *parent = process_current();
    process_t *child;
    uint32_t *child_dir;
    task_user_context_t task_ctx;
    uint32_t i;
    int task_id;
    if (!ctx ||
        !parent ||
        parent->pid == 0 ||
        !parent->page_dir ||
        ctx->eip < USER_BASE ||
        ctx->eip >= USER_STACK_TOP ||
        ctx->user_esp < USER_BASE ||
        ctx->user_esp > USER_STACK_TOP) return -1;
    child = alloc_proc();
    if (!child) return -1;
    child_dir = paging_clone_user_directory((uint32_t *)parent->page_dir);
    if (!child_dir) {
        child->state = PROC_UNUSED;
        return -1;
    }
    copy_name(child->name, parent->name);
    child->entry = parent->entry;
    child->user_stack = ctx->user_esp;
    child->page_dir = (uint32_t)child_dir;
    child->heap_start = parent->heap_start;
    child->heap_break = parent->heap_break;
    child->argc = parent->argc;
    child->umask = parent->umask;
    child->uid = parent->uid;
    child->gid = parent->gid;
    for (i = 3; i < PROCESS_MAX_FDS; i++) {
        int parent_fd;
        uint32_t parent_fd_flags;
        uint32_t flags = process_irq_save();
        parent_fd = parent->fds[i];
        parent_fd_flags = parent->fd_flags[i];
        process_irq_restore(flags);
        if (parent_fd < 0) continue;
        if (vfs_dup(parent_fd) < 0) {
            close_process_fds(child);
            paging_destroy_user_directory(child_dir);
            child->page_dir = 0;
            child->state = PROC_UNUSED;
            return -1;
        }
        child->fds[i] = parent_fd;
        child->fd_flags[i] = parent_fd_flags & PROCESS_FD_CLOEXEC;
    }
    task_ctx.edi = ctx->edi;
    task_ctx.esi = ctx->esi;
    task_ctx.ebp = ctx->ebp;
    task_ctx.ebx = ctx->ebx;
    task_ctx.edx = ctx->edx;
    task_ctx.ecx = ctx->ecx;
    task_ctx.eax = 0;
    task_ctx.eip = ctx->eip;
    task_ctx.eflags = ctx->eflags;
    task_ctx.user_esp = ctx->user_esp;
    child->state = PROC_READY;
    task_id = task_create_user_for_process(child->name, child->pid, child->page_dir, &task_ctx);
    if (task_id < 0) {
        close_process_fds(child);
        paging_destroy_user_directory(child_dir);
        child->page_dir = 0;
        child->state = PROC_UNUSED;
        return -1;
    }
    return (int)child->pid;
}

uint32_t process_count(void) {
    uint32_t n = 0;
    int i;
    for (i = 0; i < PROCESS_MAX; i++) {
        if (g_proc[i].state != PROC_UNUSED &&
            g_proc[i].state != PROC_CREATING) n++;
    }
    return n;
}

const char *process_cwd(void) {
    process_t *proc = process_current();
    return proc ? proc->cwd : "/";
}

int process_resolve_path(const char *path, char *out, uint32_t max) {
    char tmp[PROCESS_CWD_MAX];
    const char *src;
    uint32_t i = 0;
    uint32_t pos = 0;
    uint32_t out_pos = 0;
    if (!path || !path[0] || !out || max < 2) return -1;
    tmp[0] = 0;
    if (path[0] == '/') {
        if (path_len(path) >= sizeof(tmp)) return -1;
        copy_path(tmp, path);
    } else {
        const char *cwd = process_cwd();
        uint32_t cwd_len = path_len(cwd);
        uint32_t path_part_len = path_len(path);
        if (cwd_len + 1U + path_part_len >= sizeof(tmp)) return -1;
        copy_path(tmp, cwd);
        pos = path_len(tmp);
        if (pos == 0) append_path_char(tmp, &pos, sizeof(tmp), '/');
        if (pos > 1 && tmp[pos - 1] != '/') append_path_char(tmp, &pos, sizeof(tmp), '/');
        append_path_part(tmp, &pos, sizeof(tmp), path, path_part_len);
    }
    out[0] = '/';
    out[1] = 0;
    out_pos = 1;
    src = tmp;
    while (src[i]) {
        uint32_t start;
        uint32_t len;
        while (src[i] == '/') i++;
        start = i;
        while (src[i] && src[i] != '/') i++;
        len = i - start;
        if (len == 0 || component_is(src + start, len, ".")) continue;
        if (component_is(src + start, len, "..")) {
            pop_path_component(out, &out_pos);
            continue;
        }
        if (out_pos > 1) append_path_char(out, &out_pos, max, '/');
        if (out_pos + len >= max) return -1;
        append_path_part(out, &out_pos, max, src + start, len);
    }
    if (out_pos == 0) {
        out[0] = '/';
        out[1] = 0;
    }
    return 0;
}

int process_chdir(const char *path) {
    process_t *proc = process_current();
    char resolved[PROCESS_CWD_MAX];
    vfs_dirent_t ent;
    if (!proc) return -1;
    if (process_resolve_path(path, resolved, sizeof(resolved)) < 0) return -1;
    if (vfs_stat(resolved, &ent) < 0 || ent.type != VFS_NODE_DIR) return -1;
    copy_path(proc->cwd, resolved);
    return 0;
}

int process_env_count(const process_t *proc) {
    uint32_t i;
    int count = 0;
    if (!proc) return 0;
    for (i = 0; i < PROCESS_MAX_ENV; i++) {
        if (proc->env[i][0]) count++;
    }
    return count;
}

const char *process_env_entry(const process_t *proc, uint32_t index) {
    uint32_t i;
    uint32_t seen = 0;
    if (!proc) return 0;
    for (i = 0; i < PROCESS_MAX_ENV; i++) {
        if (!proc->env[i][0]) continue;
        if (seen == index) return proc->env[i];
        seen++;
    }
    return 0;
}

const char *process_env_get(const process_t *proc, const char *name) {
    uint32_t name_len;
    int slot;
    if (!proc || !env_name_valid(name, &name_len)) return 0;
    slot = env_find_index(proc, name, name_len);
    if (slot < 0) return 0;
    return proc->env[slot] + name_len + 1U;
}

int process_env_set(process_t *proc, const char *name, const char *value, int overwrite) {
    uint32_t name_len;
    uint32_t value_len;
    int slot;
    if (!proc || !env_name_valid(name, &name_len) || !value) return -1;
    value_len = bounded_env_len(value);
    if (value_len >= PROCESS_ENV_MAX) return -1;
    if (name_len + 1U + value_len >= PROCESS_ENV_MAX) return -1;
    slot = env_find_index(proc, name, name_len);
    if (slot >= 0) {
        if (!overwrite) return 0;
    } else {
        slot = env_find_free_index(proc);
        if (slot < 0) return -1;
    }
    env_copy_entry(proc->env[slot], name, name_len, value);
    return 0;
}

int process_env_unset(process_t *proc, const char *name) {
    uint32_t name_len;
    int slot;
    if (!proc || !env_name_valid(name, &name_len)) return -1;
    slot = env_find_index(proc, name, name_len);
    if (slot < 0) return -1;
    proc->env[slot][0] = 0;
    return 0;
}

int process_env_copy_value(const process_t *proc, const char *name, char *out, uint32_t max) {
    const char *value = process_env_get(proc, name);
    uint32_t len;
    uint32_t i;
    if (!value || !out || max == 0) return -1;
    len = bounded_env_len(value);
    if (len >= PROCESS_ENV_MAX || max <= len) return -1;
    for (i = 0; i <= len; i++) out[i] = value[i];
    return (int)len;
}

uint32_t process_umask_get(const process_t *proc) {
    return proc ? (proc->umask & VFS_MODE_PERM_MASK) : 0000022U;
}

int process_umask_set(process_t *proc, uint32_t mask) {
    uint32_t old;
    if (!proc || (mask & ~VFS_MODE_PERM_MASK)) return -1;
    old = proc->umask & VFS_MODE_PERM_MASK;
    proc->umask = mask & VFS_MODE_PERM_MASK;
    return (int)old;
}

uint32_t process_uid_get(const process_t *proc) {
    return proc ? proc->uid : 0;
}

uint32_t process_gid_get(const process_t *proc) {
    return proc ? proc->gid : 0;
}

int process_uid_set(process_t *proc, uint32_t uid) {
    if (!proc) return -1;
    if (proc->pid != 0 && proc->uid != 0 && uid != proc->uid) return -1;
    proc->uid = uid;
    return 0;
}

int process_gid_set(process_t *proc, uint32_t gid) {
    if (!proc) return -1;
    if (proc->pid != 0 && proc->uid != 0 && gid != proc->gid) return -1;
    proc->gid = gid;
    return 0;
}

static int process_group_exists(uint32_t pgid, uint32_t sid) {
    uint32_t i;
    if (pgid == 0) return 0;
    for (i = 0; i < PROCESS_MAX; i++) {
        if (g_proc[i].state != PROC_UNUSED &&
            g_proc[i].state != PROC_CREATING &&
            g_proc[i].state != PROC_ZOMBIE &&
            g_proc[i].pgid == pgid &&
            g_proc[i].sid == sid) return 1;
    }
    return 0;
}

int process_getpgid(uint32_t pid) {
    process_t *proc;
    if (pid == 0) {
        proc = process_current();
    } else {
        proc = process_find(pid);
    }
    if (!proc) return -1;
    return (int)proc->pgid;
}

int process_setpgid(uint32_t pid, uint32_t pgid) {
    process_t *target;
    process_t *current = process_current();
    if (!current) return -1;
    if (pid == 0) pid = current->pid;
    target = process_find(pid);
    if (!target || target->pid == 0 || target->state == PROC_ZOMBIE) return -1;
    if (pgid == 0) pgid = target->pid;
    if (pgid == 0) return -1;
    if (current->pid != 0 &&
        target->pid != current->pid &&
        target->ppid != current->pid) return -1;
    if (current->pid != 0 && target->sid != current->sid) return -1;
    if (pgid != target->pid && !process_group_exists(pgid, target->sid)) return -1;
    target->pgid = pgid;
    return 0;
}

int process_getsid(uint32_t pid) {
    process_t *proc;
    if (pid == 0) {
        proc = process_current();
    } else {
        proc = process_find(pid);
    }
    if (!proc) return -1;
    return (int)proc->sid;
}

int process_setsid(void) {
    process_t *proc = process_current();
    if (!proc || proc->pid == 0 || proc->pid == proc->pgid) return -1;
    proc->sid = proc->pid;
    proc->pgid = proc->pid;
    return (int)proc->sid;
}

int process_sbrk(process_t *proc, int32_t increment) {
    uint32_t old_break;
    uint32_t new_break;
    uint32_t old_top;
    uint32_t new_top;
    uint32_t page;
    uint32_t heap_limit = USER_MMAP_BASE;
    if (!proc || proc->pid == 0 || !proc->page_dir || proc->heap_start == 0) return -1;
    old_break = proc->heap_break;
    if (increment == 0) return (int)old_break;
    if (increment > 0) {
        uint32_t inc = (uint32_t)increment;
        if (old_break > heap_limit || inc > heap_limit - old_break) return -1;
        new_break = old_break + inc;
        old_top = page_round_up(old_break);
        new_top = page_round_up(new_break);
        for (page = old_top; page < new_top; page += PAGE_SIZE) {
            void *phys;
            if (paging_get_phys_in_directory((uint32_t *)proc->page_dir, page)) continue;
            phys = pmm_alloc();
            if (!phys) {
                uint32_t rollback;
                for (rollback = old_top; rollback < page; rollback += PAGE_SIZE)
                    (void)paging_unmap_user_page((uint32_t *)proc->page_dir, rollback);
                return -1;
            }
            zero_page(phys);
            if (paging_map_user_page((uint32_t *)proc->page_dir,
                                     page,
                                     (uint32_t)phys,
                                     PAGE_USER | PAGE_WRITE) < 0) {
                uint32_t rollback;
                pmm_free(phys);
                for (rollback = old_top; rollback < page; rollback += PAGE_SIZE)
                    (void)paging_unmap_user_page((uint32_t *)proc->page_dir, rollback);
                return -1;
            }
        }
    } else {
        uint32_t dec = (uint32_t)(-(increment + 1)) + 1U;
        if (dec > old_break - proc->heap_start) return -1;
        new_break = old_break - dec;
        old_top = page_round_up(old_break);
        new_top = page_round_up(new_break);
        for (page = new_top; page < old_top; page += PAGE_SIZE)
            (void)paging_unmap_user_page((uint32_t *)proc->page_dir, page);
    }
    proc->heap_break = new_break;
    return (int)old_break;
}

static int mmap_range_valid(uint32_t addr, uint32_t length, uint32_t *start, uint32_t *end) {
    uint32_t rounded_start;
    uint32_t rounded_end;
    if (length == 0) return 0;
    if (addr > 0xFFFFFFFFU - (length - 1U)) return 0;
    rounded_start = page_round_down(addr);
    if (rounded_start != addr) return 0;
    if (addr + length > 0xFFFFFFFFU - (PAGE_SIZE - 1U)) return 0;
    rounded_end = page_round_up(addr + length);
    if (rounded_end <= rounded_start) return 0;
    if (rounded_start < USER_MMAP_BASE || rounded_end > USER_MMAP_TOP) return 0;
    if (start) *start = rounded_start;
    if (end) *end = rounded_end;
    return 1;
}

static int mmap_region_free(uint32_t *dir, uint32_t start, uint32_t end) {
    uint32_t page;
    for (page = start; page < end; page += PAGE_SIZE) {
        if (paging_get_flags_in_directory(dir, page) & PAGE_PRESENT) return 0;
    }
    return 1;
}

int process_mmap(process_t *proc, uint32_t length, uint32_t flags) {
    uint32_t *dir;
    uint32_t size;
    uint32_t start;
    uint32_t end;
    uint32_t page_flags = PAGE_USER;
    if (!proc || proc->pid == 0 || !proc->page_dir || length == 0) return -1;
    if (flags & ~PROCESS_MMAP_WRITE) return -1;
    if (length > USER_MMAP_TOP - USER_MMAP_BASE) return -1;
    if (length > 0xFFFFFFFFU - (PAGE_SIZE - 1U)) return -1;
    size = page_round_up(length);
    if (size == 0 || size > USER_MMAP_TOP - USER_MMAP_BASE) return -1;
    if (flags & PROCESS_MMAP_WRITE) page_flags |= PAGE_WRITE;
    dir = (uint32_t *)proc->page_dir;
    for (start = USER_MMAP_BASE; start <= USER_MMAP_TOP - size; start += PAGE_SIZE) {
        if (!mmap_region_free(dir, start, start + size)) continue;
        for (end = start; end < start + size; end += PAGE_SIZE) {
            void *phys = pmm_alloc();
            if (!phys) {
                uint32_t rollback;
                for (rollback = start; rollback < end; rollback += PAGE_SIZE)
                    (void)paging_unmap_user_page(dir, rollback);
                return -1;
            }
            zero_page(phys);
            if (paging_map_user_page(dir, end, (uint32_t)phys, page_flags) < 0) {
                uint32_t rollback;
                pmm_free(phys);
                for (rollback = start; rollback < end; rollback += PAGE_SIZE)
                    (void)paging_unmap_user_page(dir, rollback);
                return -1;
            }
        }
        return (int)start;
    }
    return -1;
}

int process_munmap(process_t *proc, uint32_t addr, uint32_t length) {
    uint32_t start;
    uint32_t end;
    uint32_t page;
    uint32_t *dir;
    if (!proc || proc->pid == 0 || !proc->page_dir) return -1;
    if (!mmap_range_valid(addr, length, &start, &end)) return -1;
    dir = (uint32_t *)proc->page_dir;
    for (page = start; page < end; page += PAGE_SIZE) {
        if ((paging_get_flags_in_directory(dir, page) & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) return -1;
    }
    for (page = start; page < end; page += PAGE_SIZE)
        (void)paging_unmap_user_page(dir, page);
    return 0;
}

int process_mprotect(process_t *proc, uint32_t addr, uint32_t length, uint32_t flags) {
    uint32_t start;
    uint32_t end;
    uint32_t page;
    uint32_t page_flags = PAGE_USER;
    uint32_t *dir;
    if (!proc || proc->pid == 0 || !proc->page_dir) return -1;
    if (flags & ~PROCESS_MMAP_WRITE) return -1;
    if (!mmap_range_valid(addr, length, &start, &end)) return -1;
    if (flags & PROCESS_MMAP_WRITE) page_flags |= PAGE_WRITE;
    dir = (uint32_t *)proc->page_dir;
    for (page = start; page < end; page += PAGE_SIZE) {
        if ((paging_get_flags_in_directory(dir, page) & (PAGE_PRESENT | PAGE_USER)) !=
            (PAGE_PRESENT | PAGE_USER)) return -1;
    }
    for (page = start; page < end; page += PAGE_SIZE) {
        if (paging_set_user_page_flags(dir, page, page_flags) < 0) return -1;
    }
    return 0;
}

int process_enter_user(process_t *proc) {
    if (!proc ||
        proc->state == PROC_UNUSED ||
        !proc->page_dir ||
        proc->entry < USER_BASE ||
        proc->entry >= USER_STACK_TOP - USER_STACK_SIZE ||
        proc->user_stack < USER_STACK_TOP - USER_STACK_SIZE ||
        proc->user_stack >= USER_STACK_TOP ||
        (proc->user_stack & 3U)) {
        return -1;
    }
    proc->state = PROC_RUNNING;
    task_set_current_process_id(proc->pid);
    task_set_current_address_space(proc->page_dir);
    paging_switch_directory((uint32_t *)proc->page_dir);
    if (usermode_enter(proc->entry, proc->user_stack) < 0) {
        task_set_current_process_id(0);
        task_set_current_address_space((uint32_t)paging_kernel_directory());
        paging_switch_directory(paging_kernel_directory());
        proc->state = PROC_READY;
    }
    return -1;
}

int process_fd_install(int vfs_fd) {
    process_t *proc = process_current();
    uint32_t flags;
    uint32_t i;
    if (!proc || vfs_fd < 0) return -1;
    flags = process_irq_save();
    for (i = 3; i < PROCESS_MAX_FDS; i++) {
        if (proc->fds[i] < 0) {
            proc->fds[i] = vfs_fd;
            proc->fd_flags[i] = 0;
            process_irq_restore(flags);
            return (int)i;
        }
    }
    process_irq_restore(flags);
    return -1;
}

int process_pipe_flags(int out_fds[2], uint32_t fd_flags) {
    int vfds[2];
    int read_fd;
    int write_fd;
    if (!out_fds) return -1;
    if (fd_flags & ~PROCESS_FD_CLOEXEC) return -1;
    if (vfs_pipe(vfds) < 0) return -1;
    read_fd = process_fd_install(vfds[0]);
    if (read_fd < 0) {
        vfs_close(vfds[0]);
        vfs_close(vfds[1]);
        return -1;
    }
    write_fd = process_fd_install(vfds[1]);
    if (write_fd < 0) {
        process_fd_close(read_fd);
        vfs_close(vfds[1]);
        return -1;
    }
    if (fd_flags) {
        if (process_fd_set_flags(read_fd, fd_flags) < 0 ||
            process_fd_set_flags(write_fd, fd_flags) < 0) {
            (void)process_fd_close(read_fd);
            (void)process_fd_close(write_fd);
            return -1;
        }
    }
    out_fds[0] = read_fd;
    out_fds[1] = write_fd;
    return 0;
}

int process_pipe(int out_fds[2]) {
    return process_pipe_flags(out_fds, 0);
}

int process_fd_dup(int fd) {
    process_t *proc;
    int vfs_fd;
    int slot = -1;
    uint32_t flags;
    uint32_t i;
    if (fd >= 0 && fd < 3) return fd;
    proc = process_current();
    if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS) return -1;
    flags = process_irq_save();
    vfs_fd = proc->fds[fd];
    if (vfs_fd < 0) {
        process_irq_restore(flags);
        return -1;
    }
    for (i = 3; i < PROCESS_MAX_FDS; i++) {
        if (proc->fds[i] < 0) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0 || vfs_dup(vfs_fd) < 0) {
        process_irq_restore(flags);
        return -1;
    }
    proc->fds[slot] = vfs_fd;
    proc->fd_flags[slot] = 0;
    process_irq_restore(flags);
    return slot;
}

int process_fd_dup_min(int fd, int min_fd) {
    process_t *proc;
    int vfs_fd;
    int slot = -1;
    uint32_t flags;
    uint32_t i;
    if (fd >= 0 && fd < 3) return -1;
    proc = process_current();
    if (!proc ||
        fd < 0 ||
        fd >= PROCESS_MAX_FDS ||
        min_fd < 3 ||
        min_fd >= PROCESS_MAX_FDS) return -1;
    flags = process_irq_save();
    vfs_fd = proc->fds[fd];
    if (vfs_fd < 0) {
        process_irq_restore(flags);
        return -1;
    }
    for (i = (uint32_t)min_fd; i < PROCESS_MAX_FDS; i++) {
        if (proc->fds[i] < 0) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0 || vfs_dup(vfs_fd) < 0) {
        process_irq_restore(flags);
        return -1;
    }
    proc->fds[slot] = vfs_fd;
    proc->fd_flags[slot] = 0;
    process_irq_restore(flags);
    return slot;
}

int process_fd_dup2(int old_fd, int new_fd) {
    process_t *proc;
    int old_vfs;
    int replaced_vfs;
    uint32_t flags;
    if (old_fd >= 0 && old_fd < 3) return -1;
    if (new_fd >= 0 && new_fd < 3) return -1;
    proc = process_current();
    if (!proc ||
        old_fd < 0 ||
        old_fd >= PROCESS_MAX_FDS ||
        new_fd < 0 ||
        new_fd >= PROCESS_MAX_FDS) return -1;
    flags = process_irq_save();
    old_vfs = proc->fds[old_fd];
    if (old_vfs < 0) {
        process_irq_restore(flags);
        return -1;
    }
    if (old_fd == new_fd) {
        process_irq_restore(flags);
        return new_fd;
    }
    if (vfs_dup(old_vfs) < 0) {
        process_irq_restore(flags);
        return -1;
    }
    replaced_vfs = proc->fds[new_fd];
    proc->fds[new_fd] = old_vfs;
    proc->fd_flags[new_fd] = 0;
    process_irq_restore(flags);
    if (replaced_vfs >= 0) (void)vfs_close(replaced_vfs);
    return new_fd;
}

int process_fd_resolve(int fd) {
    process_t *proc;
    int vfs_fd;
    uint32_t flags;
    if (fd >= 0 && fd < 3) return -1;
    proc = process_current();
    if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS) return -1;
    flags = process_irq_save();
    vfs_fd = proc->fds[fd];
    process_irq_restore(flags);
    return vfs_fd;
}

int process_fd_get_flags(int fd) {
    process_t *proc;
    uint32_t fd_flags;
    uint32_t flags;
    if (fd >= 0 && fd < 3) return -1;
    proc = process_current();
    if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS) return -1;
    flags = process_irq_save();
    if (proc->fds[fd] < 0) {
        process_irq_restore(flags);
        return -1;
    }
    fd_flags = proc->fd_flags[fd];
    process_irq_restore(flags);
    return (int)fd_flags;
}

int process_fd_set_flags(int fd, uint32_t fd_flags) {
    process_t *proc;
    uint32_t flags;
    if (fd >= 0 && fd < 3) return -1;
    if (fd_flags & ~PROCESS_FD_CLOEXEC) return -1;
    proc = process_current();
    if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS) return -1;
    flags = process_irq_save();
    if (proc->fds[fd] < 0) {
        process_irq_restore(flags);
        return -1;
    }
    proc->fd_flags[fd] = fd_flags;
    process_irq_restore(flags);
    return 0;
}

int process_fd_close(int fd) {
    process_t *proc;
    int vfs_fd;
    uint32_t flags;
    if (fd >= 0 && fd < 3) return 0;
    proc = process_current();
    if (!proc || fd < 0 || fd >= PROCESS_MAX_FDS) return -1;
    flags = process_irq_save();
    vfs_fd = proc->fds[fd];
    if (vfs_fd < 0) {
        process_irq_restore(flags);
        return -1;
    }
    proc->fds[fd] = -1;
    proc->fd_flags[fd] = 0;
    process_irq_restore(flags);
    return vfs_close(vfs_fd);
}
