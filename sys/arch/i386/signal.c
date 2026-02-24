/*
 * sys/arch/i386/signal.c - Architecture-specific signal handling
 *
 * This file implements the i386-specific signal delivery mechanism.
 * It constructs signal frames on the user stack and sets up the
 * register state for signal handler execution.
 *
 * Signal Frame Layout (growing down):
 *   +---------------------+
 *   | struct sigcontext   |  <- saved register state
 *   +---------------------+
 *   | int sig             |  <- signal number (arg 1 to handler)
 *   +---------------------+
 *   | void *retaddr       |  <- return address (trampoline) at ESP
 *   +---------------------+
 *
 * For SA_SIGINFO handlers:
 *   +---------------------+
 *   | struct ucontext     |  <- full machine context
 *   +---------------------+
 *   | siginfo_t           |  <- detailed signal info
 *   +---------------------+
 *   | void *ucontext_ptr  |  <- arg 3: pointer to ucontext
 *   +---------------------+
 *   | siginfo_t *info_ptr |  <- arg 2: pointer to siginfo
 *   +---------------------+
 *   | int sig             |  <- arg 1: signal number
 *   +---------------------+
 *   | void *retaddr       |  <- return address (trampoline)
 *   +---------------------+
 */

#include <sys/signal.h>
#include <sys/proc.h>
#include <kern/console.h>
#include <arch/i386/idt.h>
#include <arch/i386/include/signal_arch.h>
#include <string.h>


/*
 * Signal trampoline address - mapped by kernel at a fixed location.
 * Contains code to invoke sys_sigreturn after signal handler returns.
 */
#define SIG_TRAMPOLINE_ADDR     0xFFFF1000
#define RT_SIG_TRAMPOLINE_ADDR  0xFFFF1010  /* For SA_SIGINFO handlers -> rt_sigreturn */

/*
 * populate_siginfo - Fill in siginfo_t structure
 *
 * Populates the siginfo_t with signal details based on signal source.
 */
static void populate_siginfo(siginfo_t *info, int sig, int code) {
    memset(info, 0, sizeof(*info));
    info->si_signo = sig;
    info->si_errno = 0;
    info->si_code = code;
    
    /* Set source-specific fields */
    if (current_process) {
        info->si_pid = current_process->pid;
        info->si_uid = current_process->uid;
    }
    
    /* For fault signals, si_addr would be set by the trap handler */
    info->si_addr = NULL;
    info->si_status = 0;
}

/*
 * populate_ucontext - Fill in ucontext_t structure with machine context
 *
 * Populates the ucontext with the full machine state for context manipulation.
 */
static void populate_ucontext(ucontext_t *uc, uint32_t mask, registers_t *regs) {
    memset(uc, 0, sizeof(*uc));
    
    uc->uc_flags = 0;
    uc->uc_link = NULL;
    
    /* Copy alt stack info if configured */
    if (current_thread) {
        uc->uc_stack = current_thread->sig_alt_stack;
    }
    
    /* Signal mask */
    uc->uc_sigmask = mask;
    
    /* Machine context */
    mcontext_t *mc = &uc->uc_mcontext;
    
    /* Segment registers */
    mc->mc_gs = regs->gs;
    mc->mc_fs = regs->fs;
    mc->mc_es = regs->es;
    mc->mc_ds = regs->ds;
    
    /* General purpose registers */
    mc->mc_edi = regs->edi;
    mc->mc_esi = regs->esi;
    mc->mc_ebp = regs->ebp;
    mc->mc_isp = regs->esp;  /* Interrupt stack pointer (from pusha) */
    mc->mc_ebx = regs->ebx;
    mc->mc_edx = regs->edx;
    mc->mc_ecx = regs->ecx;
    mc->mc_eax = regs->eax;
    
    /* Trap info */
    mc->mc_trapno = regs->int_no;
    mc->mc_err = regs->err_code;
    
    /* Control registers */
    mc->mc_eip = regs->eip;
    mc->mc_cs = regs->cs;
    mc->mc_eflags = regs->eflags;
    mc->mc_esp = regs->useresp;
    mc->mc_ss = regs->ss;
    
    /* FPU state - not saved currently */
    mc->mc_fpformat = 0;  /* 0 = no FPU state */
    mc->mc_ownedfp = 0;
}

/*
 * sendsig - Prepare user stack for signal handler execution
 *
 * This function constructs a signal frame on the user stack and modifies
 * the register state so that when the kernel returns to user mode, it
 * will begin executing the signal handler.
 *
 * The signal handler's return address points to the trampoline, which
 * will invoke sys_sigreturn to restore the original context.
 *
 * Parameters:
 *   handler - Signal handler function pointer
 *   sig     - Signal number
 *   mask    - Signal mask to restore after handler returns
 *   flags   - SA_* flags from sigaction
 *   regs    - Saved CPU registers from interrupted context
 */
