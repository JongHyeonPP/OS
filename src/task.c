#include "task.h"
#include "gdt.h"
#include "paging.h"
#include "process.h"
#include "timer.h"
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Static task table
 * --------------------------------------------------------------------- */
static task_t tasks[MAX_TASKS];
static int    current_task = 0;
static int    num_tasks    = 0;
static int    preempt_enabled = 0;
static int    preempt_pending = 0;
static uint32_t quantum_ticks = 10;
static uint32_t current_ticks = 0;
static uint32_t irq_switches = 0;
static uint32_t coop_switches = 0;

/* -----------------------------------------------------------------------
 * Forward declaration so task_create can reference it
 * --------------------------------------------------------------------- */
void task_exit(void);
void task_restore_context(void);

static void task_bootstrap(void) {
    void (*entry)(void) = 0;
    if (current_task >= 0 && current_task < num_tasks) {
        if (tasks[current_task].process_id != 0) {
            process_t *owner = process_find(tasks[current_task].process_id);
            if (owner && owner->state == PROC_READY) owner->state = PROC_RUNNING;
        }
        entry = tasks[current_task].entry;
    }
    if (entry) entry();
    if (current_task >= 0 &&
        current_task < num_tasks &&
        tasks[current_task].process_id != 0) {
        process_exit(0);
    }
    task_exit();
}

static uint32_t kernel_cr3(void) {
    return (uint32_t)paging_kernel_directory();
}

static uint32_t task_kernel_stack_top(const task_t *task) {
    return (uint32_t)(task->stack + TASK_STACK_SIZE);
}

static void activate_task_context(const task_t *task) {
    uint32_t cr3 = task->regs.cr3 ? task->regs.cr3 : kernel_cr3();
    tss_set_kernel_stack(task_kernel_stack_top(task));
    if (paging_current_directory() != (uint32_t *)cr3)
        paging_switch_directory((uint32_t *)cr3);
}

static void mark_task_process_state(const task_t *task, process_state_t state) {
    process_t *owner;
    if (!task || task->process_id == 0) return;
    owner = process_find(task->process_id);
    if (!owner || owner->state == PROC_UNUSED || owner->state == PROC_ZOMBIE) return;
    if (state == PROC_READY && (owner->state == PROC_RUNNING || owner->state == PROC_SLEEPING))
        owner->state = PROC_READY;
    else if (state == PROC_RUNNING && owner->state == PROC_READY) owner->state = PROC_RUNNING;
    else if (state == PROC_SLEEPING && (owner->state == PROC_RUNNING || owner->state == PROC_READY))
        owner->state = PROC_SLEEPING;
}

static uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\npop %0\ncli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uint32_t flags) {
    __asm__ volatile("push %0\npopfl" : : "r"(flags) : "memory");
}

static void reset_task_slot(task_t *t, uint32_t id) {
    t->id = id;
    t->state = TASK_DEAD;
    t->regs.esp = 0;
    t->regs.cr3 = kernel_cr3();
    t->sleep_until = 0;
    t->priority = TASK_PRIORITY_DEFAULT;
    t->name = 0;
    t->started = 0;
    t->process_id = 0;
    t->entry = 0;
}

static void trim_task_table(void) {
    while (num_tasks > 1 && tasks[num_tasks - 1].state == TASK_DEAD) num_tasks--;
    if (current_task >= num_tasks) current_task = 0;
}

static int tick_reached(uint32_t now, uint32_t target) {
    return (int32_t)(now - target) >= 0;
}

static int task_runnable(int index, int allow_unstarted) {
    if (index < 0 || index >= num_tasks) return 0;
    if (tasks[index].state != TASK_READY && tasks[index].state != TASK_RUNNING) return 0;
    if (!allow_unstarted && !tasks[index].started) return 0;
    return 1;
}

static int pick_next_task(int allow_unstarted) {
    int candidate = -1;
    uint32_t best_priority = 0;
    int i;
    if (num_tasks <= 1) return -1;
    for (i = 0; i < MAX_TASKS; i++) {
        int index = (current_task + 1 + i) % num_tasks;
        if (index == current_task) break;
        if (!task_runnable(index, allow_unstarted)) continue;
        if (candidate < 0 || tasks[index].priority > best_priority) {
            candidate = index;
            best_priority = tasks[index].priority;
        }
    }
    return candidate;
}

