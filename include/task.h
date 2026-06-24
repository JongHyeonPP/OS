#ifndef TASK_H
#define TASK_H
#include <stdint.h>

#define MAX_TASKS       8
#define TASK_STACK_SIZE 8192
#define TASK_PRIORITY_MIN 0U
#define TASK_PRIORITY_DEFAULT 10U
#define TASK_PRIORITY_MAX 31U

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_DEAD
} task_state_t;

typedef struct {
    uint32_t esp;   /* saved stack pointer */
    uint32_t cr3;   /* page directory for this task */
} task_regs_t;

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
} task_user_context_t;

typedef struct task {
    uint32_t       id;
    task_state_t   state;
    task_regs_t    regs;
    uint32_t       sleep_until;   /* timer tick to wake at */
    uint32_t       priority;
    const char    *name;
    int            started;
    uint32_t       process_id;
    void         (*entry)(void);
    uint8_t        stack[TASK_STACK_SIZE];
} task_t;

/* Create a new task running func(). Returns task id or -1 on error. */
int  task_create(const char *name, void (*func)(void));
int  task_create_for_process(const char *name,
                             void (*func)(void),
                             uint32_t pid,
                             uint32_t cr3);
int  task_create_user_for_process(const char *name,
                                  uint32_t pid,
                                  uint32_t cr3,
                                  const task_user_context_t *ctx);
int  task_destroy(int task_id);
const task_t *task_at(uint32_t index);
uint32_t task_table_size(void);
uint32_t task_free_slots(void);
const char *task_state_name(task_state_t state);
uint32_t scheduler_irq_switches(void);
uint32_t scheduler_coop_switches(void);
int  task_set_address_space(int task_id, uint32_t cr3);
void task_set_current_address_space(uint32_t cr3);
uint32_t task_current_address_space(void);
int  task_set_priority(int task_id, uint32_t priority);
uint32_t task_priority(int task_id);
int  task_set_process_id(int task_id, uint32_t pid);
void task_set_current_process_id(uint32_t pid);
uint32_t task_current_process_id(void);
int  task_kill_process(uint32_t pid);
void task_yield(void);          /* cooperative yield */
void task_sleep(uint32_t ms);   /* sleep for ms milliseconds */
void task_exit(void);           /* terminate current task */
int  task_current_id(void);
void scheduler_init(void);
void scheduler_tick(void);      /* called from timer IRQ */
void scheduler_set_preemption(int enabled, uint32_t quantum_ms);
int  scheduler_preemption_enabled(void);
int  scheduler_preempt_pending(void);
uint32_t scheduler_quantum_ticks(void);
uint32_t scheduler_current_ticks(void);
void scheduler_maybe_preempt(void);
uint32_t scheduler_preempt_from_irq(uint32_t irq_esp);

#endif /* TASK_H */
