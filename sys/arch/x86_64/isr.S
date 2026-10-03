/*
 * isr.S - x86_64 interrupt entry and return
 *
 * Every vector's stub pushes an error code (the CPU's, or 0) and the
 * vector number, then joins isr_common_stub, which builds the trap frame
 * declared in idt.h (registers_t) and calls isr_handler(), or
 * syscall_handler() for int $0x80.  The vectors are the same as the i386
 * kernel's (arch/i386/isr.S).
 *
 * The kernel does not use %fs or %gs and runs with whatever %ds/%es held,
 * which 64-bit code ignores.  So nothing is loaded on entry; the four
 * selectors are saved in the frame and reloaded before iretq, which is
 * what keeps a 32-bit process's TLS selector its own across a context
 * switch (reloading %gs also reloads its base from the GDT, where
 * arch_switch_to has put the incoming thread's TLS descriptor).
 */

.code64
.text

.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    cli
    pushq $0
    pushq $\num
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr\num
isr\num:
    cli
    pushq $\num
    jmp isr_common_stub
.endm

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_ERRCODE   21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_ERRCODE   29
ISR_ERRCODE   30
ISR_NOERRCODE 31
ISR_NOERRCODE 32
ISR_NOERRCODE 33
ISR_NOERRCODE 34
ISR_NOERRCODE 35
ISR_NOERRCODE 36
ISR_NOERRCODE 37
ISR_NOERRCODE 38
ISR_NOERRCODE 39
ISR_NOERRCODE 40
ISR_NOERRCODE 41
ISR_NOERRCODE 42
ISR_NOERRCODE 43
ISR_NOERRCODE 44
ISR_NOERRCODE 45
ISR_NOERRCODE 46
ISR_NOERRCODE 47
ISR_NOERRCODE 128

/* Dynamic (MSI/MSI-X) vectors 0x50..0xBF, skipping 0x80, as on i386. */
.altmacro
.set msivec, 0x50
.rept (0x7F - 0x50 + 1)
    ISR_NOERRCODE %msivec
    .set msivec, msivec + 1
.endr
.set msivec, 0x81
.rept (0xBF - 0x81 + 1)
    ISR_NOERRCODE %msivec
    .set msivec, msivec + 1
.endr
.noaltmacro

.macro MSISTUBPTR n
    .quad isr\n
.endm

.section .rodata
.align 8
.global msi_isr_stubs
msi_isr_stubs:
.altmacro
.set msivec, 0x50
.rept (0xBF - 0x50 + 1)
    MSISTUBPTR %msivec
    .set msivec, msivec + 1
.endr
.noaltmacro
.text

ISR_NOERRCODE 253
ISR_NOERRCODE 254

/* Panic IPI: never returns. */
.global isr_panic_ipi
isr_panic_ipi:
    cli
1:  hlt
    jmp 1b

/* LAPIC spurious interrupt: no EOI (the LAPIC has nothing in service). */
.global isr_spurious
isr_spurious:
    iretq

/* Offsets in registers_t: 4 selectors and 16 register slots, then int_no,
 * err_code, rip, cs. */
#define TF_INT_NO   (20 * 8)
#define TF_CS       (TF_INT_NO + 3 * 8)

.global isr_common_stub
isr_common_stub:
    /* The i386 pusha order, then r8-r15, so registers_t reads upward. */
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq $0                    /* pusha's ESP slot */
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    xorl %eax, %eax
    movw %ds, %ax
    pushq %rax
    movw %es, %ax
    pushq %rax
    movw %fs, %ax
    pushq %rax
    movw %gs, %ax
    pushq %rax

    cld
    /* The frame pointer stays in %rbx, which the C handler preserves, so
     * the stack can be realigned for the call and put back after it. */
    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $~0xF, %rsp

    cmpq $128, TF_INT_NO(%rbx)
    je 1f
    call isr_handler
    jmp 2f
1:  call syscall_handler
2:  movq %rbx, %rsp

.global isr_exit
isr_exit:
    /* Only a return to user mode needs the user's selectors back; a kernel
     * frame's are whatever the kernel ran with. */
    popq %rax
    testb $3, (TF_CS - 8)(%rsp)         /* the gs slot is already popped */
    jz 3f
    movw %ax, %gs
    popq %rax
    movw %ax, %fs
    popq %rax
    movw %ax, %es
    popq %rax
    movw %ax, %ds
    jmp 4f
3:  addq $24, %rsp
4:  popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    addq $8, %rsp               /* pusha's ESP slot */
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    addq $16, %rsp              /* vector and error code */
    iretq

