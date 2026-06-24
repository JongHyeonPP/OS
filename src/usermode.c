#include "usermode.h"
#include "gdt.h"
#include "process.h"

static int user_entry_valid(uint32_t entry) {
    return entry >= USER_BASE && entry < (USER_STACK_TOP - USER_STACK_SIZE);
}

static int user_stack_valid(uint32_t user_stack) {
    return user_stack >= (USER_STACK_TOP - USER_STACK_SIZE) &&
           user_stack < USER_STACK_TOP &&
           (user_stack & 3U) == 0;
}

int usermode_enter(uint32_t entry, uint32_t user_stack) {
    if (!user_entry_valid(entry) || !user_stack_valid(user_stack)) return -1;
    __asm__ volatile(
        "cli\n"
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushl %0\n"
        "pushl %1\n"
        "pushfl\n"
        "pop %%eax\n"
        "or $0x200, %%eax\n"
        "push %%eax\n"
        "pushl %2\n"
        "pushl %3\n"
        "iret\n"
        : : "i"(GDT_USER_DATA), "r"(user_stack), "i"(GDT_USER_CODE), "r"(entry)
        : "eax", "memory"
    );
    return -1;
}