/* -----------------------------------------------------------------------
 * Low-level context switch.
 *
 * All task stacks use the same restore frame as an IRQ return:
 * saved segment registers, pushal registers, then an iret frame.  That
 * keeps cooperative yields and timer IRQ preemption compatible.
 * --------------------------------------------------------------------- */
static uint32_t build_initial_context(task_t *t) {
    uint32_t *sp = (uint32_t *)(t->stack + TASK_STACK_SIZE);
    *--sp = 0x00000202U;              /* EFLAGS: IF set */
    *--sp = 0x00000008U;              /* kernel CS */
    *--sp = (uint32_t)task_bootstrap; /* EIP */
    *--sp = 0;                        /* EAX */
    *--sp = 0;                        /* ECX */
    *--sp = 0;                        /* EDX */
    *--sp = 0;                        /* EBX */
    *--sp = 0;                        /* original ESP placeholder */
    *--sp = 0;                        /* EBP */
    *--sp = 0;                        /* ESI */
    *--sp = 0;                        /* EDI */
    *--sp = 0x00000010U;              /* DS */
    *--sp = 0x00000010U;              /* ES */
    *--sp = 0x00000010U;              /* FS */
    *--sp = 0x00000010U;              /* GS */
    return (uint32_t)sp;
}

static uint32_t build_user_context(task_t *t, const task_user_context_t *ctx) {
    uint32_t eflags = (ctx->eflags | 0x202U) & ~0x00003000U;
    uint32_t *sp = (uint32_t *)(t->stack + TASK_STACK_SIZE);
    *--sp = GDT_USER_DATA;       /* SS */
    *--sp = ctx->user_esp;       /* user ESP */
    *--sp = eflags;              /* EFLAGS */
    *--sp = GDT_USER_CODE;       /* CS */
    *--sp = ctx->eip;            /* EIP */
    *--sp = ctx->eax;
    *--sp = ctx->ecx;
    *--sp = ctx->edx;
    *--sp = ctx->ebx;
    *--sp = 0;                   /* original ESP placeholder */
    *--sp = ctx->ebp;
    *--sp = ctx->esi;
    *--sp = ctx->edi;
    *--sp = GDT_USER_DATA;       /* DS */
    *--sp = GDT_USER_DATA;       /* ES */
    *--sp = GDT_USER_DATA;       /* FS */
    *--sp = GDT_USER_DATA;       /* GS */
    return (uint32_t)sp;
}

static void switch_tasks(task_t *from, task_t *to, uint32_t saved_eflags) {
    uint32_t *from_esp = &from->regs.esp;
    uint32_t to_esp = to->regs.esp;
    __asm__ volatile(
        "pushl %2\n"
        "pushl $0x08\n"
        "pushl $1f\n"
        "pushal\n"
        "mov %%ds, %%ax\n"
        "push %%eax\n"
        "mov %%es, %%ax\n"
        "push %%eax\n"
        "mov %%fs, %%ax\n"
        "push %%eax\n"
        "mov %%gs, %%ax\n"
        "push %%eax\n"
        "mov  %%esp, (%0)\n"
        "mov  %1,    %%esp\n"
        "jmp task_restore_context\n"
        "1:\n"
        :
        : "r"(from_esp), "r"(to_esp), "r"(saved_eflags)
        : "memory", "eax"
    );
}

/* -----------------------------------------------------------------------
 * scheduler_init — set up task 0 as the running kernel task
 * --------------------------------------------------------------------- */
void scheduler_init(void) {
    int i;
    for (i = 0; i < MAX_TASKS; i++) {
        reset_task_slot(&tasks[i], (uint32_t)i);
    }
    tasks[0].id    = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].regs.cr3 = kernel_cr3();
    tasks[0].name  = "kernel";
    tasks[0].started = 1;
    tasks[0].process_id = 0;
    current_task   = 0;
    num_tasks      = 1;
    preempt_enabled = 0;
    preempt_pending = 0;
    quantum_ticks = 10;
    current_ticks = 0;
    irq_switches = 0;
    coop_switches = 0;
}

/* -----------------------------------------------------------------------
 * task_create — allocate a task slot and set up its initial iret frame.
 * New tasks return into task_bootstrap(), which calls task->entry and
 * then task_exit() if the entry function ever returns.
 * --------------------------------------------------------------------- */
