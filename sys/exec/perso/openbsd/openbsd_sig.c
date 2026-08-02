#include <string.h>

#include <arch/i386/idt.h>
#include <arch/i386/signal_arch.h>
#include <exec/perso/openbsd/openbsd_user.h>
#include <sys/copy.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/syscall_impl.h>

/*
 * OpenBSD <-> substrate-native signal-number translation.
 *
 * OpenBSD inherited the historical 4.3BSD signal numbering (SIGCHLD=20,
 * SIGUSR1=30, SIGUSR2=31, SIGBUS=10, SIGSYS=12, SIGSTOP=17, SIGCONT=19)
 * -- identical to its NetBSD sibling -- whereas substrate's native
 * numbering is Linux-ish (SIGCHLD=17, SIGUSR1=10, SIGBUS=7, where 20 is
 * SIGTSTP).  The kernel signal machinery -- pending sets, blocked masks,
 * psignal(), the SIGCHLD raised on child exit -- all speak NATIVE numbers,
 * so every OpenBSD signal syscall argument (and the number/mask handed to
 * a signal handler) must be translated at the personality boundary.
 * Without this, an OpenBSD program's sigaction(SIGCHLD) installs on the
 * native SIGTSTP slot, its sigsuspend() mask blocks the wrong bit, and the
 * kernel's native SIGCHLD is never caught -- job control wedges forever.
 *
 * Tables are indexed by signal number (1..31); 0 and out-of-range fall
 * back to identity.  A couple of OpenBSD signals have no native equivalent
 * (EMT, XCPU, XFSZ, INFO) and map to the nearest native slot or stay put.
 */
static const unsigned char obsd2nat_signo[32] = {
    [1] = SIGHUP,  [2] = SIGINT,  [3] = SIGQUIT, [4] = SIGILL,
    [5] = SIGTRAP, [6] = SIGABRT,
    [7] = SIGBUS,                 /* OpenBSD SIGEMT -> nearest native trap */
    [8] = SIGFPE,  [9] = SIGKILL,
    [10] = SIGBUS,                /* OpenBSD SIGBUS=10  */
    [11] = SIGSEGV,
    [12] = SIGSYS,                /* OpenBSD SIGSYS=12 -> native 31 */
    [13] = SIGPIPE, [14] = SIGALRM, [15] = SIGTERM, [16] = SIGURG,
    [17] = SIGSTOP,               /* OpenBSD SIGSTOP=17 -> native 19 */
    [18] = SIGTSTP,               /* OpenBSD SIGTSTP=18 -> native 20 */
    [19] = SIGCONT,               /* OpenBSD SIGCONT=19 -> native 18 */
    [20] = SIGCHLD,               /* OpenBSD SIGCHLD=20 -> native 17 */
    [21] = SIGTTIN, [22] = SIGTTOU,
    [23] = SIGIO,                 /* OpenBSD SIGIO=23 */
    [24] = 24, [25] = 25,         /* XCPU / XFSZ -> unused native slots */
    [26] = SIGVTALRM, [27] = SIGPROF, [28] = SIGWINCH,
    [29] = 29,                    /* SIGINFO -> unused native slot */
    [30] = SIGUSR1,               /* OpenBSD SIGUSR1=30 -> native 10 */
    [31] = SIGUSR2,               /* OpenBSD SIGUSR2=31 -> native 12 */
};

static const unsigned char nat2obsd_signo[32] = {
    [SIGHUP] = 1,  [SIGINT] = 2,  [SIGQUIT] = 3, [SIGILL] = 4,
    [SIGTRAP] = 5, [SIGABRT] = 6,
    [SIGBUS] = 10,                /* native SIGBUS=7 -> OpenBSD 10 */
    [SIGFPE] = 8,  [SIGKILL] = 9,
    [SIGUSR1] = 30,               /* native SIGUSR1=10 -> OpenBSD 30 */
    [SIGSEGV] = 11,
    [SIGUSR2] = 31,               /* native SIGUSR2=12 -> OpenBSD 31 */
    [SIGPIPE] = 13, [SIGALRM] = 14, [SIGTERM] = 15, [SIGURG] = 16,
    [SIGCHLD] = 20,               /* native SIGCHLD=17 -> OpenBSD 20 */
    [SIGCONT] = 19,               /* native SIGCONT=18 -> OpenBSD 19 */
    [SIGSTOP] = 17,               /* native SIGSTOP=19 -> OpenBSD 17 */
    [SIGTSTP] = 18,               /* native SIGTSTP=20 -> OpenBSD 18 */
    [SIGTTIN] = 21, [SIGTTOU] = 22,
    [SIGIO] = 23,
    [24] = 24, [25] = 25,
    [SIGVTALRM] = 26, [SIGPROF] = 27, [SIGWINCH] = 28,
    [29] = 29, [30] = 30,
    [SIGSYS] = 12,                /* native SIGSYS=31 -> OpenBSD 12 */
};

