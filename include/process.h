#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#define PROCESS_MAX       16
#define PROCESS_NAME_MAX  32
#define PROCESS_MAX_FDS   8
#define PROCESS_CWD_MAX   96
#define PROCESS_MAX_ARGS  4
#define PROCESS_ARG_MAX   96
#define PROCESS_MAX_ENV   8
#define PROCESS_ENV_MAX   96
#define PROCESS_FD_CLOEXEC 0x1U
#define USER_BASE         0x08000000U
#define USER_MMAP_BASE    0x70000000U
#define USER_STACK_TOP    0x80000000U
#define USER_STACK_SIZE   0x00004000U
#define USER_MMAP_TOP     (USER_STACK_TOP - USER_STACK_SIZE)
#define PROCESS_MMAP_WRITE 0x1U

typedef enum {
    PROC_UNUSED = 0,
    PROC_CREATING,
    PROC_READY,
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_ZOMBIE
} process_state_t;

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint32_t pgid;
    uint32_t sid;
    process_state_t state;
    uint32_t entry;
    uint32_t user_stack;
    uint32_t page_dir;
    uint32_t heap_start;
    uint32_t heap_break;
    uint32_t argc;
    uint32_t umask;
    uint32_t uid;
    uint32_t gid;
    int exit_code;
    int fds[PROCESS_MAX_FDS];
    uint32_t fd_flags[PROCESS_MAX_FDS];
    char cwd[PROCESS_CWD_MAX];
    char env[PROCESS_MAX_ENV][PROCESS_ENV_MAX];
    char name[PROCESS_NAME_MAX];
} process_t;

typedef struct {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eip;
    uint32_t eflags;
    uint32_t user_esp;
} process_user_context_t;

void process_init(void);
process_t *process_create_kernel(const char *name, void (*entry)(void));
process_t *process_create_user_image(const char *name, const void *image, uint32_t size);
process_t *process_create_user_image_args(const char *name,
                                          const void *image,
                                          uint32_t size,
                                          uint32_t argc,
                                          const char *const argv[]);
process_t *process_spawn_path(const char *path);
process_t *process_spawn_path_args(const char *path, uint32_t argc, const char *const argv[]);
int process_replace_path_args(process_t *proc, const char *path, uint32_t argc, const char *const argv[]);
int process_exec_path_args(const char *path,
                           uint32_t argc,
                           const char *const argv[],
                           uint32_t *entry_out,
                           uint32_t *stack_out);
process_t *process_current(void);
process_t *process_find(uint32_t pid);
const process_t *process_at(uint32_t index);
const char *process_state_name(process_state_t state);
void process_exit(int code);
int process_kill(uint32_t pid, int code);
int process_kill_group(uint32_t pgid, int code);
int process_wait(uint32_t pid, int *code);
int process_wait_any(int *code);
int process_fork_user(const process_user_context_t *ctx);
uint32_t process_count(void);
const char *process_cwd(void);
int process_chdir(const char *path);
int process_resolve_path(const char *path, char *out, uint32_t max);
int process_env_count(const process_t *proc);
const char *process_env_entry(const process_t *proc, uint32_t index);
const char *process_env_get(const process_t *proc, const char *name);
int process_env_set(process_t *proc, const char *name, const char *value, int overwrite);
int process_env_unset(process_t *proc, const char *name);
int process_env_copy_value(const process_t *proc, const char *name, char *out, uint32_t max);
uint32_t process_umask_get(const process_t *proc);
int process_umask_set(process_t *proc, uint32_t mask);
uint32_t process_uid_get(const process_t *proc);
uint32_t process_gid_get(const process_t *proc);
int process_uid_set(process_t *proc, uint32_t uid);
int process_gid_set(process_t *proc, uint32_t gid);
int process_getpgid(uint32_t pid);
int process_setpgid(uint32_t pid, uint32_t pgid);
int process_getsid(uint32_t pid);
int process_setsid(void);
int process_sbrk(process_t *proc, int32_t increment);
int process_mmap(process_t *proc, uint32_t length, uint32_t flags);
int process_munmap(process_t *proc, uint32_t addr, uint32_t length);
int process_mprotect(process_t *proc, uint32_t addr, uint32_t length, uint32_t flags);
int process_enter_user(process_t *proc);
int process_fd_install(int vfs_fd);
int process_pipe(int out_fds[2]);
int process_pipe_flags(int out_fds[2], uint32_t flags);
int process_fd_dup(int fd);
int process_fd_dup_min(int fd, int min_fd);
int process_fd_dup2(int old_fd, int new_fd);
int process_fd_resolve(int fd);
int process_fd_get_flags(int fd);
int process_fd_set_flags(int fd, uint32_t flags);
int process_fd_close(int fd);

#endif
