#include "syscall.h"
#include "block.h"
#include "driver.h"
#include "gdt.h"
#include "heap.h"
#include "net.h"
#include "pci.h"
#include "process.h"
#include "paging.h"
#include "pmm.h"
#include "simplefs.h"
#include "task.h"
#include "timer.h"
#include "tty.h"
#include "uts.h"
#include "vfs.h"
#include <stdint.h>

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
} syscall_frame_t;

/* -----------------------------------------------------------------------
 * syscall_dispatch — called from the int 0x80 assembly stub.
 *
 * The stub passes EAX (syscall number), EBX/ECX/EDX/ESI as arguments,
 * plus a pointer to the saved register/iret frame for syscalls that
 * need to redirect user return state.
 * --------------------------------------------------------------------- */
static int user_range_ok(const void *ptr, uint32_t len, int need_write) {
    uint32_t start = (uint32_t)ptr;
    uint32_t end;
    uint32_t page;
    uint32_t flags;
    if (!ptr) return 0;
    if (len == 0) len = 1;
    end = start + len - 1;
    if (end < start) return 0;
    if (start < USER_BASE || end >= USER_STACK_TOP) return 0;
    page = start & ~(PAGE_SIZE - 1);
    while (page <= end) {
        flags = paging_get_flags(page);
        if ((flags & (PAGE_PRESENT | PAGE_USER)) != (PAGE_PRESENT | PAGE_USER)) return 0;
        if (need_write && (flags & PAGE_WRITE) == 0) return 0;
        if (page > 0xFFFFFFFFU - PAGE_SIZE) break;
        page += PAGE_SIZE;
    }
    return 1;
}

static int copy_user_string(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    uint32_t base;
    if (!dst || !src || max == 0) return -1;
    base = (uint32_t)src;
    for (i = 0; i < max; i++) {
        const char *user_ch;
        if (base > 0xFFFFFFFFU - i) return -1;
        user_ch = (const char *)(base + i);
        if (!user_range_ok(user_ch, 1, 0)) return -1;
        dst[i] = *user_ch;
        if (dst[i] == 0) return 0;
    }
    dst[max - 1] = 0;
    return -1;
}

static int copy_user_path(char dst[VFS_MAX_PATH], const char *src) {
    return copy_user_string(dst, src, VFS_MAX_PATH);
}

static int copy_user_argv(uint32_t argc,
                          const char *const user_argv[],
                          char argbuf[PROCESS_MAX_ARGS][PROCESS_ARG_MAX],
                          const char *argv[PROCESS_MAX_ARGS]) {
    uint32_t i;
    if (argc > PROCESS_MAX_ARGS) return -1;
    if (argc == 0) return 0;
    if (!user_range_ok(user_argv, argc * sizeof(user_argv[0]), 0)) return -1;
    for (i = 0; i < argc; i++) {
        if (copy_user_string(argbuf[i], user_argv[i], PROCESS_ARG_MAX) < 0) return -1;
        argv[i] = argbuf[i];
    }
    return 0;
}

static int copy_to_user(void *dst, const void *src, uint32_t len) {
    uint32_t i;
    if (len == 0) return 0;
    if (!src || !user_range_ok(dst, len, 1)) return -1;
    for (i = 0; i < len; i++) ((uint8_t *)dst)[i] = ((const uint8_t *)src)[i];
    return 0;
}

static int install_open_fd(int vfs_fd, uint32_t open_flags) {
    int proc_fd = process_fd_install(vfs_fd);
    if (proc_fd < 0) {
        (void)vfs_close(vfs_fd);
        return -1;
    }
    if ((open_flags & VFS_O_CLOEXEC) &&
        process_fd_set_flags(proc_fd, PROCESS_FD_CLOEXEC) < 0) {
        (void)process_fd_close(proc_fd);
        return -1;
    }
    return proc_fd;
}

static int block_user_range_ok(const void *ptr, uint32_t sectors, int need_write) {
    uint32_t bytes;
    if (sectors == 0) return 1;
    if (sectors > 0xFFFFFFFFU / BLOCK_SECTOR_SIZE) return 0;
    bytes = sectors * BLOCK_SECTOR_SIZE;
    return user_range_ok(ptr, bytes, need_write);
}

static void copy_fixed_name(char *dst, uint32_t max, const char *src) {
    uint32_t i;
    if (!dst || max == 0) return;
    for (i = 0; i < max; i++) dst[i] = 0;
    for (i = 0; src && src[i] && i + 1U < max; i++) {
        dst[i] = src[i];
    }
}

static uint32_t process_fd_count(const process_t *proc) {
    uint32_t i;
    uint32_t count = 0;
    if (!proc) return 0;
    for (i = 0; i < PROCESS_MAX_FDS; i++) {
        if (proc->fds[i] >= 0) count++;
    }
    return count;
}

static const process_t *process_by_ordinal(uint32_t index) {
    uint32_t slot;
    uint32_t seen = 0;
    for (slot = 0; slot < PROCESS_MAX; slot++) {
        const process_t *proc = process_at(slot);
        if (!proc) continue;
        if (seen == index) return proc;
        seen++;
    }
    return 0;
}

static uint32_t task_active_count(void) {
    uint32_t i;
    uint32_t count = 0;
    for (i = 0; i < task_table_size(); i++) {
        if (task_at(i)) count++;
    }
    return count;
}

static const task_t *task_by_ordinal(uint32_t index) {
    uint32_t slot;
    uint32_t seen = 0;
    for (slot = 0; slot < task_table_size(); slot++) {
        const task_t *task = task_at(slot);
        if (!task) continue;
        if (seen == index) return task;
        seen++;
    }
    return 0;
}

static void copy_sysinfo(sys_sysinfo_t *out) {
    out->ticks = timer_ticks();
    out->free_pages = pmm_free_pages();
    out->managed_pages = pmm_managed_pages();
    out->heap_free_bytes = (uint32_t)heap_free_bytes();
    out->heap_used_bytes = (uint32_t)heap_used_bytes();
    out->heap_blocks = heap_block_count();
    out->heap_free_blocks = heap_free_block_count();
    out->process_count = process_count();
    out->task_count = task_active_count();
    out->task_free_slots = task_free_slots();
    out->irq_switches = scheduler_irq_switches();
    out->coop_switches = scheduler_coop_switches();
    out->pci_count = pci_count();
    out->block_count = block_count();
    out->netif_count = netif_count();
    out->driver_count = driver_count();
}

static void copy_utsname(sys_utsname_t *out) {
    copy_fixed_name(out->sysname, sizeof(out->sysname), uts_sysname());
    copy_fixed_name(out->nodename, sizeof(out->nodename), uts_nodename());
    copy_fixed_name(out->release, sizeof(out->release), uts_release());
    copy_fixed_name(out->version, sizeof(out->version), uts_version());
    copy_fixed_name(out->machine, sizeof(out->machine), uts_machine());
}