int openbsd_to_native_signo(int sig) {
    if (sig <= 0 || sig >= 32) return sig;
    int n = obsd2nat_signo[sig];
    return n ? n : sig;
}

int native_to_openbsd_signo(int sig) {
    if (sig <= 0 || sig >= 32) return sig;
    int n = nat2obsd_signo[sig];
    return n ? n : sig;
}

/* Remap a signal bitmask (bit (signo-1) set) between the two numbering
 * schemes.  OpenBSD's sigset_t is a single 32-bit word, so signals 1..31
 * map directly onto substrate's 32-bit mask. */
uint32_t openbsd_to_native_sigmask(uint32_t m) {
    uint32_t out = 0;
    for (int s = 1; s <= 31; s++)
        if (m & (1u << (s - 1)))
            out |= 1u << (openbsd_to_native_signo(s) - 1);
    return out;
}

uint32_t native_to_openbsd_sigmask(uint32_t m) {
    uint32_t out = 0;
    for (int s = 1; s <= 31; s++)
        if (m & (1u << (s - 1)))
            out |= 1u << (native_to_openbsd_signo(s) - 1);
    return out;
}

/* OpenBSD/4.4BSD sa_flags bit values (sys/signal.h) -- they differ from
 * substrate's native SA_* layout, so the flags word must be remapped too. */
#define OBSD_SA_ONSTACK   0x0001
#define OBSD_SA_RESTART   0x0002
#define OBSD_SA_RESETHAND 0x0004
#define OBSD_SA_NOCLDSTOP 0x0008
#define OBSD_SA_NODEFER   0x0010
#define OBSD_SA_NOCLDWAIT 0x0020
#define OBSD_SA_SIGINFO   0x0040

static int obsd_to_native_saflags(int f) {
    int o = 0;
    if (f & OBSD_SA_ONSTACK)   o |= SA_ONSTACK;
    if (f & OBSD_SA_RESTART)   o |= SA_RESTART;
    if (f & OBSD_SA_RESETHAND) o |= SA_RESETHAND;
    if (f & OBSD_SA_NOCLDSTOP) o |= SA_NOCLDSTOP;
    if (f & OBSD_SA_NODEFER)   o |= SA_NODEFER;
    if (f & OBSD_SA_NOCLDWAIT) o |= SA_NOCLDWAIT;
    if (f & OBSD_SA_SIGINFO)   o |= SA_SIGINFO;
    return o;
}

static int native_to_obsd_saflags(int f) {
    int o = 0;
    if (f & SA_ONSTACK)   o |= OBSD_SA_ONSTACK;
    if (f & SA_RESTART)   o |= OBSD_SA_RESTART;
    if (f & SA_RESETHAND) o |= OBSD_SA_RESETHAND;
    if (f & SA_NOCLDSTOP) o |= OBSD_SA_NOCLDSTOP;
    if (f & SA_NODEFER)   o |= OBSD_SA_NODEFER;
    if (f & SA_NOCLDWAIT) o |= OBSD_SA_NOCLDWAIT;
    if (f & SA_SIGINFO)   o |= OBSD_SA_SIGINFO;
    return o;
}

/*
 * OpenBSD i386 `struct sigaction` (12 bytes): a 4-byte handler, a 4-byte
 * sigset_t mask (OpenBSD keeps the traditional 32-bit sigset_t, unlike
 * NetBSD's 128-bit one), then a 4-byte flags word.
 */
struct obsd_sigaction {
    uint32_t osa_handler;
    uint32_t osa_mask;
    int32_t  osa_flags;
};

void openbsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs_ptr) {
    (void)flags;
    registers_t *regs = (registers_t *)regs_ptr;
    uint32_t esp = regs->useresp;

    struct openbsd_sigframe frame;
    memset(&frame, 0, sizeof(frame));

    esp -= sizeof(struct openbsd_sigframe);
    esp &= ~0xFUL;

    if (validate_user_addr((void*)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    /* The handler receives the OpenBSD signal number, and the saved mask in
     * the sigcontext is the OpenBSD-numbered set to restore on sigreturn. */
    frame.sf_sig = native_to_openbsd_signo(sig);
    frame.sf_code = 0;
    frame.sf_scp = esp + offsetof(struct openbsd_sigframe, sf_sc);
    frame.sf_handler = (uint32_t)handler;

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
    frame.sf_sc.sc_mask = native_to_openbsd_sigmask(mask);

    if (copyout(&frame, (void*)(uintptr_t)esp, sizeof(frame)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    regs->useresp = esp;
    regs->eip = (uint32_t)handler;
}

int openbsd_sys_sigreturn(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    struct openbsd_sigcontext *scp_user = (struct openbsd_sigcontext *)regs->ebx;

    struct openbsd_sigcontext sc;
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

    /* sc_mask is an OpenBSD-numbered set; the kernel mask is native. */
    current_thread->sig_mask = openbsd_to_native_sigmask(sc.sc_mask);
    return regs->eax;
}

/*
 * sigaction(2) (OpenBSD syscall 46):
 *   int sigaction(int sig, const struct sigaction *nsa, struct sigaction *osa);
 * Translate the OpenBSD signal number, sa_mask and sa_flags into native
 * form and drive kern_sigaction.  (OpenBSD's signal trampoline is a fixed
 * kernel-provided sigcode, not passed per-call as in NetBSD.)
 */
int openbsd_sys_sigaction(int sig, const void *nsa, void *osa) {
    int nat = openbsd_to_native_signo(sig);
    struct sigaction kact, koact;
    struct sigaction *p_act = NULL;

    if (nsa) {
        struct obsd_sigaction oa;
        if (copyin(nsa, &oa, sizeof(oa)) != 0) return -14;
        memset(&kact, 0, sizeof(kact));
        kact.sa_handler = (sig_t)(uintptr_t)oa.osa_handler;
        kact.sa_mask    = openbsd_to_native_sigmask(oa.osa_mask);
        kact.sa_flags   = obsd_to_native_saflags(oa.osa_flags);
        p_act = &kact;
    }

    int ret = kern_sigaction(nat, p_act, osa ? &koact : NULL);
    if (ret != 0) return ret;

    if (osa) {
        struct obsd_sigaction oo;
        memset(&oo, 0, sizeof(oo));
        oo.osa_handler = (uint32_t)(uintptr_t)koact.sa_handler;
        oo.osa_mask    = native_to_openbsd_sigmask(koact.sa_mask);
        oo.osa_flags   = native_to_obsd_saflags(koact.sa_flags);
        if (copyout(&oo, osa, sizeof(oo)) != 0) return -14;
    }
    return 0;
}

/* kill(2): translate the OpenBSD signal number to native. */
int openbsd_sys_kill(int pid, int sig) {
    return sys_kill(pid, openbsd_to_native_signo(sig));
}

/*
 * sigprocmask(2) (OpenBSD syscall 48):
 *   int sys_sigprocmask(int how, sigset_t mask);
 * OpenBSD kept the traditional register-based ABI: the new mask is passed
 * BY VALUE (not via a pointer as in NetBSD's __sigprocmask14) and the OLD
 * mask is returned in the return register.  The libc wrapper turns NULL
 * set into (SIG_BLOCK, 0) -- a no-op that still yields the old mask -- so
 * the given how/mask are always applied.  Masks are OpenBSD-numbered.
 */
int openbsd_sys_sigprocmask(int how, uint32_t mask) {
    uint32_t kset = openbsd_to_native_sigmask(mask);
    uint32_t koset = 0;
    int ret = kern_sigprocmask(how, &kset, &koset);
    if (ret != 0) return ret;
    return (int)native_to_openbsd_sigmask(koset);
}

/*
 * sigsuspend(2) (OpenBSD syscall 111):
 *   int sys_sigsuspend(int mask);
 * The mask is passed BY VALUE and is OpenBSD-numbered.
 */
int openbsd_sys_sigsuspend(uint32_t mask) {
    uint32_t kmask = openbsd_to_native_sigmask(mask);
    return kern_sigsuspend(&kmask);
}

/*
 * sigpending(2) (OpenBSD syscall 52):
 *   int sys_sigpending(void);
 * Takes no argument; the pending set is returned in the return register.
 */
int openbsd_sys_sigpending(void) {
    uint32_t kset = 0;
    int ret = kern_sigpending(&kset);
    if (ret != 0) return ret;
    return (int)native_to_openbsd_sigmask(kset);
}
