/*
 * isr.S - x86_64 Interrupt Service Routine Stubs
 *
 * These stubs save CPU state, call C handlers, restore state, and return.
 * In Long Mode, the interrupt frame differs from 32-bit mode.
 */

.code64

/*
 * Common ISR stub - saves all registers and calls C handler
 */
.extern exception_handler
.extern irq_handler

.macro ISR_NOERRCODE vector
    .global isr\vector
    isr\vector:
        pushq $0                /* Dummy error code */
        pushq $\vector
        jmp isr_common_stub
.endm

.macro ISR_ERRCODE vector
    .global isr\vector
    isr\vector:
        /* Error code already pushed by CPU */
        pushq $\vector
        jmp isr_common_stub
.endm

.macro IRQ vector, irqnum
    .global irq\irqnum
    irq\irqnum:
        pushq $0                /* Dummy error code */
        pushq $\vector
        jmp irq_common_stub
.endm

/*
 * Exception ISRs (0-31)
 */
ISR_NOERRCODE 0     /* Divide Error */
ISR_NOERRCODE 1     /* Debug */
ISR_NOERRCODE 2     /* NMI */
ISR_NOERRCODE 3     /* Breakpoint */
ISR_NOERRCODE 4     /* Overflow */
ISR_NOERRCODE 5     /* Bound Range */
ISR_NOERRCODE 6     /* Invalid Opcode */
ISR_NOERRCODE 7     /* Device Not Available */
ISR_ERRCODE   8     /* Double Fault (error code) */
ISR_NOERRCODE 9     /* Coprocessor Segment */
ISR_ERRCODE   10    /* Invalid TSS (error code) */
ISR_ERRCODE   11    /* Segment Not Present (error code) */
ISR_ERRCODE   12    /* Stack Segment (error code) */
ISR_ERRCODE   13    /* General Protection (error code) */
ISR_ERRCODE   14    /* Page Fault (error code) */
ISR_NOERRCODE 15    /* Reserved */
ISR_NOERRCODE 16    /* x87 FP */
ISR_ERRCODE   17    /* Alignment Check (error code) */
ISR_NOERRCODE 18    /* Machine Check */
ISR_NOERRCODE 19    /* SIMD FP */
ISR_NOERRCODE 20    /* Virtualization */
ISR_ERRCODE   21    /* Control Protection (error code) */
ISR_NOERRCODE 22    /* Reserved */
ISR_NOERRCODE 23    /* Reserved */
ISR_NOERRCODE 24    /* Reserved */
ISR_NOERRCODE 25    /* Reserved */
ISR_NOERRCODE 26    /* Reserved */
ISR_NOERRCODE 27    /* Reserved */
ISR_NOERRCODE 28    /* Hypervisor Injection */
ISR_ERRCODE   29    /* VMM Communication (error code) */
ISR_ERRCODE   30    /* Security Exception (error code) */
ISR_NOERRCODE 31    /* Reserved */

/*
 * Hardware IRQs (32-47)
 */
IRQ 32, 0
IRQ 33, 1
IRQ 34, 2
IRQ 35, 3
IRQ 36, 4
IRQ 37, 5
IRQ 38, 6
IRQ 39, 7
IRQ 40, 8
IRQ 41, 9
IRQ 42, 10
IRQ 43, 11
IRQ 44, 12
IRQ 45, 13
IRQ 46, 14
IRQ 47, 15

/*
 * Syscall via int 0x80 (Linux compatibility)
 */
.global isr128
isr128:
    pushq $0                    /* No error code */
    pushq $0x80                 /* Vector 128 */
    jmp isr_common_stub

/*
 * Common ISR stub
 *
 * Stack on entry:
 *   [RSP+0]   = interrupt number
 *   [RSP+8]   = error code (or dummy 0)
 *   [RSP+16]  = RIP (pushed by CPU)
 *   [RSP+24]  = CS (pushed by CPU)
 *   [RSP+32]  = RFLAGS (pushed by CPU)
 *   [RSP+40]  = RSP (pushed by CPU, if privilege change)
 *   [RSP+48]  = SS (pushed by CPU, if privilege change)
 */
isr_common_stub:
    /* Save all general purpose registers */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    /* Save data segment registers (might be user values) */
    /* In Long Mode, DS/ES/SS are largely unused but save for completeness */
    
    /* Pass pointer to interrupt_frame as argument */
    movq %rsp, %rdi
    
    /* Ensure 16-byte stack alignment for C ABI */
    andq $~0x0F, %rsp
    
    /* Call C handler */
    call exception_handler
    
    /* Restore RSP (handler may return on different stack) */
    movq %rdi, %rsp
    
    /* Restore registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    
    /* Pop interrupt number and error code */
    addq $16, %rsp
    
    /* Return from interrupt */
    iretq

/*
 * Common IRQ stub (similar but calls irq_handler)
 */
irq_common_stub:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    movq %rsp, %rdi
    andq $~0x0F, %rsp
    
    call irq_handler
    
    movq %rdi, %rsp
    
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    
    addq $16, %rsp
    iretq

/*
 * SWAPGS helper - swap GS base on kernel entry from userspace
 * Called at start of exception if coming from Ring 3
 */
.global swapgs_if_needed
swapgs_if_needed:
    /* Check if we came from Ring 3 by examining saved CS */
    testb $3, 24(%rsp)          /* CS is at offset 24 from isr stub entry */
    jz 1f                       /* If Ring 0, skip swapgs */
    swapgs
1:  ret