static int sysconf_value(uint32_t key, uint32_t *out) {
    if (!out) return -1;
    switch (key) {
        case SYS_CONF_PAGE_SIZE:
            *out = PAGE_SIZE;
            return 0;
        case SYS_CONF_PROCESS_MAX:
            *out = PROCESS_MAX;
            return 0;
        case SYS_CONF_TASK_MAX:
            *out = MAX_TASKS;
            return 0;
        case SYS_CONF_FD_MAX:
            *out = PROCESS_MAX_FDS;
            return 0;
        case SYS_CONF_ARG_MAX:
            *out = PROCESS_ARG_MAX;
            return 0;
        case SYS_CONF_PATH_MAX:
            *out = VFS_MAX_PATH;
            return 0;
        case SYS_CONF_NAME_MAX:
            *out = VFS_MAX_NAME;
            return 0;
        case SYS_CONF_NETIF_MAX:
            *out = NET_MAX_IFACES;
            return 0;
        case SYS_CONF_BLOCK_MAX:
            *out = BLOCK_MAX_DEVICES;
            return 0;
        case SYS_CONF_DRIVER_MAX:
            *out = DRIVER_MAX;
            return 0;
        case SYS_CONF_TIMER_HZ:
            *out = timer_hz();
            return 0;
        case SYS_CONF_ENV_MAX:
            *out = PROCESS_MAX_ENV;
            return 0;
        case SYS_CONF_ENV_VALUE_MAX:
            *out = PROCESS_ENV_MAX;
            return 0;
        case SYS_CONF_UTS_FIELD_MAX:
            *out = UTS_FIELD_MAX;
            return 0;
        default:
            return -1;
    }
}

static void copy_process_info(sys_process_info_t *out, const process_t *proc) {
    copy_fixed_name(out->name, sizeof(out->name), proc ? proc->name : 0);
    out->pid = proc ? proc->pid : 0;
    out->ppid = proc ? proc->ppid : 0;
    out->state = proc ? (uint32_t)proc->state : 0;
    out->entry = proc ? proc->entry : 0;
    out->user_stack = proc ? proc->user_stack : 0;
    out->heap_start = proc ? proc->heap_start : 0;
    out->heap_break = proc ? proc->heap_break : 0;
    out->argc = proc ? proc->argc : 0;
    out->fd_count = process_fd_count(proc);
    out->uid = process_uid_get(proc);
    out->gid = process_gid_get(proc);
    out->pgid = proc ? proc->pgid : 0;
    out->sid = proc ? proc->sid : 0;
}

static void copy_task_info(sys_task_info_t *out, const task_t *task) {
    copy_fixed_name(out->name, sizeof(out->name), task ? task->name : 0);
    out->id = task ? task->id : 0;
    out->state = task ? (uint32_t)task->state : 0;
    out->priority = task ? task->priority : 0;
    out->process_id = task ? task->process_id : 0;
    out->sleep_until = task ? task->sleep_until : 0;
    out->cr3 = task ? task->regs.cr3 : 0;
    out->started = task && task->started ? 1U : 0U;
}

static void copy_block_info(sys_block_info_t *out, const block_device_t *block) {
    copy_fixed_name(out->name, sizeof(out->name), block ? block->name : 0);
    out->sector_count = block ? block->sector_count : 0;
    out->sector_size = block ? block->sector_size : 0;
    out->writable = block && block->writable ? 1U : 0U;
    out->read_ops = block ? block->read_ops : 0;
    out->write_ops = block ? block->write_ops : 0;
    out->read_sectors = block ? block->read_sectors : 0;
    out->write_sectors = block ? block->write_sectors : 0;
}

static void copy_fsinfo(sys_fsinfo_t *out, const vfs_fsinfo_t *fs) {
    copy_fixed_name(out->name, sizeof(out->name), fs ? fs->name : 0);
    out->block_size = fs ? fs->block_size : 0;
    out->total_blocks = fs ? fs->total_blocks : 0;
    out->free_blocks = fs ? fs->free_blocks : 0;
    out->total_files = fs ? fs->total_files : 0;
    out->free_files = fs ? fs->free_files : 0;
}

static void copy_driver_info(sys_driver_info_t *out, const driver_t *driver) {
    copy_fixed_name(out->name, sizeof(out->name), driver ? driver->name : 0);
    out->bus = driver ? (uint32_t)driver->bus : 0;
    out->id0 = driver ? driver->id0 : 0;
    out->id1 = driver ? driver->id1 : 0;
    out->loaded = driver && driver->loaded ? 1U : 0U;
}

static void copy_pci_info(sys_pci_info_t *out, const pci_device_t *pci) {
    uint32_t i;
    out->bus = pci ? pci->bus : 0;
    out->slot = pci ? pci->slot : 0;
    out->function = pci ? pci->function : 0;
    out->class_code = pci ? pci->class_code : 0;
    out->vendor_id = pci ? pci->vendor_id : 0;
    out->device_id = pci ? pci->device_id : 0;
    out->subclass = pci ? pci->subclass : 0;
    out->prog_if = pci ? pci->prog_if : 0;
    out->revision = pci ? pci->revision : 0;
    out->header_type = pci ? pci->header_type : 0;
    for (i = 0; i < 6; i++) out->bar[i] = pci ? pci->bar[i] : 0;
}

static void copy_route_info(sys_route_info_t *out, const net_route_t *route) {
    out->dest = route ? route->dest : 0;
    out->mask = route ? route->mask : 0;
    out->gateway = route ? route->gateway : 0;
    out->ifindex = route ? route->ifindex : 0;
}

static void copy_arp_info(sys_arp_info_t *out, const net_arp_entry_t *arp) {
    uint32_t i;
    out->ifindex = arp ? arp->ifindex : 0;
    out->ipv4 = arp ? arp->ipv4 : 0;
    for (i = 0; i < 6; i++) out->mac[i] = arp ? arp->mac[i] : 0;
}

static void copy_netif_info(sys_netif_info_t *out, const netif_t *net) {
    uint32_t i;
    copy_fixed_name(out->name, sizeof(out->name), net ? net->name : 0);
    for (i = 0; i < 6; i++) out->mac[i] = net ? net->mac[i] : 0;
    out->ipv4 = net ? net->ipv4 : 0;
    out->mtu = net ? net->mtu : 0;
    out->up = net && net->up ? 1U : 0U;
    out->tx_packets = net ? net->tx_packets : 0;
    out->rx_packets = net ? net->rx_packets : 0;
}

static int add_io_count(uint32_t *total, int n) {
    if (!total || n < 0) return -1;
    if ((uint32_t)n > 0x7FFFFFFFU - *total) return -1;
    *total += (uint32_t)n;
    return 0;
}

static int validate_iovecs(const sys_iovec_t *iov, uint32_t count, int need_write) {
    uint32_t i;
    if (count == 0) return 0;
    if (count > SYS_IOV_MAX || !user_range_ok(iov, count * sizeof(iov[0]), 0)) return -1;
    for (i = 0; i < count; i++) {
        if (iov[i].len == 0) continue;
        if (!user_range_ok(iov[i].base, iov[i].len, need_write)) return -1;
    }
    return 0;
}

static int syscall_readv_impl(uint32_t fd_arg, sys_iovec_t *iov, uint32_t count) {
    uint32_t i;
    uint32_t total = 0;
    int vfs_fd = -1;
    if (validate_iovecs(iov, count, 1) < 0) return -1;
    if (count == 0) return 0;
    if (fd_arg != 0) {
        vfs_fd = process_fd_resolve((int)fd_arg);
        if (vfs_fd < 0) return -1;
    }
    for (i = 0; i < count; i++) {
        int n;
        if (iov[i].len == 0) continue;
        n = fd_arg == 0 ?
            tty_read((char *)iov[i].base, iov[i].len) :
            vfs_read(vfs_fd, iov[i].base, iov[i].len);
        if (n < 0) return total ? (int)total : -1;
        if (add_io_count(&total, n) < 0) return -1;
        if (n == 0 || (uint32_t)n < iov[i].len) break;
    }
    return (int)total;
}