static int task_create_configured(const char *name,
                                  void (*func)(void),
                                  uint32_t pid,
                                  uint32_t cr3) {
    task_t   *t;
    uint32_t *sp;
    uint32_t flags;
    int slot = -1;
    int i;

    if (!func) return -1;
    flags = irq_save();
    for (i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_DEAD) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        irq_restore(flags);
        return -1;
    }
    if (slot >= num_tasks) num_tasks = slot + 1;

    t          = &tasks[slot];
    t->id      = (uint32_t)slot;
    t->state   = TASK_DEAD;
    t->name    = name;
    t->sleep_until = 0;
    t->priority = TASK_PRIORITY_DEFAULT;
    t->started = 0;
    t->process_id = pid;
    t->entry = func;
    sp = (uint32_t *)build_initial_context(t);
    t->regs.esp = (uint32_t)sp;
    t->regs.cr3 = cr3 ? cr3 : kernel_cr3();
    t->state = TASK_READY;
    irq_restore(flags);

    return slot;
}

int task_create(const char *name, void (*func)(void)) {
    return task_create_configured(name, func, 0, kernel_cr3());
}

int task_create_for_process(const char *name,
                            void (*func)(void),
                            uint32_t pid,
                            uint32_t cr3) {
    return task_create_configured(name, func, pid, cr3);
}

int task_create_user_for_process(const char *name,
                                 uint32_t pid,
                                 uint32_t cr3,
                                 const task_user_context_t *ctx) {
    task_t *t;
    uint32_t flags;
    int slot = -1;
    int i;
    if (!ctx || pid == 0 || cr3 == 0 || ctx->eip == 0 || ctx->user_esp == 0) return -1;
    flags = irq_save();
    for (i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_DEAD) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        irq_restore(flags);
        return -1;
    }
    if (slot >= num_tasks) num_tasks = slot + 1;
    t = &tasks[slot];
    t->id = (uint32_t)slot;
    t->state = TASK_DEAD;
    t->name = name;
    t->sleep_until = 0;
    t->priority = TASK_PRIORITY_DEFAULT;
    t->started = 0;
    t->process_id = pid;
    t->entry = 0;
    t->regs.esp = build_user_context(t, ctx);
    t->regs.cr3 = cr3;
    t->state = TASK_READY;
    irq_restore(flags);
    return slot;
}

int task_destroy(int task_id) {
    uint32_t flags;
    flags = irq_save();
    if (task_id <= 0 ||
        task_id >= num_tasks ||
        task_id == current_task ||
        tasks[task_id].state == TASK_DEAD) {
        irq_restore(flags);
        return -1;
    }
    reset_task_slot(&tasks[task_id], (uint32_t)task_id);
    trim_task_table();
    irq_restore(flags);
    return 0;
}

const task_t *task_at(uint32_t index) {
    if (index >= (uint32_t)num_tasks || tasks[index].state == TASK_DEAD) return 0;
    return &tasks[index];
}

uint32_t task_table_size(void) {
    return (uint32_t)num_tasks;
}

uint32_t task_free_slots(void) {
    uint32_t free = 0;
    uint32_t i;
    for (i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_DEAD) free++;
    }
    return free;
}

const char *task_state_name(task_state_t state) {
    switch (state) {
        case TASK_READY: return "ready";
        case TASK_RUNNING: return "running";
        case TASK_SLEEPING: return "sleeping";
        case TASK_DEAD: return "dead";
        default: return "unknown";
    }
}

int task_set_address_space(int task_id, uint32_t cr3) {
    uint32_t flags;
    flags = irq_save();
    if (task_id < 0 || task_id >= num_tasks || tasks[task_id].state == TASK_DEAD) {
        irq_restore(flags);
        return -1;
    }
    tasks[task_id].regs.cr3 = cr3 ? cr3 : kernel_cr3();
    irq_restore(flags);
    return 0;
}

void task_set_current_address_space(uint32_t cr3) {
    uint32_t flags = irq_save();
    if (current_task < 0 || current_task >= num_tasks) {
        irq_restore(flags);
        return;
    }
    tasks[current_task].regs.cr3 = cr3 ? cr3 : kernel_cr3();
    irq_restore(flags);
}

uint32_t task_current_address_space(void) {
    if (current_task < 0 || current_task >= num_tasks) return kernel_cr3();
    return tasks[current_task].regs.cr3 ? tasks[current_task].regs.cr3 : kernel_cr3();
}

