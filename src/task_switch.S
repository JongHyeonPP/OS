.global task_restore_context
task_restore_context:
    pop %eax
    mov %ax, %gs
    pop %eax
    mov %ax, %fs
    pop %eax
    mov %ax, %es
    pop %eax
    mov %ax, %ds
    popal
    iret