static int syscall_writev_impl(uint32_t fd_arg, const sys_iovec_t *iov, uint32_t count) {
    uint32_t i;
    uint32_t total = 0;
    int vfs_fd = -1;
    if (validate_iovecs(iov, count, 0) < 0) return -1;
    if (count == 0) return 0;
    if (fd_arg != 1 && fd_arg != 2) {
        vfs_fd = process_fd_resolve((int)fd_arg);
        if (vfs_fd < 0) return -1;
    }
    for (i = 0; i < count; i++) {
        int n;
        if (iov[i].len == 0) continue;
        if (fd_arg == 1 || fd_arg == 2) {
            tty_write_buf((const char *)iov[i].base, iov[i].len);
            n = (int)iov[i].len;
        } else {
            n = vfs_write(vfs_fd, iov[i].base, iov[i].len);
        }
        if (n < 0) return total ? (int)total : -1;
        if (add_io_count(&total, n) < 0) return -1;
        if ((uint32_t)n < iov[i].len) break;
    }
    return (int)total;
}

static int timespec_to_sleep_ms(const sys_timespec_t *ts, uint32_t *out_ms) {
    uint32_t ms;
    uint32_t nsec_ms;
    if (!ts || !out_ms || ts->nsec >= 1000000000U) return -1;
    if (ts->sec > 0xFFFFFFFFU / 1000U) return -1;
    ms = ts->sec * 1000U;
    nsec_ms = (ts->nsec + 999999U) / 1000000U;
    if (ms > 0xFFFFFFFFU - nsec_ms) return -1;
    *out_ms = ms + nsec_ms;
    return 0;
}