int task_set_priority(int task_id, uint32_t priority) {
    uint32_t flags;
    if (priority > TASK_PRIORITY_MAX) return -1;
    flags = irq_save();
    if (task_id < 0 || task_id >= num_tasks || tasks[task_id].state == TASK_DEAD) {
        irq_restore(flags);
        return -1;
    }
    tasks[task_id].priority = priority;
    irq_restore(flags);
    return 0;
}

uint32_t task_priority(int task_id) {
    uint32_t priority = 0;
    uint32_t flags = irq_save();
    if (task_id >= 0 && task_id < num_tasks && tasks[task_id].state != TASK_DEAD)
        priority = tasks[task_id].priority;
    irq_restore(flags);
    return priority;
}

int task_set_process_id(int task_id, uint32_t pid) {
    uint32_t flags;
    flags = irq_save();
    if (task_id < 0 || task_id >= num_tasks || tasks[task_id].state == TASK_DEAD) {
        irq_restore(flags);
        return -1;
    }
    tasks[task_id].process_id = pid;
    irq_restore(flags);
    return 0;
}

void task_set_current_process_id(uint32_t pid) {
    uint32_t flags = irq_save();
    if (current_task < 0 || current_task >= num_tasks) {
        irq_restore(flags);
        return;
    }
    tasks[current_task].process_id = pid;
    irq_restore(flags);
}

uint32_t task_current_process_id(void) {
    if (current_task < 0 || current_task >= num_tasks) return 0;
    return tasks[current_task].process_id;
}

int task_kill_process(uint32_t pid) {
    uint32_t flags;
    int killed = 0;
    int i;
    if (pid == 0) return -1;
    flags = irq_save();
    for (i = 1; i < num_tasks; i++) {
        if (i == current_task) continue;
        if (tasks[i].state != TASK_DEAD && tasks[i].process_id == pid) {
            tasks[i].state = TASK_DEAD;
            tasks[i].sleep_until = 0;
            tasks[i].process_id = 0;
            tasks[i].regs.cr3 = kernel_cr3();
            killed++;
        }
    }
    trim_task_table();
    irq_restore(flags);
    return killed ? 0 : -1;
}

/* -----------------------------------------------------------------------
 * task_yield — cooperative yield; find the next READY/RUNNING task
 * --------------------------------------------------------------------- */
static void task_yield_internal(int allow_unstarted) {
    uint32_t flags = irq_save();
    int next = pick_next_task(allow_unstarted);

    if (next >= 0) {
        int old = current_task;
        task_state_t old_state = tasks[old].state;
        current_task = next;
        tasks[next].state = TASK_RUNNING;
        tasks[next].started = 1;
        if (old_state == TASK_RUNNING) {
            tasks[old].state = TASK_READY;
            mark_task_process_state(&tasks[old], PROC_READY);
        }
        mark_task_process_state(&tasks[next], PROC_RUNNING);
        current_ticks = 0;
        preempt_pending = 0;
        coop_switches++;
        activate_task_context(&tasks[next]);
        switch_tasks(&tasks[old], &tasks[next], flags);
        return;
    }
    irq_restore(flags);
    /* Nothing else to run — stay in current task */
}

void task_yield(void) {
    task_yield_internal(1);
}

/* -----------------------------------------------------------------------
 * task_sleep — put current task to sleep for `ms` milliseconds
 * --------------------------------------------------------------------- */
void task_sleep(uint32_t ms) {
    uint32_t wait_ticks = timer_ms_to_ticks(ms);
    uint32_t target = timer_ticks() + wait_ticks;
    uint32_t flags = irq_save();
    if (wait_ticks == 0) return;
    tasks[current_task].state       = TASK_SLEEPING;
    tasks[current_task].sleep_until = target;
    mark_task_process_state(&tasks[current_task], PROC_SLEEPING);
    irq_restore(flags);
    __asm__ volatile("sti" : : : "memory");
    while (1) {
        task_yield();
        flags = irq_save();
        if (tasks[current_task].state == TASK_READY) {
            tasks[current_task].state = TASK_RUNNING;
            mark_task_process_state(&tasks[current_task], PROC_RUNNING);
            irq_restore(flags);
            break;
        }
        if (tasks[current_task].state != TASK_SLEEPING) {
            irq_restore(flags);
            break;
        }
        if (tick_reached(timer_ticks(), target)) {
            tasks[current_task].state = TASK_RUNNING;
            tasks[current_task].sleep_until = 0;
            mark_task_process_state(&tasks[current_task], PROC_RUNNING);
            irq_restore(flags);
            break;
        }
        irq_restore(flags);
        __asm__ volatile("hlt");
    }
}

