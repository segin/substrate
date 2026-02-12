/*
 * sys/arch/i386/vm86_asm.S
 *
 * Assembly helpers for VM86 mode transitions.
 * Implements robust stack handling for VM86 entry and exit.
 */

.section .text
.global vm86_enter
.global vm86_bios_ret_point

/* Offsets into struct vm86_struct (specifically info->regs) */
/* Based on struct vm86_regs definition in sys/vm86.h */
#define REGS_EBX      0
#define REGS_ECX      4
#define REGS_EDX      8
#define REGS_ESI      12
#define REGS_EDI      16
#define REGS_EBP      20
#define REGS_EAX      24
/* __null_ds, __null_es, __null_fs, __null_gs skipped */
/* orig_eax skipped */
#define REGS_EIP      48
#define REGS_CS       52
#define REGS_EFLAGS   56
#define REGS_ESP      60
#define REGS_SS       64
#define REGS_ES       68
#define REGS_DS       72
#define REGS_FS       76
#define REGS_GS       80

/*
 * void vm86_enter(struct vm86_struct *info)
 *
 * Enters VM86 mode using the provided context.
 * Does not return until vm86_bios_ret_point is reached (via GPF handler).
 *
 * Stack Frame Layout:
 * [EBP] -> Saved EBP
 * [EBP+4] -> Return Address
 * [EBP+8] -> info pointer
 */
vm86_enter:
    /* Standard Prologue */
    push %ebp
    mov %esp, %ebp

    /* Save callee-saved registers */
    push %ebx
    push %esi
    push %edi

    /* Get info pointer into EAX */
    /* We use EAX as base because we can load final EAX value last */
    mov 8(%ebp), %eax

    /* Disable interrupts while building the IRET frame */
    cli

    /* Build VM86 IRET Frame on Kernel Stack */
    /* Pushed Order: GS, FS, DS, ES, SS, ESP, EFLAGS, CS, EIP */

    /* Push GS */
    /* Note: struct fields are 16-bit but we push 32-bit values (padded) */
    /* The struct uses 'unsigned short' but with padding or implicit access? */
    /* sys/vm86.h: unsigned short gs, __gsh; -> 4 bytes total */
    /* So we can load 32 bits. */
    pushl REGS_GS(%eax)

    /* Push FS */
    pushl REGS_FS(%eax)

    /* Push DS */
    pushl REGS_DS(%eax)

    /* Push ES */
    pushl REGS_ES(%eax)

    /* Push SS */
    pushl REGS_SS(%eax)

    /* Push ESP */
    pushl REGS_ESP(%eax)

    /* Push EFLAGS */
    /* We must ensure VM bit (17) and IF bit (9) are set */
    movl REGS_EFLAGS(%eax), %edx  /* Use EDX as temp (clobberable) */
    orl $0x20200, %edx            /* Set VM | IF */
    pushl %edx

    /* Push CS */
    pushl REGS_CS(%eax)

    /* Push EIP */
    pushl REGS_EIP(%eax)

    /* Load General Purpose Registers from info structure */
    /* We can overwrite everything except EAX (base ptr) and ESP (stack) */

    movl REGS_EBX(%eax), %ebx
    movl REGS_ECX(%eax), %ecx
    movl REGS_EDX(%eax), %edx
    movl REGS_ESI(%eax), %esi
    movl REGS_EDI(%eax), %edi
    movl REGS_EBP(%eax), %ebp

    /* Finally load EAX */
    movl REGS_EAX(%eax), %eax

    /* Enter VM86 Mode */
    /* IRET pops EIP, CS, EFLAGS, ESP, SS, ES, DS, FS, GS */
    iret

/*
 * void vm86_bios_ret_point(void)
 *
 * Trampoline target for returning from VM86 mode to Kernel mode.
 * The VM86 GPF handler redirects execution here when exiting.
 *
 * Current Stack State (after handler return and IRET):
 * [ESP] points to remaining VM86 IRET frame:
 * SS, ES, DS, FS, GS (Wait, correct?)
 */
vm86_bios_ret_point:
    /*
     * Analysis of Stack State:
     * 1. VM86 Exception pushed: GS, FS, DS, ES, SS, ESP, EFLAGS, CS, EIP.
     * 2. GPF Handler modified Stack:
     *    - CS -> Kernel CS (0x08)
     *    - EIP -> vm86_bios_ret_point
     *    - EFLAGS -> VM bit cleared
     * 3. IRET executed:
     *    - Popped EIP, CS, EFLAGS.
     *    - Because VM=0 and CPL=0, it stopped there.
     * 4. Remaining on Stack (at ESP):
     *    - ESP, SS, ES, DS, FS, GS (from VM86 frame).
     *    - Wait, Push Order was: GS, FS, DS, ES, SS, ESP, EFLAGS, CS, EIP.
     *    - Pop Order: EIP, CS, EFLAGS.
     *    - Next on stack: ESP.
     *    - Then: SS, ES, DS, FS, GS.
     *    - Total 6 registers * 4 bytes = 24 bytes.
     */

    /* Clean up VM86 leftovers */
    add $24, %esp

    /* Now Stack is back to state before 'iret' in vm86_enter */
    /* [Saved EDI] [Saved ESI] [Saved EBX] [Saved EBP] [RetAddr] ... */

    /* Restore callee-saved registers */
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp

    /* Return to caller of vm86_enter */
    ret
