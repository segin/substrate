#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/kern_syscalls.h>
#include <sys/syscall_impl.h>
#include <exec/perso/netbsd/netbsd_user.h>
#include <arch/i386/idt.h>
#include <arch/i386/signal_arch.h>
#include <string.h>

extern int copyout(const void *src, void *dst, size_t size);
extern int copyin(const void *src, void *dst, size_t size);
extern int validate_user_addr(const void *addr, size_t size);

/*
 * NetBSD <-> substrate-native signal-number translation.
 *
 * NetBSD uses the historical 4.3BSD signal numbering (SIGCHLD=20,
 * SIGUSR1=30, SIGBUS=10, SIGSYS=12, ...); substrate's native numbering is
 * Linux-ish (SIGCHLD=17, SIGUSR1=10, SIGBUS=7, where 20=SIGTSTP).  The
 * kernel signal machinery -- pending sets, blocked masks, psignal(), the
 * SIGCHLD raised on child exit -- all speak NATIVE numbers, so every
 * NetBSD signal syscall argument (and the number/mask handed to a signal
 * handler) must be translated at the personality boundary.  Without this,
 * a NetBSD program's sigaction(SIGCHLD) installs on the native SIGTSTP
 * slot, its sigsuspend() mask blocks the wrong bit, and the kernel's
 * native SIGCHLD is never caught -- e.g. ksh job control blocks forever in
 * sigsuspend() after the first external command and the prompt never
 * returns.
 *
 * Tables are indexed by signal number (1..31); 0 and out-of-range fall
 * back to identity.  A couple of NetBSD signals have no native equivalent
 * (EMT, XCPU, XFSZ, INFO) and map to the nearest native slot or stay put;
 * they are effectively never raised.
 */
static const unsigned char nbsd2nat_signo[32] = {
    [1] = SIGHUP,  [2] = SIGINT,  [3] = SIGQUIT, [4] = SIGILL,
    [5] = SIGTRAP, [6] = SIGABRT,
    [7] = SIGBUS,                 /* NetBSD SIGEMT -> nearest native trap */
    [8] = SIGFPE,  [9] = SIGKILL,
    [10] = SIGBUS,                /* NetBSD SIGBUS=10  */
    [11] = SIGSEGV,
    [12] = SIGSYS,                /* NetBSD SIGSYS=12 -> native 31 */
    [13] = SIGPIPE, [14] = SIGALRM, [15] = SIGTERM, [16] = SIGURG,
    [17] = SIGSTOP,               /* NetBSD SIGSTOP=17 -> native 19 */
    [18] = SIGTSTP,               /* NetBSD SIGTSTP=18 -> native 20 */
    [19] = SIGCONT,               /* NetBSD SIGCONT=19 -> native 18 */
    [20] = SIGCHLD,               /* NetBSD SIGCHLD=20 -> native 17 */
    [21] = SIGTTIN, [22] = SIGTTOU,
    [23] = SIGIO,                 /* NetBSD SIGIO=23 (SIGPOLL) */
    [24] = 24, [25] = 25,         /* XCPU / XFSZ -> unused native slots */
    [26] = SIGVTALRM, [27] = SIGPROF, [28] = SIGWINCH,
    [29] = 29,                    /* SIGINFO -> unused native slot */
    [30] = SIGUSR1,               /* NetBSD SIGUSR1=30 -> native 10 */
    [31] = SIGUSR2,               /* NetBSD SIGUSR2=31 -> native 12 */
};