/* -----------------------------------------------------------------------
 * task_exit — mark current task as DEAD and yield away
 * --------------------------------------------------------------------- */
void task_exit(void) {
    uint32_t flags;
    if (task_current_process_id() != 0) process_exit(0);
    flags = irq_save();
    tasks[current_task].state = TASK_DEAD;
    tasks[current_task].sleep_until = 0;
    tasks[current_task].process_id = 0;
    tasks[current_task].regs.cr3 = kernel_cr3();
    current_ticks = 0;
    preempt_pending = 0;
    irq_restore(flags);
    /* Yield until another task picks up; we will never be scheduled again */
    while (1) {
        task_yield();
    }
}

/* -----------------------------------------------------------------------
 * task_current_id
 * --------------------------------------------------------------------- */
int task_current_id(void) {
    return (int)tasks[current_task].id;
}

/* -----------------------------------------------------------------------
 * scheduler_tick — called from timer IRQ every 1 ms
 * Wakes sleeping tasks whose deadline has passed.
 * --------------------------------------------------------------------- */
void scheduler_tick(void) {
    uint32_t now = timer_ticks();
    int i;
    for (i = 0; i < num_tasks; i++) {
        if (tasks[i].state == TASK_SLEEPING && tick_reached(now, tasks[i].sleep_until)) {
            tasks[i].state = TASK_READY;
            mark_task_process_state(&tasks[i], PROC_READY);
        }
    }
    if (preempt_enabled && num_tasks > 1 &&
        tasks[current_task].state == TASK_RUNNING) {
        current_ticks++;
        if (current_ticks >= quantum_ticks) preempt_pending = 1;
    }
}

void scheduler_set_preemption(int enabled, uint32_t quantum_ms) {
    uint32_t flags = irq_save();
    preempt_enabled = enabled ? 1 : 0;
    quantum_ticks = timer_ms_to_ticks(quantum_ms ? quantum_ms : 1U);
    current_ticks = 0;
    preempt_pending = 0;
    irq_restore(flags);
}

int scheduler_preemption_enabled(void) {
    return preempt_enabled;
}

int scheduler_preempt_pending(void) {
    return preempt_pending;
}

uint32_t scheduler_quantum_ticks(void) {
    return quantum_ticks;
}

uint32_t scheduler_current_ticks(void) {
    return current_ticks;
}

uint32_t scheduler_irq_switches(void) {
    return irq_switches;
}

uint32_t scheduler_coop_switches(void) {
    return coop_switches;
}

void scheduler_maybe_preempt(void) {
    uint32_t flags;
    int next;
    int should_switch;
    if (!preempt_enabled || !preempt_pending) return;
    flags = irq_save();
    next = pick_next_task(0);
    should_switch = current_task >= 0 &&
                    current_task < num_tasks &&
                    next >= 0 &&
                    tasks[next].priority >= tasks[current_task].priority;
    if (!should_switch) {
        preempt_pending = 0;
        current_ticks = 0;
    }
    irq_restore(flags);
    if (should_switch) task_yield_internal(0);
}

uint32_t scheduler_preempt_from_irq(uint32_t irq_esp) {
    int next;
    int old;
    if (!preempt_enabled || !preempt_pending) return irq_esp;
    if (num_tasks <= 1) return irq_esp;
    if (current_task < 0 || current_task >= num_tasks) return irq_esp;
    if (tasks[current_task].state != TASK_RUNNING) return irq_esp;
    next = pick_next_task(0);
    if (next < 0 || tasks[next].priority < tasks[current_task].priority) return irq_esp;
    old = current_task;
    tasks[old].regs.esp = irq_esp;
    if (tasks[old].state == TASK_RUNNING) {
        tasks[old].state = TASK_READY;
        mark_task_process_state(&tasks[old], PROC_READY);
    }
    current_task = next;
    tasks[next].state = TASK_RUNNING;
    tasks[next].started = 1;
    mark_task_process_state(&tasks[next], PROC_RUNNING);
    current_ticks = 0;
    preempt_pending = 0;
    irq_switches++;
    activate_task_context(&tasks[next]);
    return tasks[next].regs.esp;
}