void sendsig(sig_t handler, int sig, uint32_t mask, uint32_t flags, registers_t *regs) {
    if (!regs) {
        kprint("sendsig: NULL regs pointer\n");
        return;
    }
    
    if (!current_thread) {
        kprint("sendsig: No current thread\n");
        return;
    }
    
    uint32_t esp;
    
    /*
     * Step 1: Calculate user stack pointer
     *
     * The stack pointer comes from the interrupted user context.
     * We need to check for alternate signal stack usage.
     */
    if ((flags & SA_ONSTACK) && 
        (current_thread->sig_alt_stack.ss_flags & SS_DISABLE) == 0 &&
        !current_thread->sig_on_stack) {
        /*
         * Use alternate signal stack:
         * Stack starts at ss_sp + ss_size (stack grows down)
         */
        esp = (uint32_t)(uintptr_t)current_thread->sig_alt_stack.ss_sp +
              (uint32_t)current_thread->sig_alt_stack.ss_size;
        current_thread->sig_on_stack = 1;
    } else {
        /* Use current user stack */
        esp = regs->useresp;
    }
    
    /*
     * Handle SA_SIGINFO - extended frame with siginfo_t and ucontext
     */
    if (flags & SA_SIGINFO) {
        struct siginfo_frame sif;
        
        /* Reserve space for siginfo frame */
        esp -= sizeof(struct siginfo_frame);
        
        /* Align stack to 16-byte boundary */
        esp &= ~0xFUL;
        
        /* Validate stack address */
        if (validate_user_addr((void*)(uintptr_t)esp, sizeof(struct siginfo_frame)) != 0) {
            kprint("sendsig: SA_SIGINFO stack address out of bounds\n");
            extern void sigexit(process_t *p, int sig);
            sigexit(current_process, SIGSEGV);
            return;
        }
        
        /* Build siginfo_frame */
        sif.retaddr = RT_SIG_TRAMPOLINE_ADDR;  /* Return to rt_sigreturn trampoline */
        sif.sig = sig;
        
        /* Pointers to embedded structures (offsets from esp) */
        sif.info_ptr = esp + offsetof(struct siginfo_frame, info);
        sif.ucontext_ptr = esp + offsetof(struct siginfo_frame, uc);
        
        /* Populate the siginfo_t structure */
        populate_siginfo(&sif.info, sig, 0);  /* code = 0 for now, trap handler sets specific codes */
        
        /* Populate the ucontext_t structure */
        populate_ucontext(&sif.uc, mask, regs);
        
        /* Copy siginfo_frame to user stack */
        if (copyout(&sif, (void*)(uintptr_t)esp, sizeof(sif)) != 0) {
            kprint("sendsig: Failed to copy siginfo frame to user stack\n");
            extern void sigexit(process_t *p, int sig);
            sigexit(current_process, SIGSEGV);
            return;
        }
        
        /* Modify saved registers for handler execution */
        regs->useresp = esp;
        regs->eip = (uint32_t)handler;
        regs->eflags &= ~(1 << 10);  /* Clear DF */
        
        return;
    }
    
    /*
     * Legacy signal frame path (non-SA_SIGINFO)
     */
    struct sigframe sf;
    
    /* Reserve space for signal frame */
    esp -= sizeof(struct sigframe);
    
    /* Align stack to 16-byte boundary */
    esp &= ~0xFUL;
    
    /* Validate stack address */
    if (validate_user_addr((void*)(uintptr_t)esp, sizeof(struct sigframe)) != 0) {
        kprint("sendsig: Stack address 0x");
        char hex[9];
        for (int i = 7; i >= 0; i--) {
            int nibble = (esp >> (i * 4)) & 0xF;
            hex[7-i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        }
        hex[8] = '\0';
        kprint(hex);
        kprint(" out of user space bounds\n");
        extern void sigexit(process_t *p, int sig);
        sigexit(current_process, SIGSEGV);
        return;
    }
    
    /*
     * Build the sigcontext - save all registers
     */
    struct sigcontext *scp = &sf.sc;
    
    /* Segment registers */
    /* Segment registers */
    scp->gs = regs->gs;
    scp->fs = regs->fs;
    scp->es = regs->es;
    scp->ds = regs->ds;
    
    /* General purpose registers (from pusha) */
    scp->edi = regs->edi;
    scp->esi = regs->esi;
    scp->ebp = regs->ebp;
    scp->esp = regs->esp;  /* ESP from pusha, usually ignored */
    scp->ebx = regs->ebx;
    scp->edx = regs->edx;
    scp->ecx = regs->ecx;
    scp->eax = regs->eax;
    
    /* Trap information */
    scp->trapno = regs->int_no;
    scp->err = regs->err_code;
    
    /* Control registers (pushed by CPU on interrupt) */
    scp->eip = regs->eip;
    scp->cs = regs->cs;
    scp->eflags = regs->eflags;
    
    /* User stack state */
    scp->user_esp = regs->useresp;
    scp->user_ss = regs->ss;
    
    /* Save signal mask for restore in sigreturn */
    scp->oldmask = mask;
    
    /*
     * Build sigframe arguments
     */
    sf.retaddr = SIG_TRAMPOLINE_ADDR;  /* Return to trampoline */
    sf.sig = sig;                       /* Signal number (handler arg 1) */
    
    /*
     * Copy frame to user stack
     */
    if (copyout(&sf, (void*)(uintptr_t)esp, sizeof(sf)) != 0) {
        kprint("sendsig: Failed to copy signal frame to user stack\n");
        extern void sigexit(process_t *p, int sig);
        sigexit(current_process, SIGSEGV);
        return;
    }
    
    /*
     * Modify saved registers for handler execution
     *
     * When the kernel returns to user mode (via iret), it will:
     * - Pop EIP from stack (now set to handler address)
     * - Pop CS (unchanged, still user code segment)
     * - Pop EFLAGS (unchanged, preserving interrupt state)
     * - Pop ESP from stack (now points to our signal frame)
     * - Pop SS (unchanged, still user stack segment)
     */
    regs->useresp = esp;            /* Stack points to signal frame */
    regs->eip = (uint32_t)handler;  /* Execute signal handler */
    
    /*
     * Clear direction flag for handler (per ABI)
     * EFLAGS bit 10 is DF
     */
    regs->eflags &= ~(1 << 10);
}

/*
 * sys_sigreturn - Restore context from signal frame
 *
 * Called via trampoline after signal handler returns.
 * Restores all registers from the sigcontext on the user stack
 * and resumes execution at the originally interrupted location.
 *
 * Security checks:
 * - Validates sigcontext pointer is in user space
 * - Verifies segment registers have user-mode RPL (ring 3)
 * - Masks sensitive EFLAGS bits to prevent privilege escalation
 *
 * Returns:
 *   This function should not return - it modifies the saved register
 *   state and the subsequent iret returns to the original context.
 *   On error, returns -1 (EFAULT).
 */
int sys_sigreturn(void *scp_ptr) {
    struct sigcontext *scp = (struct sigcontext *)scp_ptr;
    if (!scp) {
        return -1;  /* EINVAL */
    }
    
    if (!current_thread || !current_thread->syscall_regs) {
        return -1;  /* Internal error */
    }
    registers_t *syscall_regs = (registers_t *)current_thread->syscall_regs;
    
    /*
     * Copy sigcontext from user space to kernel buffer
     */
    struct sigcontext sc;
    if (copyin(scp, &sc, sizeof(sc)) != 0) {
        return -1;  /* EFAULT */
    }
    
    /*
     * Security: Validate segment registers have user-mode RPL
     *
     * The Request Privilege Level (RPL) is in bits 0-1 of segment selectors.
     * User mode requires RPL = 3. If attacker tries to set RPL = 0,
     * they could gain kernel privileges.
     */
    if ((sc.cs & 3) != 3) {
        kprint("sys_sigreturn: Invalid CS RPL\n");
        return -1;  /* EPERM */
    }
    if ((sc.user_ss & 3) != 3) {
        kprint("sys_sigreturn: Invalid SS RPL\n");
        return -1;  /* EPERM */
    }
    
    /*
     * Restore general-purpose registers
     */
    syscall_regs->gs = sc.gs;
    syscall_regs->fs = sc.fs;
    syscall_regs->es = sc.es;
    syscall_regs->ds = sc.ds;
    syscall_regs->edi = sc.edi;
    syscall_regs->esi = sc.esi;
    syscall_regs->ebp = sc.ebp;
    /* esp from pusha ignored - useresp used instead */
    syscall_regs->ebx = sc.ebx;
    syscall_regs->edx = sc.edx;
    syscall_regs->ecx = sc.ecx;
    syscall_regs->eax = sc.eax;
    
    /*
     * Restore control registers
     */
    syscall_regs->eip = sc.eip;
    syscall_regs->cs = sc.cs;
    
    /*
     * Restore EFLAGS with security filtering
     *
     * Sensitive bits that must not be changed by user:
     * - IOPL (bits 12-13): I/O privilege level
     * - VM (bit 17): Virtual 8086 mode
     * - RF (bit 16): Resume flag
     * - NT (bit 14): Nested task
     *
     * We preserve the kernel's values for these bits and only
     * restore the user-controllable bits from the saved context.
     */
    #define EFLAGS_KERNEL_MASK  0x00033200  /* IOPL, VM, RF, NT */
    #define EFLAGS_USER_MASK    0xFFCCCDFF  /* User-modifiable flags */
    
    uint32_t safe_eflags = (syscall_regs->eflags & EFLAGS_KERNEL_MASK) |
                           (sc.eflags & EFLAGS_USER_MASK);
    syscall_regs->eflags = safe_eflags;
    
    /*
     * Restore user stack
     */
    syscall_regs->useresp = sc.user_esp;
    syscall_regs->ss = sc.user_ss;
    
    /*
     * Restore signal mask
     */
    if (current_thread) {
        current_thread->sig_mask = sc.oldmask;
        
        /* Clear sig_on_stack flag - we're returning from handler */
        current_thread->sig_on_stack = 0;
    }
    
    /*
     * Return value is EAX from saved context, not a syscall return
     *
     * The calling convention expects sys_sigreturn to return normally,
     * and the iret will use the restored register values.
     */
    return sc.eax;
}

/*
 * sys_rt_sigreturn - Restore context from SA_SIGINFO frame
 *
 * Called via RT trampoline after SA_SIGINFO signal handler returns.
 * Restores the full machine context from the ucontext_t on the user stack.
 *
 * The ucontext_t pointer is passed on the stack by the trampoline.
 *
 * Security checks:
 * - Validates ucontext pointer is in user space
 * - Verifies segment registers have user-mode RPL (ring 3)
 * - Masks sensitive EFLAGS bits to prevent privilege escalation
 *
 * Returns:
 *   This function should not return normally - it modifies the saved
 *   register state and the subsequent iret returns to the original context.
 *   On error, returns -1 (EFAULT).
 */
int sys_rt_sigreturn(void *ucp_ptr) {
    ucontext_t *ucp = (ucontext_t *)ucp_ptr;
    if (!ucp) {
        return -1;  /* EINVAL */
    }
    
    if (!current_thread || !current_thread->syscall_regs) {
        return -1;  /* Internal error */
    }
    registers_t *syscall_regs = (registers_t *)current_thread->syscall_regs;
    
    /*
     * Copy ucontext from user space to kernel buffer
     */
    ucontext_t uc;
    if (copyin(ucp, &uc, sizeof(uc)) != 0) {
        return -1;  /* EFAULT */
    }
    
    mcontext_t *mc = &uc.uc_mcontext;
    
    /*
     * Security: Validate segment registers have user-mode RPL
     */
    if ((mc->mc_cs & 3) != 3) {
        kprint("sys_rt_sigreturn: Invalid CS RPL\n");
        return -1;  /* EPERM */
    }
    if ((mc->mc_ss & 3) != 3) {
        kprint("sys_rt_sigreturn: Invalid SS RPL\n");
        return -1;  /* EPERM */
    }
    
    /*
     * Restore segment registers
     */
    syscall_regs->gs = mc->mc_gs;
    syscall_regs->fs = mc->mc_fs;
    syscall_regs->es = mc->mc_es;
    syscall_regs->ds = mc->mc_ds;
    
    /*
     * Restore general-purpose registers
     */
    syscall_regs->edi = mc->mc_edi;
    syscall_regs->esi = mc->mc_esi;
    syscall_regs->ebp = mc->mc_ebp;
    /* mc_isp (interrupt stack pointer from pusha) ignored */
    syscall_regs->ebx = mc->mc_ebx;
    syscall_regs->edx = mc->mc_edx;
    syscall_regs->ecx = mc->mc_ecx;
    syscall_regs->eax = mc->mc_eax;
    
    /*
     * Restore control registers
     */
    syscall_regs->eip = mc->mc_eip;
    syscall_regs->cs = mc->mc_cs;
    
    /*
     * Restore EFLAGS with security filtering
     */
    #define RT_EFLAGS_KERNEL_MASK  0x00033200  /* IOPL, VM, RF, NT */
    #define RT_EFLAGS_USER_MASK    0xFFCCCDFF  /* User-modifiable flags */
    
    uint32_t safe_eflags = (syscall_regs->eflags & RT_EFLAGS_KERNEL_MASK) |
                           (mc->mc_eflags & RT_EFLAGS_USER_MASK);
    syscall_regs->eflags = safe_eflags;
    
    /*
     * Restore user stack
     */
    syscall_regs->useresp = mc->mc_esp;
    syscall_regs->ss = mc->mc_ss;
    
    /*
     * Restore signal mask from ucontext
     */
    if (current_thread) {
        current_thread->sig_mask = uc.uc_sigmask;
        
        /* Clear sig_on_stack flag - we're returning from handler */
        current_thread->sig_on_stack = 0;
    }
    
    /*
     * Return value is EAX from saved context
     */
    return mc->mc_eax;
}