static const unsigned char nat2nbsd_signo[32] = {
    [SIGHUP] = 1,  [SIGINT] = 2,  [SIGQUIT] = 3, [SIGILL] = 4,
    [SIGTRAP] = 5, [SIGABRT] = 6,
    [SIGBUS] = 10,                /* native SIGBUS=7 -> NetBSD 10 */
    [SIGFPE] = 8,  [SIGKILL] = 9,
    [SIGUSR1] = 30,               /* native SIGUSR1=10 -> NetBSD 30 */
    [SIGSEGV] = 11,
    [SIGUSR2] = 31,               /* native SIGUSR2=12 -> NetBSD 31 */
    [SIGPIPE] = 13, [SIGALRM] = 14, [SIGTERM] = 15, [SIGURG] = 16,
    [SIGCHLD] = 20,               /* native SIGCHLD=17 -> NetBSD 20 */
    [SIGCONT] = 19,               /* native SIGCONT=18 -> NetBSD 19 */
    [SIGSTOP] = 17,               /* native SIGSTOP=19 -> NetBSD 17 */
    [SIGTSTP] = 18,               /* native SIGTSTP=20 -> NetBSD 18 */
    [SIGTTIN] = 21, [SIGTTOU] = 22,
    [SIGIO] = 23,
    [24] = 24, [25] = 25,
    [SIGVTALRM] = 26, [SIGPROF] = 27, [SIGWINCH] = 28,
    [29] = 29, [30] = 30,
    [SIGSYS] = 12,                /* native SIGSYS=31 -> NetBSD 12 */
};

int netbsd_to_native_signo(int sig) {
    if (sig <= 0 || sig >= 32) return sig;
    int n = nbsd2nat_signo[sig];
    return n ? n : sig;
}

int native_to_netbsd_signo(int sig) {
    if (sig <= 0 || sig >= 32) return sig;
    int n = nat2nbsd_signo[sig];
    return n ? n : sig;
}

/* Remap a signal bitmask (bit (signo-1) set) between the two numbering
 * schemes.  NetBSD's sigset_t is 128-bit, but signals 1..32 live in the
 * first 32-bit word, which is all substrate supports. */
uint32_t netbsd_to_native_sigmask(uint32_t m) {
    uint32_t out = 0;
    for (int s = 1; s <= 31; s++)
        if (m & (1u << (s - 1)))
            out |= 1u << (netbsd_to_native_signo(s) - 1);
    return out;
}

uint32_t native_to_netbsd_sigmask(uint32_t m) {
    uint32_t out = 0;
    for (int s = 1; s <= 31; s++)
        if (m & (1u << (s - 1)))
            out |= 1u << (native_to_netbsd_signo(s) - 1);
    return out;
}

/* NetBSD/4.4BSD sa_flags bit values (sys/signal.h) -- they differ from
 * substrate's native SA_* layout, so the flags word must be remapped too. */
#define NBSD_SA_ONSTACK   0x0001
#define NBSD_SA_RESTART   0x0002
#define NBSD_SA_RESETHAND 0x0004
#define NBSD_SA_NOCLDSTOP 0x0008
#define NBSD_SA_NODEFER   0x0010
#define NBSD_SA_NOCLDWAIT 0x0020
#define NBSD_SA_SIGINFO   0x0040

static int nbsd_to_native_saflags(int f) {
    int o = 0;
    if (f & NBSD_SA_ONSTACK)   o |= SA_ONSTACK;
    if (f & NBSD_SA_RESTART)   o |= SA_RESTART;
    if (f & NBSD_SA_RESETHAND) o |= SA_RESETHAND;
    if (f & NBSD_SA_NOCLDSTOP) o |= SA_NOCLDSTOP;
    if (f & NBSD_SA_NODEFER)   o |= SA_NODEFER;
    if (f & NBSD_SA_NOCLDWAIT) o |= SA_NOCLDWAIT;
    if (f & NBSD_SA_SIGINFO)   o |= SA_SIGINFO;
    return o;
}

static int native_to_nbsd_saflags(int f) {
    int o = 0;
    if (f & SA_ONSTACK)   o |= NBSD_SA_ONSTACK;
    if (f & SA_RESTART)   o |= NBSD_SA_RESTART;
    if (f & SA_RESETHAND) o |= NBSD_SA_RESETHAND;
    if (f & SA_NOCLDSTOP) o |= NBSD_SA_NOCLDSTOP;
    if (f & SA_NODEFER)   o |= NBSD_SA_NODEFER;
    if (f & SA_NOCLDWAIT) o |= NBSD_SA_NOCLDWAIT;
    if (f & SA_SIGINFO)   o |= NBSD_SA_SIGINFO;
    return o;
}

