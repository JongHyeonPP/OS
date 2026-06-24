.extern irq_handler_c
.extern scheduler_preempt_from_irq
.extern panic
.extern exception_handler_c

/* Default exception stub: just halt cleanly instead of triple-faulting */
.global exc_halt_stub
exc_halt_stub:
    cli
    cld
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    push $exc_halt_msg
    call panic
    add $4, %esp
1:  hlt
    jmp 1b

.section .rodata
exc_halt_msg:
    .asciz "unhandled CPU exception"
.text

.macro EXC_NOERR num
.global exc\num\()_stub
exc\num\()_stub:
    cli
    cld
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov 0(%esp), %eax          /* interrupted EIP */
    mov 4(%esp), %edx          /* interrupted CS */
    push %edx                  /* cs */
    push %eax                  /* eip */
    push $0
    push $\num
    call exception_handler_c
    add $16, %esp
1:  hlt
    jmp 1b
.endm

.macro EXC_ERR num
.global exc\num\()_stub
exc\num\()_stub:
    cli
    cld
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov 0(%esp), %ecx          /* CPU-pushed error code */
    mov 4(%esp), %eax          /* interrupted EIP */
    mov 8(%esp), %edx          /* interrupted CS */
    push %edx                  /* cs */
    push %eax                  /* eip */
    push %ecx                  /* error */
    push $\num
    call exception_handler_c
    add $16, %esp
1:  hlt
    jmp 1b
.endm

EXC_NOERR 0
EXC_NOERR 1
EXC_NOERR 2
EXC_NOERR 3
EXC_NOERR 4
EXC_NOERR 5
EXC_NOERR 6
EXC_NOERR 7
EXC_ERR   8
EXC_NOERR 9
EXC_ERR   10
EXC_ERR   11
EXC_ERR   12
EXC_ERR   13
EXC_ERR   14
EXC_NOERR 15
EXC_NOERR 16
EXC_ERR   17
EXC_NOERR 18
EXC_NOERR 19
EXC_NOERR 20
EXC_ERR   21
EXC_NOERR 22
EXC_NOERR 23
EXC_NOERR 24
EXC_NOERR 25
EXC_NOERR 26
EXC_NOERR 27
EXC_NOERR 28
EXC_NOERR 29
EXC_ERR   30
EXC_NOERR 31

.macro IRQ num
.global irq\num\()_stub
irq\num\()_stub:
    cld
    pushal
    mov %ds, %ax
    push %eax
    mov %es, %ax
    push %eax
    mov %fs, %ax
    push %eax
    mov %gs, %ax
    push %eax
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    push $\num
    call irq_handler_c
    add $4, %esp
    mov %esp, %eax
    push %eax
    call scheduler_preempt_from_irq
    add $4, %esp
    mov %eax, %esp
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
.endm

IRQ 0
IRQ 1
IRQ 2
IRQ 3
IRQ 4
IRQ 5
IRQ 6
IRQ 7
IRQ 8
IRQ 9
IRQ 10
IRQ 11
IRQ 12
IRQ 13
IRQ 14
IRQ 15
/* ------------------------------------------------------------------
 * int 0x80 syscall stub
 * On entry: EAX = syscall number, EBX/ECX/EDX/ESI = arguments.
 * We save all registers, pass (num, a, b, c, d, frame) to syscall_handler_c_frame,
 * store the return value in the saved EAX slot, then restore and iret.
 * ------------------------------------------------------------------ */
.extern syscall_handler_c_frame

.global syscall0x80_stub
syscall0x80_stub:
    cld
    pushal                      /* push EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX */
    mov %ds, %ax
    push %eax
    mov %es, %ax
    push %eax
    mov %fs, %ax
    push %eax
    mov %gs, %ax
    push %eax
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    /* pushal layout on stack (top = low address):
       [esp+0..15] = saved GS/FS/ES/DS
       [esp+16] = EDI
       [esp+20] = ESI
       [esp+24] = EBP
       [esp+28] = orig ESP (ignored)
       [esp+32] = EBX   <- arg
       [esp+36] = EDX
       [esp+40] = ECX
       [esp+44] = EAX   <- num */
    mov 44(%esp), %eax          /* syscall number */
    mov 32(%esp), %ebx          /* arg 1 */
    mov 40(%esp), %ecx          /* arg 2 */
    mov 36(%esp), %edx          /* arg 3 */
    mov 20(%esp), %esi          /* arg 4 */
    mov %esp, %edi              /* saved syscall/iret frame */
    push %edi
    push %esi
    push %edx
    push %ecx
    push %ebx
    push %eax
    call syscall_handler_c_frame
    add  $24, %esp
    mov  %eax, 44(%esp)         /* saved EAX becomes syscall return value */
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