.global idt_flush
idt_flush:
    lidt (%rdi)
    ret

/*
 * void switch_stacks(uintptr_t *save_sp, uintptr_t load_sp)
 *
 * The context switch proper (switch_to() in sched.c calls it with
 * &prev->kstack_ptr and next->kstack_ptr): save the callee-saved
 * registers on this stack, store the stack pointer, load the other one
 * and pop its registers.  A thread that has never run starts with a stack
 * built by sched.c in exactly this shape, so the ret lands in its entry
 * trampoline.
 */
.global switch_stacks
switch_stacks:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

/*
 * fork_child_return - a forked (or cloned) thread's first run.  sched.c
 * built a complete trap frame, the parent's with %eax = 0, directly above
 * the switch_stacks frame, so returning to user mode is isr_exit's job.
 */
.global fork_child_return
fork_child_return:
    cli
    jmp isr_exit

/*
 * new_kernel_thread_trampoline - a kernel thread's first run.  switch_to()
 * runs with interrupts disabled and does not restore RFLAGS, so enable
 * them, then call entry(arg) from %r15/%r14 (set up by
 * sched_create_thread) and exit the thread if it returns.
 */
.global new_kernel_thread_trampoline
new_kernel_thread_trampoline:
    sti
    movq %r14, %rdi
    andq $~0xF, %rsp
    call *%r15
    call sched_kernel_thread_exit
1:  hlt
    jmp 1b

/*
 * new_user_thread_trampoline - a user thread's first run (thr_new): enter
 * user_entry(user_arg) on user_stack in compatibility mode, as the i386
 * kernel does.  %r15 = user_entry, %r14 = user_stack, %r13 = user_arg.
 * arch_switch_to has already pointed the TLS slot at this thread's base,
 * so loading %gs with 0x33 picks it up.
 */
.global new_user_thread_trampoline
new_user_thread_trampoline:
    cli
    /* The cdecl frame on the user stack: arg, then return address 0. */
    subl $4, %r14d
    movl %r13d, (%r14)
    subl $4, %r14d
    movl $0, (%r14)

    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    xorw %ax, %ax
    movw %ax, %fs
    movw $0x33, %ax
    movw %ax, %gs

    pushq $0x23                 /* SS */
    pushq %r14                  /* ESP */
    pushfq
    orq $0x200, (%rsp)          /* IF */
    pushq $0x1B                 /* CS: 32-bit user code */
    pushq %r15                  /* EIP */
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    iretq

/*
 * void jump_to_userspace(uint32_t entry, uint32_t stack, uint32_t ebx)
 *
 * Enter a freshly exec'd 32-bit image: flat user selectors, empty %fs and
 * %gs, %ebx as the personality wants it (ps_strings for the BSDs), every
 * other register clear -- the i386 kernel's contract.
 */
.global jump_to_userspace
jump_to_userspace:
    cli
    movl %edi, %ecx             /* entry */
    movl %edx, %ebx             /* the entry %ebx */
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    pushq $0x23                 /* SS */
    movl %esi, %esi             /* zero-extend the stack */
    pushq %rsi                  /* ESP */
    pushfq
    orq $0x200, (%rsp)          /* IF */
    pushq $0x1B                 /* CS: 32-bit user code */
    pushq %rcx                  /* EIP */
    xorl %eax, %eax
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %r10d, %r10d
    xorl %r11d, %r11d
    iretq

/*
 * void jump_to_elks(uint32_t entry, uint32_t stack, uint32_t cs,
 *                   uint32_t ds, uint32_t ss, uint32_t es, uint32_t dx_value)
 *
 * The same for segmented programs, on the selectors given (LDT segments;
 * compatibility mode runs 16- and 32-bit code segments alike).
 * Arguments: %edi %esi %edx %ecx %r8d %r9d, dx_value on the stack.
 */
.global jump_to_elks
jump_to_elks:
    cli
    movl 8(%rsp), %eax          /* dx_value */
    movl %eax, %r11d
    movw %cx, %ds
    movw %r9w, %es
    movw %cx, %fs
    movw %cx, %gs

    movl %r8d, %eax
    pushq %rax                  /* SS */
    movl %esi, %esi
    pushq %rsi                  /* ESP */
    pushfq
    orq $0x200, (%rsp)          /* IF */
    movl %edx, %eax
    pushq %rax                  /* CS */
    movl %edi, %edi
    pushq %rdi                  /* EIP */
    movl %r11d, %edx            /* ELKS expects the stack size in DX */
    xorl %eax, %eax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %ebp, %ebp
    iretq