/*
 * NetBSD i386 `struct sigaction` (24 bytes): a 4-byte handler, a 16-byte
 * sigset_t mask, then a 4-byte flags word -- not substrate's 12-byte
 * native layout, so it cannot be copied straight through.
 */
struct nbsd_sigaction {
    uint32_t nsa_handler;
    uint32_t nsa_mask[4];
    int32_t  nsa_flags;
};

void netbsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs_ptr) {
    (void)flags;
    registers_t *regs = (registers_t *)regs_ptr;
    uint32_t esp = regs->useresp;
    
    struct netbsd_sigframe frame;
    memset(&frame, 0, sizeof(frame));

    esp -= sizeof(struct netbsd_sigframe);
    esp &= ~0xFUL;

    if (validate_user_addr((void*)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    /* The handler is invoked with the NetBSD signal number, and the saved
     * mask in the sigcontext is the NetBSD-numbered set to restore.  The
     * first word is the return address: the NetBSD sigreturn trampoline,
     * which the handler returns into. */
    frame.sf_ra = NBSD_SIG_TRAMPOLINE_ADDR;
    frame.sf_sig = native_to_netbsd_signo(sig);
    frame.sf_code = 0;
    frame.sf_scp = esp + offsetof(struct netbsd_sigframe, sf_sc);

    frame.sf_sc.sc_eip = regs->eip;
    frame.sf_sc.sc_eax = regs->eax;
    frame.sf_sc.sc_ebx = regs->ebx;
    frame.sf_sc.sc_ecx = regs->ecx;
    frame.sf_sc.sc_edx = regs->edx;
    frame.sf_sc.sc_edi = regs->edi;
    frame.sf_sc.sc_esi = regs->esi;
    frame.sf_sc.sc_ebp = regs->ebp;
    frame.sf_sc.sc_esp = regs->useresp;
    frame.sf_sc.sc_eflags = regs->eflags;
    frame.sf_sc.sc_cs = regs->cs;
    frame.sf_sc.sc_ss = regs->ss;
    frame.sf_sc.sc_ds = regs->ds;
    frame.sf_sc.sc_es = regs->es;
    frame.sf_sc.sc_fs = regs->fs;
    frame.sf_sc.sc_gs = regs->gs;
    frame.sf_sc.sc_mask = native_to_netbsd_sigmask(mask);

    if (copyout(&frame, (void*)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    regs->useresp = esp;
    regs->eip = (uint32_t)handler;
}

int netbsd_sys_sigreturn(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    struct netbsd_sigcontext *scp_user = (struct netbsd_sigcontext *)regs->ebx;
    
    struct netbsd_sigcontext sc;
    if (copyin(scp_user, &sc, sizeof(sc)) != 0) return -1;

    regs->eip = sc.sc_eip;
    regs->eax = sc.sc_eax;
    regs->ebx = sc.sc_ebx;
    regs->ecx = sc.sc_ecx;
    regs->edx = sc.sc_edx;
    regs->edi = sc.sc_edi;
    regs->esi = sc.sc_esi;
    regs->ebp = sc.sc_ebp;
    regs->useresp = sc.sc_esp;
    /* EFLAGS sanitize: keep kernel-owned bits (IOPL/VM/RF/NT), take only
     * user bits from the frame — a raw assignment is an IOPL privilege
     * escalation.  Mirrors native sys_sigreturn. */
    regs->eflags = (regs->eflags & 0x00033200u) | (sc.sc_eflags & 0xFFCCCDFFu);
    regs->cs = sc.sc_cs | 3;
    regs->ss = sc.sc_ss | 3;
    regs->ds = sc.sc_ds | 3;
    regs->es = sc.sc_es | 3;
    regs->fs = sc.sc_fs | 3;
    regs->gs = sc.sc_gs | 3;

    /* sc_mask is a NetBSD-numbered set; the kernel mask is native. */
    current_thread->sig_mask = netbsd_to_native_sigmask(sc.sc_mask);
    /* Trapframe is now the restored user context — the dispatcher must
     * not apply its eax/edx/CF writebacks (see sys_sigreturn). */
    current_thread->frame_replaced = 1;
    return regs->eax;
}

/*
 * __sigaction_sigtramp(2) (NetBSD syscall 340): the modern sigaction.
 *   int __sigaction_sigtramp(int sig, const struct sigaction *nsa,
 *                            struct sigaction *osa, const void *tramp,
 *                            int vers);
 * The trampoline/version args select the libc-supplied signal return
 * trampoline; substrate installs its own via sendsig, so they are ignored.
 * Translate the NetBSD signal number, sa_mask and sa_flags into native
 * form and drive kern_sigaction.
 */
int netbsd_sys_sigaction(int sig, const void *nsa, void *osa,
                         const void *tramp, int vers) {
    (void)tramp; (void)vers;

    int nat = netbsd_to_native_signo(sig);
    struct sigaction kact, koact;
    struct sigaction *p_act = NULL;

    if (nsa) {
        struct nbsd_sigaction na;
        if (copyin(nsa, &na, sizeof(na)) != 0) return -14;
        memset(&kact, 0, sizeof(kact));
        kact.sa_handler = (sig_t)(uintptr_t)na.nsa_handler;
        kact.sa_mask    = netbsd_to_native_sigmask(na.nsa_mask[0]);
        kact.sa_flags   = nbsd_to_native_saflags(na.nsa_flags);
        p_act = &kact;
    }

    int ret = kern_sigaction(nat, p_act, osa ? &koact : NULL);
    if (ret != 0) return ret;

    if (osa) {
        struct nbsd_sigaction no;
        memset(&no, 0, sizeof(no));
        no.nsa_handler  = (uint32_t)(uintptr_t)koact.sa_handler;
        no.nsa_mask[0]  = native_to_netbsd_sigmask(koact.sa_mask);
        no.nsa_flags    = native_to_nbsd_saflags(koact.sa_flags);
        if (copyout(&no, osa, sizeof(no)) != 0) return -14;
    }
    return 0;
}

/* kill(2): translate the NetBSD signal number to native. */
int netbsd_sys_kill(int pid, int sig) {
    return sys_kill(pid, netbsd_to_native_signo(sig));
}

/*
 * sigprocmask(2) / __sigprocmask14(2): the set/oset are NetBSD-numbered
 * sigset_t (only the first 32-bit word is meaningful here).  Translate the
 * incoming mask to native bit positions, and the outgoing old mask back.
 */
int netbsd_sys_sigprocmask(int how, const void *set, void *oset) {
    uint32_t kset, koset;
    uint32_t *p_set = NULL;

    if (set) {
        uint32_t nset;
        if (copyin(set, &nset, sizeof(nset)) != 0) return -14;
        kset = netbsd_to_native_sigmask(nset);
        p_set = &kset;
    }

    int ret = kern_sigprocmask(how, p_set, oset ? &koset : NULL);
    if (ret != 0) return ret;

    if (oset) {
        uint32_t nold = native_to_netbsd_sigmask(koset);
        if (copyout(&nold, oset, sizeof(nold)) != 0) return -14;
    }
    return 0;
}

/* sigsuspend(2) / __sigsuspend14(2): the mask is NetBSD-numbered. */
int netbsd_sys_sigsuspend(const void *mask) {
    uint32_t nmask = 0;
    if (mask && copyin(mask, &nmask, sizeof(nmask)) != 0) return -14;
    uint32_t kmask = netbsd_to_native_sigmask(nmask);
    return kern_sigsuspend(&kmask);
}

/* sigpending(2): the returned set is NetBSD-numbered. */
int netbsd_sys_sigpending(void *set) {
    uint32_t kset = 0;
    int ret = kern_sigpending(&kset);
    if (ret != 0) return ret;
    if (set) {
        uint32_t nset = native_to_netbsd_sigmask(kset);
        if (copyout(&nset, set, sizeof(nset)) != 0) return -14;
    }
    return 0;
}
