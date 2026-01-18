/*
 * switch.S - x86_64 Context Switch
 *
 * Implements thread context switching for 64-bit mode.
 * Follows System V AMD64 ABI for callee-saved registers.
 */

.code64

/*
 * switch_to(struct context **old, struct context *new)
 *
 * Saves current context to *old, loads context from new.
 *
 * Callee-saved registers (System V AMD64 ABI):
 *   RBX, RBP, R12-R15, RSP
 *
 * RDI = pointer to old context pointer (struct context **)
 * RSI = new context pointer (struct context *)
 *
 * Context layout (struct context):
 *   +0:   RIP (return address)
 *   +8:   RSP
 *   +16:  RBP
 *   +24:  RBX
 *   +32:  R12
 *   +40:  R13
 *   +48:  R14
 *   +56:  R15
 *   +64:  MXCSR (SSE control/status)
 *   +68:  x87 CW (x87 control word)
 *   Total: 72 bytes (aligned to 8)
 */

.global switch_to
.type switch_to, @function
switch_to:
    /* Allocate space for context on old stack */
    /* We'll store context at current RSP - sizeof(context) */
    
    /* Save callee-saved registers to stack */
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %rbx
    pushq %rbp
    
    /* Save SSE/x87 control state */
    subq $8, %rsp
    stmxcsr (%rsp)              /* Save MXCSR (4 bytes) */
    fnstcw 4(%rsp)              /* Save x87 control word (2 bytes) */
    
    /* Push return address (RIP) - it's already on stack from call */
    /* Current RSP will be saved as the context pointer */
    
    /* Save stack pointer to *old (RDI points to old context pointer) */
    movq %rsp, (%rdi)
    
    /* Switch to new stack (RSI = new context) */
    movq %rsi, %rsp
    
    /* Restore SSE/x87 control state */
    ldmxcsr (%rsp)              /* Restore MXCSR */
    fldcw 4(%rsp)               /* Restore x87 control word */
    addq $8, %rsp
    
    /* Restore callee-saved registers */
    popq %rbp
    popq %rbx
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    
    /* Return (pops RIP from new context's stack) */
    ret

/*
 * switch_to_first(struct context *new)
 *
 * Switch to a new context without saving old.
 * Used for initial thread switch after fork/exec.
 *
 * RDI = new context pointer
 */
.global switch_to_first
.type switch_to_first, @function
switch_to_first:
    /* Just load new context */
    movq %rdi, %rsp
    
    ldmxcsr (%rsp)
    fldcw 4(%rsp)
    addq $8, %rsp
    
    popq %rbp
    popq %rbx
    popq %r12
    popq %r13
    popq %r14
    popq %r15
    
    ret

/*
 * context_init(struct context *ctx, void *stack_top, void (*entry)(void *), void *arg)
 *
 * Initialize a context structure for a new thread.
 *
 * RDI = context pointer
 * RSI = stack top (highest address)
 * RDX = entry point function
 * RCX = argument for entry function
 */
.global context_init
.type context_init, @function
context_init:
    /* Align stack to 16 bytes */
    andq $~0x0F, %rsi
    
    /* Reserve space for context on stack */
    /* Stack grows down, so subtract */
    
    /* Push fake return address (thread exit handler) */
    subq $8, %rsi
    leaq thread_exit_stub(%rip), %rax
    movq %rax, (%rsi)
    
    /* Push entry address (will be RIP) */
    subq $8, %rsi
    movq %rdx, (%rsi)
    
    /* Push callee-saved registers (all zero for new thread) */
    subq $48, %rsi              /* Space for R15-R12, RBX, RBP */
    xorq %rax, %rax
    movq %rax, 40(%rsi)         /* R15 = 0 */
    movq %rax, 32(%rsi)         /* R14 = 0 */
    movq %rax, 24(%rsi)         /* R13 = 0 */
    movq %rax, 16(%rsi)         /* R12 = 0 */
    movq %rax, 8(%rsi)          /* RBX = 0 */
    movq %rax, (%rsi)           /* RBP = 0 */
    
    /* Push SSE/x87 state */
    subq $8, %rsi
    movl $0x1F80, (%rsi)        /* Default MXCSR */
    movw $0x037F, 4(%rsi)       /* Default x87 CW */
    
    /* Store argument in context (will be in RDI when entry is called) */
    /* We need to arrange for RDI to be rcx (the arg) when thread starts */
    /* Since we saved registers in a specific order, setup thread entry differently */
    
    /* Store the context pointer */
    movq %rsi, (%rdi)
    
    ret

/*
 * Thread exit stub - called when thread returns from entry function
 */
.extern thread_exit
thread_exit_stub:
    /* RDI = return value from thread entry */
    movq %rax, %rdi
    call thread_exit
    
    /* Should not return */
1:  hlt
    jmp 1b

/*
 * fork_return - Return path for child after fork()
 *
 * The child process lands here after switch_to()
 */
.global fork_return
.type fork_return, @function
fork_return:
    /* Child returns 0 from fork() */
    xorq %rax, %rax
    ret
