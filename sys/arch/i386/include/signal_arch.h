/*
 * sys/arch/i386/include/signal_arch.h - i386 Signal Context Structures
 */

#ifndef _ARCH_I386_SIGNAL_ARCH_H
#define _ARCH_I386_SIGNAL_ARCH_H

#include <stdint.h>
#include <sys/signal.h>  /* For stack_t, siginfo_t */

/*
 * Signal Context (sigcontext)
 * 
 * This structure is pushed onto the user stack during signal delivery.
 * It contains the saved state of the interrupted user thread.
 * When the signal handler returns, sys_sigreturn uses this to restore state.
 */
struct sigcontext {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;       // Spurious, ignored by popad
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t trapno;
    uint32_t err;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t user_esp;
    uint32_t user_ss;
    uint32_t oldmask;   // Saved signal mask (pre-handler)
};

/*
 * Signal Frame (sigframe)
 * 
 * The actual stack layout seen by the signal handler.
 * 
 * Stack grows down:
 * [ ... higher addresses ... ]
 * [ struct sigcontext sc   ]  <- Saved context
 * [ int sig                ]  <- Argument 1: Signal number
 * [ void *return_addr      ]  <- Return address (trampoline)
 * [ ... lower addresses ... ]
 * 
 * Note: Check ABI for argument passing (stack vs regs). i386 System V passes args on stack.
 */
struct sigframe {
    uint32_t retaddr;       // Return address (trampoline)
    int      sig;           // Signal number (Argument 1)
    // struct sigcontext sc; // Context is usually passed by pointer or sits above args
    // To match typical BSD/Linux:
    // Handler(sig, code, scp) -> sigcontext is on stack, pointer passed as 3rd arg
    // But for simple handlers: void handler(int sig)
    
    // We will place sigcontext strictly AFTER the arguments for sigreturn to find it easily?
    // Actually, usually sigreturn takes a pointer, or we interpret stack pointer.
    
    // Simpler layout:
    // [ sigcontext ]
    // [ args       ]
    // [ retaddr    ]
    
    // Let's use:
    // [ sigcontext  ] (at esp + offset)
    // [ sig         ] (at esp + 4)
    // [ retaddr     ] (at esp) -> points to trampoline
    
    struct sigcontext sc;
};

/*
 * Machine Context (mcontext_t)
 *
 * Architecture-specific machine state for ucontext_t.
 * Contains all general-purpose and control registers.
 */
typedef struct mcontext {
    uint32_t mc_gs;
    uint32_t mc_fs;
    uint32_t mc_es;
    uint32_t mc_ds;
    uint32_t mc_edi;
    uint32_t mc_esi;
    uint32_t mc_ebp;
    uint32_t mc_isp;        /* Interrupt stack pointer (not used by user) */
    uint32_t mc_ebx;
    uint32_t mc_edx;
    uint32_t mc_ecx;
    uint32_t mc_eax;
    uint32_t mc_trapno;
    uint32_t mc_err;
    uint32_t mc_eip;
    uint32_t mc_cs;
    uint32_t mc_eflags;
    uint32_t mc_esp;
    uint32_t mc_ss;
    /* FPU state would go here in a full implementation */
    uint32_t mc_fpformat;   /* FPU state format: 0 = none, 1 = fnsave, 2 = fxsave */
    uint32_t mc_ownedfp;    /* FPU ownership flags */
    uint32_t mc_fpstate[128]; /* Placeholder for FPU state (512 bytes for fxsave) */
} mcontext_t;

/*
 * User Context (ucontext_t)
 *
 * Full user context including signal mask and machine state.
 * Used for SA_SIGINFO signal handlers and getcontext/setcontext.
 */
typedef struct ucontext {
    uint32_t         uc_flags;
    struct ucontext *uc_link;       /* Pointer to context resumed when this returns */
    stack_t          uc_stack;      /* Stack used by this context */
    mcontext_t       uc_mcontext;   /* Machine-specific context */
    uint32_t         uc_sigmask;    /* Signal mask */
} ucontext_t;

/*
 * SA_SIGINFO Signal Frame (siginfo_frame)
 *
 * Extended frame for SA_SIGINFO handlers:
 *   void handler(int sig, siginfo_t *info, void *ucontext);
 *
 * Stack layout (growing down):
 * [ ... higher addresses ... ]
 * [ ucontext_t              ]  <- Full machine context
 * [ siginfo_t               ]  <- Detailed signal info
 * [ void *ucontext_ptr      ]  <- Arg 3: pointer to ucontext
 * [ siginfo_t *info_ptr     ]  <- Arg 2: pointer to siginfo
 * [ int sig                 ]  <- Arg 1: signal number
 * [ void *retaddr           ]  <- Return address (trampoline)
 * [ ... lower addresses ... ]
 */
struct siginfo_frame {
    uint32_t    retaddr;        /* Return address (rt_sigreturn trampoline) */
    int         sig;            /* Argument 1: Signal number */
    uint32_t    info_ptr;       /* Argument 2: Pointer to siginfo_t */
    uint32_t    ucontext_ptr;   /* Argument 3: Pointer to ucontext_t */
    siginfo_t   info;           /* siginfo_t structure */
    ucontext_t  uc;             /* ucontext_t structure */
};

#endif /* _ARCH_I386_SIGNAL_ARCH_H */
