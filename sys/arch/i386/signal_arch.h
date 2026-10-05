/*
 * sys/arch/i386/signal_arch.h - i386 Signal Context Structures
 */

#ifndef _ARCH_I386_SIGNAL_ARCH_H
#define _ARCH_I386_SIGNAL_ARCH_H

#include <stdint.h>
#include <sys/signal.h>
#include <sys/compat32.h>  /* stack32_t, siginfo32_t: the process's layouts */

/*
 * Signal trampoline address - mapped by kernel at a fixed location.
 * Contains code to invoke sys_sigreturn after signal handler returns.
 *
 * The trampoline lives OUTSIDE the recursive PDE range (PDE 1023 =
 * 0xFFC00000-0xFFFFFFFF).  Mapping any user-accessible page in that
 * region is impossible: pmap_enter for that range writes to a PDE
 * slot rather than a PTE (and corrupts the page directory), and the
 * recursive PDE itself has no USER bit so the underlying page would
 * fault on any ring-3 access regardless.  0xFE000000 (PDE 1016) is
 * unused kernel virtual space; pmap_enter allocates a fresh page
 * table there with the USER bit set on the new PDE, and the trampoline
 * PTE inherits VM_PROT_USER cleanly.
 */
#define SIG_TRAMPOLINE_ADDR     0xFE000000
#define RT_SIG_TRAMPOLINE_ADDR  0xFE000010  /* For SA_SIGINFO handlers -> rt_sigreturn */
#define NBSD_SIG_TRAMPOLINE_ADDR 0xFE000020 /* NetBSD: loads EBX=scp before sigreturn */
#define FBSD_SIG_TRAMPOLINE_ADDR 0xFE000030 /* FreeBSD: loads EBX=scp before sigreturn */
/*
 * Linux passes nothing to sigreturn on the stack: the kernel recovers the
 * frame from ESP alone (frame = ESP - 8 legacy, ESP - 4 for rt).  These
 * slots therefore must NOT push an argument the way the native slots above
 * do -- a pushed argument shifts ESP and the kernel then reads the
 * sigcontext from below the frame.
 */
#define LINUX_SIG_TRAMPOLINE_ADDR    0xFE000040 /* Linux: popl %eax, then sigreturn */
#define LINUX_RT_SIG_TRAMPOLINE_ADDR 0xFE000050 /* Linux: rt_sigreturn, no pop */
#define AMD64_SIG_TRAMPOLINE_ADDR    0xFE000060 /* native 64-bit processes */

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
 * FPU state in a signal frame.
 *
 * The image is what FXSAVE writes (512 bytes), or FNSAVE (the first 108)
 * on a CPU without it; fp_format says which, in the values of
 * FPU_SIG_* (machine/fpu.h), as does mc_fpformat for the image in a
 * ucontext's mc_fpstate.
 *
 * With XSAVE there is more state than FXSAVE's image holds -- the XSAVE
 * header and the extended components, so the upper halves of the YMM
 * registers.  That goes in a block higher on the stack, and the image
 * says where: FXSAVE leaves its last 48 bytes to software, and the kernel
 * keeps a struct sig_fpx there, at SIG_FPX_OFFSET.
 */
#define SIG_FP_IMAGE_SIZE   512
#define SIG_FPX_OFFSET      464
#define SIG_FPX_MAGIC       0x58534653u     /* "SFSX" */
#define SIGFRAME_FP_MAGIC   0x50464653u     /* "SFFP" */

struct sig_fpx {
    uint32_t magic;         /* SIG_FPX_MAGIC */
    uint32_t addr;          /* the extended block */
    uint32_t len;           /* its size */
};

struct sigframe_fp {
    uint32_t fp_magic;      /* SIGFRAME_FP_MAGIC */
    uint32_t fp_format;     /* FPU_SIG_* */
    uint32_t fp_pad[2];
    uint8_t  fp_image[SIG_FP_IMAGE_SIZE];
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

    /* The FPU state, which struct sigcontext has no room for.  It follows
     * the sigcontext so that sigreturn(scp) finds it; a sigcontext that
     * did not come from sendsig() has no SIGFRAME_FP_MAGIC behind it and
     * restores no FPU state. */
    struct sigframe_fp fp;
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
    uptr32_t         uc_link;       /* struct ucontext *: context resumed when this returns */
    stack32_t        uc_stack;      /* Stack used by this context */
    mcontext_t       uc_mcontext;   /* Machine-specific context */
    uint32_t         uc_sigmask;    /* Signal mask */
} ucontext_t;
ABI32_ASSERT_SIZE(ucontext_t, 620);

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
    siginfo32_t info;           /* siginfo_t, as the process sees it */
    ucontext_t  uc;             /* ucontext_t structure */
};
ABI32_ASSERT_SIZE(struct siginfo_frame, 768);

/* Fill a siginfo for signal `sig` raised with `code` on the current thread. */
void populate_siginfo(siginfo_t *info, int sig, int code);

#ifdef SUBSTRATE_ARCH_X86_64
/* Signal delivery to, and sigreturn from, a native 64-bit process
 * (arch/x86_64/signal64.c).  `regs` is the registers_t to redirect. */
void sendsig_amd64(void *handler, int sig, uint32_t mask, uint32_t flags,
                   void *regs);
int amd64_sys_sigreturn(void *ucp);
#endif

extern unsigned char sig_trampoline_code[];
extern unsigned int sig_trampoline_size;

#endif /* _ARCH_I386_SIGNAL_ARCH_H */