static uint32_t sys_path_len(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static int join_at_path(const char *base, const char *rel, char out[VFS_MAX_PATH]) {
    uint32_t base_len = sys_path_len(base);
    uint32_t rel_len = sys_path_len(rel);
    uint32_t pos = 0;
    uint32_t i;
    if (!base || !base[0] || !rel || !rel[0]) return -1;
    if (base_len + rel_len + 1U >= VFS_MAX_PATH) return -1;
    if (base[0] == '/' && base[1] == 0) {
        out[pos++] = '/';
    } else {
        for (i = 0; i < base_len; i++) out[pos++] = base[i];
        out[pos++] = '/';
    }
    for (i = 0; i <= rel_len; i++) out[pos++] = rel[i];
    return 0;
}

static int resolve_at_user_path(uint32_t dirfd_arg, const char *user_ptr, char out[VFS_MAX_PATH]) {
    char user_path[VFS_MAX_PATH];
    char base[VFS_MAX_PATH];
    char joined[VFS_MAX_PATH];
    vfs_dirent_t ent;
    int vfs_fd;
    if (copy_user_path(user_path, user_ptr) < 0) return -1;
    if (user_path[0] == '/' || dirfd_arg == SYS_AT_FDCWD)
        return process_resolve_path(user_path, out, VFS_MAX_PATH);
    vfs_fd = process_fd_resolve((int)dirfd_arg);
    if (vfs_fd < 0 || vfs_fd_path(vfs_fd, base, sizeof(base)) < 0) return -1;
    if (vfs_stat(base, &ent) < 0 || ent.type != VFS_NODE_DIR) return -1;
    if (join_at_path(base, user_path, joined) < 0) return -1;
    return process_resolve_path(joined, out, VFS_MAX_PATH);
}

static int poll_process_fd_events(uint32_t fd, uint32_t events) {
    int vfs_fd;
    uint32_t ready = 0;
    if (events == 0 || (events & ~(VFS_POLL_READ | VFS_POLL_WRITE))) return -1;
    if (fd == 0) {
        if ((events & VFS_POLL_READ) && tty_read_ready()) ready |= VFS_POLL_READ;
        return (int)ready;
    }
    if (fd == 1 || fd == 2) {
        if (events & VFS_POLL_WRITE) ready |= VFS_POLL_WRITE;
        return (int)ready;
    }
    vfs_fd = process_fd_resolve((int)fd);
    if (vfs_fd < 0) return -1;
    return vfs_poll(vfs_fd, events);
}

static int select_scan(uint32_t read_in,
                       uint32_t write_in,
                       uint32_t nfds,
                       uint32_t *read_out,
                       uint32_t *write_out) {
    uint32_t fd;
    uint32_t ready_count = 0;
    *read_out = 0;
    *write_out = 0;
    for (fd = 0; fd < nfds; fd++) {
        uint32_t bit = 1U << fd;
        uint32_t events = 0;
        int ready;
        if (read_in & bit) events |= VFS_POLL_READ;
        if (write_in & bit) events |= VFS_POLL_WRITE;
        if (events == 0) continue;
        ready = poll_process_fd_events(fd, events);
        if (ready < 0) return -1;
        if ((ready & VFS_POLL_READ) && (read_in & bit)) {
            *read_out |= bit;
            ready_count++;
        }
        if ((ready & VFS_POLL_WRITE) && (write_in & bit)) {
            *write_out |= bit;
            ready_count++;
        }
    }
    return (int)ready_count;
}

static void prepare_exec_return(syscall_frame_t *frame, uint32_t entry, uint32_t user_stack) {
    frame->gs = GDT_USER_DATA;
    frame->fs = GDT_USER_DATA;
    frame->es = GDT_USER_DATA;
    frame->ds = GDT_USER_DATA;
    frame->eax = 0;
    frame->eip = entry;
    frame->cs = GDT_USER_CODE;
    frame->eflags |= 0x200U;
    frame->user_esp = user_stack;
    frame->ss = GDT_USER_DATA;
}

static void copy_fork_context(process_user_context_t *ctx, const syscall_frame_t *frame) {
    ctx->edi = frame->edi;
    ctx->esi = frame->esi;
    ctx->ebp = frame->ebp;
    ctx->ebx = frame->ebx;
    ctx->edx = frame->edx;
    ctx->ecx = frame->ecx;
    ctx->eax = 0;
    ctx->eip = frame->eip;
    ctx->eflags = frame->eflags;
    ctx->user_esp = frame->user_esp;
}

static uint32_t syscall_dispatch(uint32_t num,
                                 uint32_t a,
                                 uint32_t b,
                                 uint32_t c,
                                 uint32_t d,
                                 syscall_frame_t *frame) {
#define SYSCALL_RET(v) do { \
        uint32_t _ret = (uint32_t)(v); \
        scheduler_maybe_preempt(); \
        return _ret; \
    } while (0)
    switch (num) {
        case SYS_YIELD:
            task_yield();
            SYSCALL_RET(0);
        case SYS_SLEEP:
        {
            process_t *proc = process_current();
            if (proc && proc->pid != 0) proc->state = PROC_SLEEPING;
            task_sleep(a);
            if (proc && proc->state == PROC_SLEEPING) proc->state = PROC_RUNNING;
            SYSCALL_RET(0);
        }
        case SYS_EXIT:
            process_exit((int)a);
            task_exit();
            SYSCALL_RET(0);
        case SYS_GETPID:
        {
            process_t *proc = process_current();
            SYSCALL_RET(proc ? proc->pid : 0);
        }
        case SYS_GETPPID:
        {
            process_t *proc = process_current();
            SYSCALL_RET(proc ? proc->ppid : 0);
        }
        case SYS_WRITE:
            if (a == 1 || a == 2) {
                if (c == 0) SYSCALL_RET(0);
                if (!user_range_ok((const void *)b, c, 0)) SYSCALL_RET(-1);
                tty_write_buf((const char *)b, c);
                SYSCALL_RET(c);
            }
        {
            int vfs_fd;
            vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            if (c == 0) SYSCALL_RET(0);
            if (!user_range_ok((const void *)b, c, 0)) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_write(vfs_fd, (const void *)b, c));
        }
        case SYS_OPEN:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            int vfs_fd;
            int proc_fd;
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            vfs_fd = vfs_open(path, (int)b);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            proc_fd = install_open_fd(vfs_fd, b);
            if (proc_fd < 0) {
                SYSCALL_RET(-1);
            }
            SYSCALL_RET(proc_fd);
        }
        case SYS_OPENAT:
        {
            char path[VFS_MAX_PATH];
            int vfs_fd;
            int proc_fd;
            if (resolve_at_user_path(a, (const char *)b, path) < 0) SYSCALL_RET(-1);
            vfs_fd = vfs_open(path, (int)c);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            proc_fd = install_open_fd(vfs_fd, c);
            if (proc_fd < 0) {
                SYSCALL_RET(-1);
            }
            SYSCALL_RET(proc_fd);
        }
        case SYS_READ:
            if (a == 0) {
                if (c == 0) SYSCALL_RET(0);
                if (!user_range_ok((void *)b, c, 1)) SYSCALL_RET(-1);
                SYSCALL_RET(tty_read((char *)b, c));
            }
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            if (c == 0) SYSCALL_RET(0);
            if (!user_range_ok((void *)b, c, 1)) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_read(vfs_fd, (void *)b, c));
        }
        case SYS_PREAD:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            if (c == 0) SYSCALL_RET(0);
            if (!user_range_ok((void *)b, c, 1)) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_pread(vfs_fd, (void *)b, c, d));
        }
        case SYS_PWRITE:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            if (c == 0) SYSCALL_RET(0);
            if (!user_range_ok((const void *)b, c, 0)) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_pwrite(vfs_fd, (const void *)b, c, d));
        }
        case SYS_READV:
            SYSCALL_RET(syscall_readv_impl(a, (sys_iovec_t *)b, c));
        case SYS_WRITEV:
            SYSCALL_RET(syscall_writev_impl(a, (const sys_iovec_t *)b, c));
        case SYS_CLOSE:
            SYSCALL_RET(process_fd_close((int)a));
        case SYS_DUP:
            SYSCALL_RET(process_fd_dup((int)a));
        case SYS_DUP2:
            SYSCALL_RET(process_fd_dup2((int)a, (int)b));
        case SYS_FCNTL:
        {
            int vfs_fd;
            if (b == SYS_FCNTL_DUPFD) SYSCALL_RET(process_fd_dup_min((int)a, (int)c));
            if (b == SYS_FCNTL_GETFD) SYSCALL_RET(process_fd_get_flags((int)a));
            if (b == SYS_FCNTL_SETFD) SYSCALL_RET(process_fd_set_flags((int)a, c));
            vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            if (b == SYS_FCNTL_GETFL) SYSCALL_RET(vfs_fd_flags(vfs_fd));
            if (b == SYS_FCNTL_SETFL) SYSCALL_RET(vfs_set_fd_flags(vfs_fd, (int)c));
            SYSCALL_RET(-1);
        }
        case SYS_SEEK:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_seek(vfs_fd, (int32_t)b, (int)c));
        }
        case SYS_STAT:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0 ||
                !user_range_ok((void *)b, sizeof(vfs_dirent_t), 1)) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_stat(path, (vfs_dirent_t *)b));
        }
        case SYS_LSTAT:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0 ||
                !user_range_ok((void *)b, sizeof(vfs_dirent_t), 1)) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_lstat(path, (vfs_dirent_t *)b));
        }
        case SYS_FSTAT:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0 || !user_range_ok((void *)b, sizeof(vfs_dirent_t), 1)) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_fstat(vfs_fd, (vfs_dirent_t *)b));
        }
        case SYS_FSTATAT:
        {
            char path[VFS_MAX_PATH];
            if (!user_range_ok((void *)c, sizeof(vfs_dirent_t), 1)) SYSCALL_RET(-1);
            if (resolve_at_user_path(a, (const char *)b, path) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_stat(path, (vfs_dirent_t *)c));
        }
        case SYS_READDIR:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0 ||
                !user_range_ok((void *)c, sizeof(vfs_dirent_t), 1)) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_readdir(path, b, (vfs_dirent_t *)c));
        }
        case SYS_FREADDIR:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0 || !user_range_ok((void *)c, sizeof(vfs_dirent_t), 1)) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_readdir_fd(vfs_fd, b, (vfs_dirent_t *)c));
        }
        case SYS_SPAWN:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            process_t *proc;
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            proc = process_spawn_path(path);
            SYSCALL_RET(proc ? (int)proc->pid : -1);
        }
        case SYS_SPAWN_ARGS:
            (void)d;
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            char argbuf[PROCESS_MAX_ARGS][PROCESS_ARG_MAX];
            const char *argv[PROCESS_MAX_ARGS];
            process_t *proc;
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (copy_user_argv(b, (const char *const *)c, argbuf, argv) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            proc = b ? process_spawn_path_args(path, b, argv) : process_spawn_path(path);
            SYSCALL_RET(proc ? (int)proc->pid : -1);
        }
        case SYS_EXEC:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            const char *argv[1];
            uint32_t entry;
            uint32_t stack;
            if (!frame) SYSCALL_RET(-1);
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            argv[0] = path;
            if (process_exec_path_args(path, 1, argv, &entry, &stack) < 0) SYSCALL_RET(-1);
            prepare_exec_return(frame, entry, stack);
            SYSCALL_RET(0);
        }
        case SYS_EXEC_ARGS:
            (void)d;
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            char argbuf[PROCESS_MAX_ARGS][PROCESS_ARG_MAX];
            const char *argv[PROCESS_MAX_ARGS];
            uint32_t entry;
            uint32_t stack;
            if (!frame) SYSCALL_RET(-1);
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (copy_user_argv(b, (const char *const *)c, argbuf, argv) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            if (process_exec_path_args(path, b, b ? argv : 0, &entry, &stack) < 0) SYSCALL_RET(-1);
            prepare_exec_return(frame, entry, stack);
            SYSCALL_RET(0);
        }
        case SYS_FORK:
        {
            process_user_context_t ctx;
            (void)a;
            (void)b;
            (void)c;
            (void)d;
            if (!frame) SYSCALL_RET(-1);
            if ((frame->cs & 3U) != 3U || (frame->ss & 3U) != 3U) SYSCALL_RET(-1);
            copy_fork_context(&ctx, frame);
            SYSCALL_RET(process_fork_user(&ctx));
        }
        case SYS_WAIT:
            if (b && !user_range_ok((void *)b, sizeof(int), 1)) SYSCALL_RET(-1);
            SYSCALL_RET(process_wait(a, (int *)b));
        case SYS_WAIT_ANY:
            if (a && !user_range_ok((void *)a, sizeof(int), 1)) SYSCALL_RET(-1);
            SYSCALL_RET(process_wait_any((int *)a));
        case SYS_MKDIR:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_mkdir(path));
        }
        case SYS_MKDIRAT:
        {
            char path[VFS_MAX_PATH];
            if (resolve_at_user_path(a, (const char *)b, path) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_mkdir(path));
        }
        case SYS_RMDIR:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_rmdir(path));
        }
        case SYS_SYMLINK:
        {
            char target[VFS_MAX_PATH];
            char user_link_path[VFS_MAX_PATH];
            char link_path[VFS_MAX_PATH];
            if (copy_user_string(target, (const char *)a, sizeof(target)) < 0 ||
                copy_user_path(user_link_path, (const char *)b) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_link_path, link_path, sizeof(link_path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_symlink(target, link_path));
        }
        case SYS_READLINK:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            char target[VFS_MAX_PATH];
            int len;
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            len = vfs_readlink(path, target, sizeof(target));
            if (len < 0 || c == 0 || c <= (uint32_t)len) SYSCALL_RET(-1);
            SYSCALL_RET(copy_to_user((void *)b, target, (uint32_t)len + 1U) < 0 ? -1 : len);
        }
        case SYS_UNLINK:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_unlink(path));
        }
        case SYS_UNLINKAT:
        {
            char path[VFS_MAX_PATH];
            if (resolve_at_user_path(a, (const char *)b, path) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_unlink(path));
        }
        case SYS_KILL:
        {
            process_t *proc = process_current();
            if (proc && proc->pid == a) {
                process_exit(-9);
                task_exit();
                SYSCALL_RET(0);
            }
            SYSCALL_RET(process_kill(a, -9));
        }
        case SYS_KILL_SIGNAL:
        {
            process_t *proc = process_current();
            int code;
            if (b == 0 || b > 31U) SYSCALL_RET(-1);
            code = -(int)b;
            if (proc && proc->pid == a) {
                process_exit(code);
                task_exit();
                SYSCALL_RET(0);
            }
            SYSCALL_RET(process_kill(a, code));
        }
        case SYS_KILL_GROUP:
        {
            process_t *proc = process_current();
            uint32_t pgid;
            int self_hit;
            int code;
            if (b == 0 || b > 31U) SYSCALL_RET(-1);
            if (!proc) SYSCALL_RET(-1);
            pgid = a ? a : proc->pgid;
            self_hit = proc->pid != 0 && proc->pgid == pgid;
            code = -(int)b;
            if (process_kill_group(a, code) < 0) SYSCALL_RET(-1);
            if (self_hit) task_exit();
            SYSCALL_RET(0);
        }
        case SYS_CHDIR:
        {
            char user_path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(process_chdir(user_path));
        }
        case SYS_FCHDIR:
        {
            int vfs_fd = process_fd_resolve((int)a);
            char path[VFS_MAX_PATH];
            if (vfs_fd < 0 || vfs_fd_path(vfs_fd, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(process_chdir(path));
        }
        case SYS_GETCWD:
        {
            const char *cwd = process_cwd();
            uint32_t len = 0;
            uint32_t i;
            while (cwd[len]) len++;
            if (b == 0 || b <= len || !user_range_ok((void *)a, b, 1)) SYSCALL_RET(-1);
            for (i = 0; i <= len; i++) ((char *)a)[i] = cwd[i];
            SYSCALL_RET((int)len);
        }
        case SYS_RENAME:
        {
            char user_old_path[VFS_MAX_PATH];
            char user_new_path[VFS_MAX_PATH];
            char old_path[VFS_MAX_PATH];
            char new_path[VFS_MAX_PATH];
            if (copy_user_path(user_old_path, (const char *)a) < 0 ||
                copy_user_path(user_new_path, (const char *)b) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_old_path, old_path, sizeof(old_path)) < 0 ||
                process_resolve_path(user_new_path, new_path, sizeof(new_path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_rename(old_path, new_path));
        }
        case SYS_RENAMEAT:
        {
            char old_path[VFS_MAX_PATH];
            char new_path[VFS_MAX_PATH];
            if (resolve_at_user_path(a, (const char *)b, old_path) < 0 ||
                resolve_at_user_path(c, (const char *)d, new_path) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_rename(old_path, new_path));
        }
        case SYS_PIPE:
        {
            int fds[2];
            if (!user_range_ok((void *)a, sizeof(fds), 1)) SYSCALL_RET(-1);
            if (process_pipe(fds) < 0) SYSCALL_RET(-1);
            ((int *)a)[0] = fds[0];
            ((int *)a)[1] = fds[1];
            SYSCALL_RET(0);
        }
        case SYS_PIPE2:
        {
            int fds[2];
            uint32_t fd_flags = b & PROCESS_FD_CLOEXEC;
            uint32_t status_flags = b & VFS_O_NONBLOCK;
            if (b & ~(PROCESS_FD_CLOEXEC | VFS_O_NONBLOCK)) SYSCALL_RET(-1);
            if (!user_range_ok((void *)a, sizeof(fds), 1)) SYSCALL_RET(-1);
            if (process_pipe_flags(fds, fd_flags) < 0) SYSCALL_RET(-1);
            if (status_flags) {
                int read_vfs_fd = process_fd_resolve(fds[0]);
                int write_vfs_fd = process_fd_resolve(fds[1]);
                if (read_vfs_fd < 0 ||
                    write_vfs_fd < 0 ||
                    vfs_set_fd_flags(read_vfs_fd, VFS_O_RDONLY | status_flags) < 0 ||
                    vfs_set_fd_flags(write_vfs_fd, VFS_O_WRONLY | status_flags) < 0) {
                    (void)process_fd_close(fds[0]);
                    (void)process_fd_close(fds[1]);
                    SYSCALL_RET(-1);
                }
            }
            ((int *)a)[0] = fds[0];
            ((int *)a)[1] = fds[1];
            SYSCALL_RET(0);
        }
        case SYS_SBRK:
            SYSCALL_RET(process_sbrk(process_current(), (int32_t)a));
        case SYS_MMAP:
            SYSCALL_RET(process_mmap(process_current(), a, b));
        case SYS_MUNMAP:
            SYSCALL_RET(process_munmap(process_current(), a, b));
        case SYS_MPROTECT:
            SYSCALL_RET(process_mprotect(process_current(), a, b, c));
        case SYS_TRUNCATE:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_truncate(path, b));
        }
        case SYS_FTRUNCATE:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_ftruncate(vfs_fd, b));
        }
        case SYS_FCHMOD:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_fchmod(vfs_fd, b));
        }
        case SYS_FCHOWN:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_fchown(vfs_fd, b, c));
        }
        case SYS_FUTIME:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_futime(vfs_fd, b, c));
        }
        case SYS_FSYNC:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_fsync(vfs_fd));
        }
        case SYS_FDATASYNC:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_fdatasync(vfs_fd));
        }
        case SYS_SYNC:
            SYSCALL_RET(vfs_sync());
        case SYS_CHMOD:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_chmod(path, b));
        }
        case SYS_CHOWN:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_chown(path, b, c));
        }
        case SYS_UTIME:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_utime(path, b, c));
        }
        case SYS_ACCESS:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_access(path, b));
        }
        case SYS_ACCESSAT:
        {
            char path[VFS_MAX_PATH];
            if (resolve_at_user_path(a, (const char *)b, path) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_access(path, c));
        }
        case SYS_POLL:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_poll(vfs_fd, b));
        }
        case SYS_POLL_MANY:
        {
            sys_pollfd_t *fds = (sys_pollfd_t *)a;
            uint32_t i;
            int ready_count = 0;
            if (b == 0) SYSCALL_RET(0);
            if (b > SYS_POLL_MAX ||
                !user_range_ok(fds, b * sizeof(sys_pollfd_t), 1)) SYSCALL_RET(-1);
            for (i = 0; i < b; i++) {
                int vfs_fd = process_fd_resolve(fds[i].fd);
                int ready;
                if (vfs_fd < 0) SYSCALL_RET(-1);
                ready = vfs_poll(vfs_fd, fds[i].events);
                if (ready < 0) SYSCALL_RET(-1);
                fds[i].revents = (uint32_t)ready;
                if (ready) ready_count++;
            }
            SYSCALL_RET(ready_count);
        }
        case SYS_SELECT:
        {
            uint32_t read_in = 0;
            uint32_t write_in = 0;
            uint32_t read_out = 0;
            uint32_t write_out = 0;
            uint32_t mask;
            int ready;
            if (c > PROCESS_MAX_FDS || c > 31U) SYSCALL_RET(-1);
            if (a && !user_range_ok((void *)a, sizeof(uint32_t), 1)) SYSCALL_RET(-1);
            if (b && !user_range_ok((void *)b, sizeof(uint32_t), 1)) SYSCALL_RET(-1);
            if (a) read_in = *(const uint32_t *)a;
            if (b) write_in = *(const uint32_t *)b;
            mask = c == 0 ? 0 : ((1U << c) - 1U);
            read_in &= mask;
            write_in &= mask;
            ready = select_scan(read_in, write_in, c, &read_out, &write_out);
            if (ready == 0 && d > 0) {
                process_t *proc = process_current();
                if (proc && proc->pid != 0) proc->state = PROC_SLEEPING;
                task_sleep(d);
                if (proc && proc->state == PROC_SLEEPING) proc->state = PROC_RUNNING;
                ready = select_scan(read_in, write_in, c, &read_out, &write_out);
            }
            if (ready < 0) SYSCALL_RET(-1);
            if (a && copy_to_user((void *)a, &read_out, sizeof(read_out)) < 0) SYSCALL_RET(-1);
            if (b && copy_to_user((void *)b, &write_out, sizeof(write_out)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(ready);
        }
        case SYS_IOCTL:
        {
            int vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_ioctl(vfs_fd, b, c));
        }
        case SYS_TTY_GET_MODE:
            SYSCALL_RET(tty_get_mode());
        case SYS_TTY_SET_MODE:
            SYSCALL_RET(tty_set_mode(a));
        case SYS_STATFS:
        {
            char user_path[VFS_MAX_PATH];
            char path[VFS_MAX_PATH];
            vfs_fsinfo_t fs;
            sys_fsinfo_t info;
            if (copy_user_path(user_path, (const char *)a) < 0) SYSCALL_RET(-1);
            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
            if (vfs_statfs(path, &fs) < 0) SYSCALL_RET(-1);
            copy_fsinfo(&info, &fs);
            SYSCALL_RET(copy_to_user((void *)b, &info, sizeof(info)));
        }
        case SYS_CLOCK_MS:
            SYSCALL_RET(timer_ms());
        case SYS_CLOCK_GETTIME:
        {
            uint32_t ms;
            sys_timespec_t ts;
            if (a != SYS_CLOCK_UPTIME_MONOTONIC) SYSCALL_RET(-1);
            ms = timer_ms();
            ts.sec = ms / 1000U;
            ts.nsec = (ms % 1000U) * 1000000U;
            SYSCALL_RET(copy_to_user((void *)b, &ts, sizeof(ts)));
        }
        case SYS_TIME:
        {
            uint32_t sec = timer_ms() / 1000U;
            if (a) {
                if (!user_range_ok((void *)a, sizeof(uint32_t), 1)) SYSCALL_RET(-1);
                *(uint32_t *)a = sec;
            }
            SYSCALL_RET(sec);
        }
        case SYS_GETTIMEOFDAY:
        {
            uint32_t ms;
            sys_timeval_t tv;
            if (!user_range_ok((void *)a, sizeof(tv), 1)) SYSCALL_RET(-1);
            ms = timer_ms();
            tv.sec = ms / 1000U;
            tv.usec = (ms % 1000U) * 1000U;
            SYSCALL_RET(copy_to_user((void *)a, &tv, sizeof(tv)));
        }
        case SYS_NANOSLEEP:
        {
            const sys_timespec_t *req = (const sys_timespec_t *)a;
            sys_timespec_t req_copy;
            uint32_t sleep_ms;
            process_t *proc;
            if (!user_range_ok(req, sizeof(req_copy), 0)) SYSCALL_RET(-1);
            req_copy = *req;
            if (timespec_to_sleep_ms(&req_copy, &sleep_ms) < 0) SYSCALL_RET(-1);
            if (b) {
                sys_timespec_t zero;
                zero.sec = 0;
                zero.nsec = 0;
                if (!user_range_ok((void *)b, sizeof(zero), 1)) SYSCALL_RET(-1);
                *(sys_timespec_t *)b = zero;
            }
            proc = process_current();
            if (proc && proc->pid != 0) proc->state = PROC_SLEEPING;
            task_sleep(sleep_ms);
            if (proc && proc->state == PROC_SLEEPING) proc->state = PROC_RUNNING;
            SYSCALL_RET(0);
        }
        case SYS_GETRANDOM:
            if (c != 0) SYSCALL_RET(-1);
            if (b == 0) SYSCALL_RET(0);
            if (!user_range_ok((void *)a, b, 1)) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_getrandom((void *)a, b));
        case SYS_UPTIME:
            SYSCALL_RET(timer_ticks());
        case SYS_NETIF_COUNT:
            SYSCALL_RET(netif_count());
        case SYS_NETIF_INFO:
        {
            const netif_t *net = netif_at(a);
            sys_netif_info_t info;
            if (!net) SYSCALL_RET(-1);
            copy_netif_info(&info, net);
            SYSCALL_RET(copy_to_user((void *)b, &info, sizeof(info)));
        }
        case SYS_NET_PING4:
        {
            uint32_t ifindex = 0;
            if (net_route_lookup4(a, &ifindex, 0) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(net_ping4(ifindex, a));
        }
        case SYS_UDP_SEND4:
        {
            uint32_t ifindex = 0;
            uint16_t dst_port;
            uint16_t src_port;
            if (b == 0 || b > 65535U || d > NET_UDP_PAYLOAD_MAX) SYSCALL_RET(-1);
            if (d > 0 && !user_range_ok((const void *)c, d, 0)) SYSCALL_RET(-1);
            if (net_route_lookup4(a, &ifindex, 0) < 0) SYSCALL_RET(-1);
            dst_port = (uint16_t)b;
            src_port = (uint16_t)(dst_port == 65535U ? 65534U : dst_port + 1U);
            SYSCALL_RET(net_udp_send4(ifindex, a, src_port, dst_port, (const void *)c, d));
        }
	        case SYS_UDP_RECV4:
	        {
	            uint32_t src_ipv4 = 0;
	            uint16_t src_port = 0;
	            sys_udp_peer_t peer;
            int n;
            if (a == 0 || a > 65535U || c > NET_UDP_PAYLOAD_MAX) SYSCALL_RET(-1);
            if (c > 0 && !user_range_ok((void *)b, c, 1)) SYSCALL_RET(-1);
            if (d && !user_range_ok((void *)d, sizeof(peer), 1)) SYSCALL_RET(-1);
            n = net_udp_recv4((uint16_t)a, (void *)b, c, &src_ipv4, &src_port);
            if (n < 0) SYSCALL_RET(-1);
            if (d) {
                peer.src_ipv4 = src_ipv4;
                peer.src_port = src_port;
                if (copy_to_user((void *)d, &peer, sizeof(peer)) < 0) SYSCALL_RET(-1);
	            }
	            SYSCALL_RET(n);
	        }
        case SYS_SOCKET_UDP4:
        {
            int vfs_fd;
            int proc_fd;
            vfs_fd = vfs_socket_udp4();
            if (vfs_fd < 0) SYSCALL_RET(-1);
            proc_fd = process_fd_install(vfs_fd);
            if (proc_fd < 0) {
                vfs_close(vfs_fd);
                SYSCALL_RET(-1);
            }
            SYSCALL_RET(proc_fd);
        }
        case SYS_BIND_UDP4:
        {
            int vfs_fd;
            if (b > 65535U) SYSCALL_RET(-1);
            vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_socket_bind_udp4(vfs_fd, (uint16_t)b));
        }
        case SYS_SENDTO_UDP4:
        {
            sys_sockaddr_in4_t addr;
            int vfs_fd;
            if (!user_range_ok((const void *)b, sizeof(addr), 0)) SYSCALL_RET(-1);
            if (d > NET_UDP_PAYLOAD_MAX) SYSCALL_RET(-1);
            if (d > 0 && !user_range_ok((const void *)c, d, 0)) SYSCALL_RET(-1);
            addr = *(const sys_sockaddr_in4_t *)b;
            vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_socket_sendto_udp4(vfs_fd,
                                               addr.ipv4,
                                               addr.port,
                                               (const void *)c,
                                               d));
        }
        case SYS_RECVFROM_UDP4:
        {
            sys_sockaddr_in4_t addr;
            uint32_t src_ipv4 = 0;
            uint16_t src_port = 0;
            int vfs_fd;
            int n;
            if (d > NET_UDP_PAYLOAD_MAX) SYSCALL_RET(-1);
            if (d > 0 && !user_range_ok((void *)c, d, 1)) SYSCALL_RET(-1);
            if (b && !user_range_ok((void *)b, sizeof(addr), 1)) SYSCALL_RET(-1);
            vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            n = vfs_socket_recvfrom_udp4(vfs_fd, &src_ipv4, &src_port, (void *)c, d);
            if (n < 0) SYSCALL_RET(-1);
            if (b) {
                addr.ipv4 = src_ipv4;
                addr.port = src_port;
                if (copy_to_user((void *)b, &addr, sizeof(addr)) < 0) SYSCALL_RET(-1);
            }
            SYSCALL_RET(n);
        }
        case SYS_CONNECT_UDP4:
        {
            sys_sockaddr_in4_t addr;
            int vfs_fd;
            if (!user_range_ok((const void *)b, sizeof(addr), 0)) SYSCALL_RET(-1);
            addr = *(const sys_sockaddr_in4_t *)b;
            vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            SYSCALL_RET(vfs_socket_connect_udp4(vfs_fd, addr.ipv4, addr.port));
        }
        case SYS_GETSOCKNAME_UDP4:
        {
            sys_sockaddr_in4_t addr;
            uint32_t local_ipv4 = 0;
            uint16_t local_port = 0;
            int vfs_fd;
            if (!user_range_ok((void *)b, sizeof(addr), 1)) SYSCALL_RET(-1);
            vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            if (vfs_socket_getname_udp4(vfs_fd, &local_ipv4, &local_port) < 0) SYSCALL_RET(-1);
            addr.ipv4 = local_ipv4;
            addr.port = local_port;
            SYSCALL_RET(copy_to_user((void *)b, &addr, sizeof(addr)));
        }
        case SYS_GETPEERNAME_UDP4:
        {
            sys_sockaddr_in4_t addr;
            uint32_t dst_ipv4 = 0;
            uint16_t dst_port = 0;
            int vfs_fd;
            if (!user_range_ok((void *)b, sizeof(addr), 1)) SYSCALL_RET(-1);
            vfs_fd = process_fd_resolve((int)a);
            if (vfs_fd < 0) SYSCALL_RET(-1);
            if (vfs_socket_getpeer_udp4(vfs_fd, &dst_ipv4, &dst_port) < 0) SYSCALL_RET(-1);
            addr.ipv4 = dst_ipv4;
            addr.port = dst_port;
            SYSCALL_RET(copy_to_user((void *)b, &addr, sizeof(addr)));
        }
	        case SYS_SYSINFO:
	        {
	            sys_sysinfo_t info;
	            copy_sysinfo(&info);
	            SYSCALL_RET(copy_to_user((void *)a, &info, sizeof(info)));
	        }
        case SYS_UNAME:
        {
            sys_utsname_t uts;
            copy_utsname(&uts);
            SYSCALL_RET(copy_to_user((void *)a, &uts, sizeof(uts)));
        }
        case SYS_SYSCONF:
        {
            uint32_t value;
            if (sysconf_value(a, &value) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(value);
        }
        case SYS_GETENV:
        {
            char name[PROCESS_ENV_MAX];
            char value[PROCESS_ENV_MAX];
            int len;
            if (copy_user_string(name, (const char *)a, sizeof(name)) < 0) SYSCALL_RET(-1);
            len = process_env_copy_value(process_current(), name, value, sizeof(value));
            if (len < 0 || c == 0 || c <= (uint32_t)len) SYSCALL_RET(-1);
            SYSCALL_RET(copy_to_user((void *)b, value, (uint32_t)len + 1U) < 0 ? -1 : len);
        }
        case SYS_SETENV:
        {
            char name[PROCESS_ENV_MAX];
            char value[PROCESS_ENV_MAX];
            if (copy_user_string(name, (const char *)a, sizeof(name)) < 0 ||
                copy_user_string(value, (const char *)b, sizeof(value)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(process_env_set(process_current(), name, value, c ? 1 : 0));
        }
        case SYS_UNSETENV:
        {
            char name[PROCESS_ENV_MAX];
            if (copy_user_string(name, (const char *)a, sizeof(name)) < 0) SYSCALL_RET(-1);
            SYSCALL_RET(process_env_unset(process_current(), name));
        }
        case SYS_ENV_COUNT:
            SYSCALL_RET(process_env_count(process_current()));
        case SYS_ENV_INFO:
        {
            const char *entry = process_env_entry(process_current(), a);
            char copy[PROCESS_ENV_MAX];
            uint32_t len = 0;
            uint32_t i;
            if (!entry) SYSCALL_RET(-1);
            while (entry[len]) len++;
            if (c == 0 || c <= len) SYSCALL_RET(-1);
            for (i = 0; i <= len; i++) copy[i] = entry[i];
            SYSCALL_RET(copy_to_user((void *)b, copy, len + 1U) < 0 ? -1 : (int)len);
        }
        case SYS_GETHOSTNAME:
        {
            char name[UTS_FIELD_MAX];
            int len = uts_copy_nodename(name, sizeof(name));
            if (len < 0 || b == 0 || b <= (uint32_t)len) SYSCALL_RET(-1);
            SYSCALL_RET(copy_to_user((void *)a, name, (uint32_t)len + 1U) < 0 ? -1 : len);
        }
        case SYS_SETHOSTNAME:
        {
            char name[UTS_FIELD_MAX];
            if (copy_user_string(name, (const char *)a, sizeof(name)) < 0) SYSCALL_RET(-1);
            if (uts_set_nodename(name) < 0) SYSCALL_RET(-1);
            (void)process_env_set(process_current(), "HOSTNAME", uts_nodename(), 1);
            SYSCALL_RET(0);
        }
        case SYS_UMASK:
            SYSCALL_RET(process_umask_set(process_current(), a));
        case SYS_GETUID:
            SYSCALL_RET(process_uid_get(process_current()));
        case SYS_GETGID:
            SYSCALL_RET(process_gid_get(process_current()));
        case SYS_SETUID:
            SYSCALL_RET(process_uid_set(process_current(), a));
        case SYS_SETGID:
            SYSCALL_RET(process_gid_set(process_current(), a));
        case SYS_GETPGID:
            SYSCALL_RET(process_getpgid(a));
        case SYS_SETPGID:
            SYSCALL_RET(process_setpgid(a, b));
        case SYS_GETSID:
            SYSCALL_RET(process_getsid(a));
        case SYS_SETSID:
            SYSCALL_RET(process_setsid());
	        case SYS_PROCESS_COUNT:
	            SYSCALL_RET(process_count());
	        case SYS_PROCESS_INFO:
	        {
	            const process_t *proc = process_by_ordinal(a);
	            sys_process_info_t info;
	            if (!proc) SYSCALL_RET(-1);
	            copy_process_info(&info, proc);
	            SYSCALL_RET(copy_to_user((void *)b, &info, sizeof(info)));
	        }
	        case SYS_BLOCK_COUNT:
	            SYSCALL_RET(block_count());
	        case SYS_BLOCK_INFO:
	        {
	            const block_device_t *block = block_at(a);
	            sys_block_info_t info;
	            if (!block) SYSCALL_RET(-1);
	            copy_block_info(&info, block);
	            SYSCALL_RET(copy_to_user((void *)b, &info, sizeof(info)));
	        }
	        case SYS_DRIVER_COUNT:
	            SYSCALL_RET(driver_count());
	        case SYS_DRIVER_INFO:
	        {
	            const driver_t *driver = driver_at(a);
	            sys_driver_info_t info;
	            if (!driver) SYSCALL_RET(-1);
	            copy_driver_info(&info, driver);
	            SYSCALL_RET(copy_to_user((void *)b, &info, sizeof(info)));
	        }
	        case SYS_TASK_COUNT:
	            SYSCALL_RET(task_active_count());
	        case SYS_TASK_INFO:
	        {
	            const task_t *task = task_by_ordinal(a);
	            sys_task_info_t info;
	            if (!task) SYSCALL_RET(-1);
	            copy_task_info(&info, task);
	            SYSCALL_RET(copy_to_user((void *)b, &info, sizeof(info)));
	        }
        case SYS_TASK_SET_PRIORITY:
            SYSCALL_RET(task_set_priority((int)a, b));
        case SYS_TASK_PRIORITY:
        {
            const task_t *task = task_at(a);
            if (!task) SYSCALL_RET(-1);
            SYSCALL_RET(task->priority);
        }
	        case SYS_PCI_COUNT:
	            SYSCALL_RET(pci_count());
	        case SYS_PCI_INFO:
	        {
	            const pci_device_t *pci = pci_at(a);
	            sys_pci_info_t info;
	            if (!pci) SYSCALL_RET(-1);
	            copy_pci_info(&info, pci);
	            SYSCALL_RET(copy_to_user((void *)b, &info, sizeof(info)));
	        }
	        case SYS_BLOCK_READ:
	            if (!block_user_range_ok((void *)c, d, 1)) SYSCALL_RET(-1);
	            SYSCALL_RET(block_read(a, b, (void *)c, d));
	        case SYS_BLOCK_WRITE:
	            if (!block_user_range_ok((const void *)c, d, 0)) SYSCALL_RET(-1);
	            SYSCALL_RET(block_write(a, b, (const void *)c, d));
	        case SYS_SIMPLEFS_FORMAT:
	            SYSCALL_RET(simplefs_format(a));
	        case SYS_SIMPLEFS_MOUNT:
	        {
	            char user_path[VFS_MAX_PATH];
	            char path[VFS_MAX_PATH];
	            vfs_dirent_t ent;
	            if (copy_user_path(user_path, (const char *)b) < 0) SYSCALL_RET(-1);
	            if (process_resolve_path(user_path, path, sizeof(path)) < 0) SYSCALL_RET(-1);
	            if (vfs_stat(path, &ent) < 0 || ent.type != VFS_NODE_DIR) SYSCALL_RET(-1);
	            SYSCALL_RET(simplefs_mount(a, path));
	        }
            case SYS_SIMPLEFS_UNMOUNT:
                SYSCALL_RET(simplefs_unmount());
	        case SYS_NETIF_SET_UP:
	            SYSCALL_RET(netif_set_up(a, b ? 1 : 0));
	        case SYS_NETIF_SET_IPV4:
	            SYSCALL_RET(netif_set_ipv4(a, b));
	        case SYS_ROUTE_ADD4:
	            SYSCALL_RET(net_route_add(a, b, c, d));
	        case SYS_ROUTE_DEL4:
	            SYSCALL_RET(net_route_del(a, b, c, d));
	        case SYS_ARP_ADD4:
	            if (!user_range_ok((const void *)c, 6, 0)) SYSCALL_RET(-1);
	            SYSCALL_RET(net_arp_learn(a, b, (const uint8_t *)c));
	        case SYS_ARP_DEL4:
	            SYSCALL_RET(net_arp_delete(a, b));
	        case SYS_ROUTE_COUNT:
	            SYSCALL_RET(net_route_count());
	        case SYS_ROUTE_INFO:
	        {
	            const net_route_t *route = net_route_at(a);
	            sys_route_info_t info;
	            if (!route) SYSCALL_RET(-1);
	            copy_route_info(&info, route);
	            SYSCALL_RET(copy_to_user((void *)b, &info, sizeof(info)));
	        }
	        case SYS_ARP_COUNT:
	            SYSCALL_RET(net_arp_count());
	        case SYS_ARP_INFO:
	        {
	            const net_arp_entry_t *arp = net_arp_at(a);
	            sys_arp_info_t info;
	            if (!arp) SYSCALL_RET(-1);
	            copy_arp_info(&info, arp);
	            SYSCALL_RET(copy_to_user((void *)b, &info, sizeof(info)));
	        }
	        default:
	            SYSCALL_RET((uint32_t)-1);
	    }
#undef SYSCALL_RET
}

uint32_t syscall_handler_c(uint32_t num, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    return syscall_dispatch(num, a, b, c, d, 0);
}

uint32_t syscall_handler_c_frame(uint32_t num,
                                 uint32_t a,
                                 uint32_t b,
                                 uint32_t c,
                                 uint32_t d,
                                 syscall_frame_t *frame) {
    return syscall_dispatch(num, a, b, c, d, frame);
}

/* -----------------------------------------------------------------------
 * syscall_init — install the int 0x80 gate in the IDT
 *
 * We reach into the IDT directly because irq_install_handler() only
 * covers the 16-slot IRQ table.  The gate descriptor layout is identical
 * to the IRQ entries (interrupt gate, ring-0, selector 0x08).
 * The assembly stub syscall0x80_stub is defined in idt_stubs.S.
 * --------------------------------------------------------------------- */

/* IDT entry layout (matches idt.c) */
struct syscall_idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

/* The IDT lives at a known symbol; we get its address at run-time via SIDT */
extern void syscall0x80_stub(void);  /* defined in idt_stubs.S */

void syscall_init(void) {
    /* Read current IDTR to find the IDT base */
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) idtr;

    __asm__ volatile("sidt %0" : "=m"(idtr));

    struct syscall_idt_entry *idt =
        (struct syscall_idt_entry *)(uintptr_t)idtr.base;

    uint32_t addr = (uint32_t)syscall0x80_stub;
    idt[0x80].base_low  = addr & 0xFFFF;
    idt[0x80].base_high = (addr >> 16) & 0xFFFF;
    idt[0x80].sel       = 0x08;
    idt[0x80].zero      = 0;
    idt[0x80].flags     = 0xEE; /* interrupt gate, callable from ring 3 */
}
