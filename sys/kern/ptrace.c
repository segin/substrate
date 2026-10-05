/*
 * sys/kern/ptrace.c - ptrace(2): process inspection and control.
 *
 * Implements the native ptrace syscall (SYS_PTRACE = 26) that debuggers (gdb)
 * use to inspect and steer another process: read/write its registers
 * (GETREGS/SETREGS) and memory (PEEK/POKE), stop it on signals, single-step it
 * (SINGLESTEP), resume it (CONT) and detach (DETACH).
 *
 * Model
 * -----
 * A tracee carries the P_TRACED flag and a p_tracer pointer.  PTRACE_TRACEME
 * marks the caller as traced by its parent (gdb's launch-a-program path, where
 * the child execs under the tracer).  PTRACE_ATTACH stops an already-running
 * process and reparents it onto the tracer so the existing wait4() machinery
 * reports its stops (the "inspect another process" path).
 *
 * When a traced process takes any signal it stops in signal_handle_pending()
 * (sys/kern/signal.c), which records its user-mode trapframe in
 * thread->user_frame and wakes the tracer's wait4().  The tracer then issues
 * GET/SET/PEEK/POKE against that parked state and CONT/SINGLESTEP/DETACH to
 * resume it.  Register access goes through the saved trapframe; memory access
 * goes through pmap_copyin_other()/pmap_copyout_other(), which walk the
 * tracee's page tables via the physical direct map so they work on a process
 * that is not the current address space.
 */

#include <string.h>

#include <machine/idt.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>
#include <pm/pm.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/signal.h>
#include <sys/sysinfo.h>
#include <vm/vm_fault.h>

#define EFLAGS_TF 0x00000100u   /* trap flag — single-step after each insn */

/* ---- register marshalling: kernel registers_t <-> user_regs_struct ------- */

static void frame_to_uregs(const registers_t *f, struct user_regs_struct *u) {
    u->ebx = f->ebx; u->ecx = f->ecx; u->edx = f->edx;
    u->esi = f->esi; u->edi = f->edi; u->ebp = f->ebp; u->eax = f->eax;
    u->xds = f->ds; u->xes = f->es; u->xfs = f->fs; u->xgs = f->gs;
    u->orig_eax = f->eax;
    u->eip = f->eip; u->xcs = f->cs; u->eflags = f->eflags;
    u->esp = f->useresp;        /* user stack pointer (CPU-pushed, not pusha's) */
    u->xss = f->ss;
}

static void uregs_to_frame(const struct user_regs_struct *u, registers_t *f) {
    f->ebx = u->ebx; f->ecx = u->ecx; f->edx = u->edx;
    f->esi = u->esi; f->edi = u->edi; f->ebp = u->ebp; f->eax = u->eax;
    f->ds = u->xds; f->es = u->xes; f->fs = u->xfs; f->gs = u->xgs;
    f->eip = u->eip; f->cs = u->xcs; f->eflags = u->eflags;
    f->useresp = u->esp; f->ss = u->xss;
}

#ifdef SUBSTRATE_ARCH_X86_64
/*
 * A 64-bit tracer (gdb built for x86-64) exchanges the amd64 register set
 * and moves 8-byte memory words -- its sizeof(long).  What decides is the
 * tracer's width, not the tracee's: a 64-bit debugger expects the 64-bit
 * layout even for a 32-bit tracee, whose registers come back zero-extended,
 * as on Linux.
 */
static int ptrace_tracer_is_amd64(void) {
    return current_process && current_process->bitness == BITNESS_64;
}

/* The flags a tracer may change: CF PF AF ZF SF TF DF OF.  IF, IOPL, NT,
 * RF, VM and AC stay the kernel's -- a tracer must not be able to run its
 * tracee with interrupts off or with I/O privilege. */
#define EFLAGS_USER_MASK 0x00000DD5u

/* Lowest non-canonical address.  iretq to a non-canonical RIP or RSP faults
 * in ring 0, so neither may come from a tracer. */
#define PTRACE_CANONICAL_LIMIT 0x0000800000000000ULL

static void frame_to_uregs64(const registers_t *f, const thread_t *t,
                             const process_t *tracee,
                             struct user_regs_struct64 *u) {
    u->r15 = f->r15; u->r14 = f->r14; u->r13 = f->r13; u->r12 = f->r12;
    u->r11 = f->r11; u->r10 = f->r10; u->r9  = f->r9;  u->r8  = f->r8;
    u->rbp = f->rbp; u->rbx = f->rbx; u->rax = f->rax; u->rcx = f->rcx;
    u->rdx = f->rdx; u->rsi = f->rsi; u->rdi = f->rdi;
    u->orig_rax = f->rax;
    u->rip = f->rip; u->cs = f->cs64; u->eflags = f->rflags;
    u->rsp = f->rsp; u->ss = f->ss64;
    u->ds = f->ds64; u->es = f->es64; u->fs = f->fs64; u->gs = f->gs64;
    /* A 64-bit thread's pointer is its %fs base, kept in gs_base
     * (arch/i386/sysarch.c). */
    u->fs_base = (t && tracee->bitness == BITNESS_64) ? t->gs_base : 0;
    u->gs_base = 0;
}

/*
 * The registers a tracer may set.  The selectors stay the tracee's own: a
 * tracer-supplied %cs or %ss could name a kernel segment, and the tracee
 * would then be resumed in ring 0.  Only the user flags are taken, and a
 * RIP or RSP that is not canonical is refused.
 */
static int uregs64_to_frame(const struct user_regs_struct64 *u, registers_t *f,
                            thread_t *t, const process_t *tracee) {
    if (u->rip >= PTRACE_CANONICAL_LIMIT || u->rsp >= PTRACE_CANONICAL_LIMIT) {
        return -EIO;
    }
    /* A 32-bit tracee returns to a compatibility-mode segment, where an
     * instruction pointer above 4 GiB is past the segment limit. */
    if (tracee->bitness != BITNESS_64 &&
        (u->rip > 0xFFFFFFFFULL || u->rsp > 0xFFFFFFFFULL)) {
        return -EIO;
    }
    if (t && tracee->bitness == BITNESS_64 && u->fs_base != t->gs_base) {
        /* The base is kept in 32 bits; user space ends below 4 GiB. */
        if (u->fs_base >= USER32_VA_END) return -EIO;
        t->gs_base = (uint32_t)u->fs_base;
    }
    f->r15 = u->r15; f->r14 = u->r14; f->r13 = u->r13; f->r12 = u->r12;
    f->r11 = u->r11; f->r10 = u->r10; f->r9  = u->r9;  f->r8  = u->r8;
    f->rbp = u->rbp; f->rbx = u->rbx; f->rax = u->rax; f->rcx = u->rcx;
    f->rdx = u->rdx; f->rsi = u->rsi; f->rdi = u->rdi;
    f->rip = u->rip; f->rsp = u->rsp;
    f->rflags = (f->rflags & ~(uint64_t)EFLAGS_USER_MASK) |
                (u->eflags & EFLAGS_USER_MASK);
    return 0;
}
#endif

/* ---- children-list surgery for PTRACE_ATTACH reparenting ------------------ */

static void ptrace_unlink_child(process_t *parent, process_t *child) {
    process_t *c;
    if (!parent || !child) return;
    if (parent->p_children == child) {
        parent->p_children = child->p_sibling;
        return;
    }
    for (c = parent->p_children; c; c = c->p_sibling) {
        if (c->p_sibling == child) {
            c->p_sibling = child->p_sibling;
            return;
        }
    }
}

static void ptrace_link_child(process_t *parent, process_t *child) {
    child->p_sibling = parent->p_children;
    parent->p_children = child;
}

/* Make the tracee's page(s) covering [addr, addr+len) resident.  Tracee text
 * and data are demand-paged, but pmap_copyin/out_other only walk *present*
 * PTEs — so without this a PEEK/POKE into not-yet-touched memory (e.g. gdb
 * planting a breakpoint in code that hasn't run) spuriously fails. */
static void ptrace_fault_in(process_t *t, uint32_t addr, uint32_t len) {
    if (!t || !t->vm_map) return;
    vm_fault(t->vm_map, addr, VM_PROT_READ);
    if (len > 1) {
        vm_fault(t->vm_map, addr + len - 1, VM_PROT_READ);
    }
}

/* ---- the syscall --------------------------------------------------------- */

int sys_ptrace(int req, int pid, int addr, int data) {
    process_t *me = current_process;
    process_t *tracee;
    registers_t *frame;

    if (!me) return -ESRCH;

    /* PTRACE_TRACEME: caller asks to be traced by its parent. */
    if (req == PTRACE_TRACEME) {
        me->p_flag |= P_TRACED;
        me->p_tracer = me->p_parent;
        return 0;
    }

    tracee = proc_find(pid);
    if (!tracee) return -ESRCH;

    /* PTRACE_ATTACH: become the tracer of an existing process and stop it.
     * Reparent it onto us so wait4() (which scans our children) sees its
     * stops; remember the real parent to restore on detach. */
    if (req == PTRACE_ATTACH) {
        if (tracee == me || tracee->pid <= 1) return -EPERM;

        /* Credential check: only root, or a tracer whose euid matches every one
         * of the tracee's uids and egid matches every one of its gids, may
         * attach.  Without this any unprivileged process could reparent, stop,
         * and PEEK/POKE the memory + registers of any other user's process
         * (privilege escalation / information disclosure). */
        if (me->euid != 0 &&
            (me->euid != tracee->uid || me->euid != tracee->euid ||
             me->euid != tracee->suid ||
             me->egid != tracee->gid || me->egid != tracee->egid ||
             me->egid != tracee->sgid)) {
            return -EPERM;
        }

        mutex_lock(&proctree_lock);
        /* Re-check the traced flag and perform the reparent + tracer/flag set
         * atomically under proctree_lock, so two concurrent attachers can't
         * both win (and holding the lock keeps the tracee from being reaped
         * mid-attach). */
        if (tracee->p_flag & P_TRACED) {
            mutex_unlock(&proctree_lock);
            return -EPERM;
        }
        tracee->p_oparent = tracee->p_parent;
        ptrace_unlink_child(tracee->p_parent, tracee);
        tracee->p_parent = me;
        ptrace_link_child(me, tracee);
        tracee->p_tracer = me;
        tracee->p_flag |= P_TRACED;
        mutex_unlock(&proctree_lock);

        psignal(tracee, SIGSTOP);
        return 0;
    }

    /* Everything else requires us to be this process's tracer. */
    if (tracee->p_tracer != me) return -ESRCH;

    frame = (registers_t *)ptrace_user_frame(tracee);

    switch (req) {
    case PTRACE_PEEKTEXT:
    case PTRACE_PEEKDATA: {
#ifdef SUBSTRATE_ARCH_X86_64
        if (ptrace_tracer_is_amd64()) {
            /* A 64-bit tracer's word is 8 bytes.  libsys always passes an
             * out-pointer, so there is no in-band form to honour: the
             * return value is 32 bits wide and could not carry the word. */
            uint64_t word64 = 0;
            ptrace_fault_in(tracee, (uint32_t)addr, sizeof(word64));
            if (pmap_copyin_other(tracee->pmap, (uintptr_t)(uint32_t)addr,
                                  &word64, sizeof(word64)) != sizeof(word64)) {
                return -EFAULT;
            }
            if ((void *)(uintptr_t)(uint32_t)data == NULL) return -EINVAL;
            if (copyout(&word64, (void *)(uintptr_t)(uint32_t)data,
                        sizeof(word64)) != 0) {
                return -EFAULT;
            }
            return 0;
        }
#endif
        /* Read one word from the tracee at `addr`; store it through the
         * tracer's `data` pointer.  Returns 0/-errno (the libc wrapper turns
         * this back into the classic "PEEK returns the word"). */
        uint32_t word = 0;
        ptrace_fault_in(tracee, (uint32_t)addr, sizeof(word));
        if (pmap_copyin_other(tracee->pmap, (uintptr_t)(uint32_t)addr,
                              &word, sizeof(word)) != sizeof(word)) {
            return -EFAULT;
        }
        /* gdb's inf-ptrace reads memory the classic BSD PT_READ_I way:
         * data == NULL, and the word comes back as the syscall return value.
         * The Linux/modern form instead passes a non-NULL out-pointer and
         * reads 0/-errno.  Honour both — without the NULL case the kernel
         * copied the word to address 0, returned EFAULT, and gdb saw every
         * memory read fail (so it could not insert a breakpoint). */
        if ((void *)(uintptr_t)(uint32_t)data == NULL) {
            return (int)word;
        }
        if (copyout(&word, (void *)(uintptr_t)(uint32_t)data, sizeof(word)) != 0) {
            return -EFAULT;
        }
        return 0;
    }

    case PTRACE_POKETEXT:
    case PTRACE_POKEDATA: {
#ifdef SUBSTRATE_ARCH_X86_64
        if (ptrace_tracer_is_amd64()) {
            /* The dispatcher hands handlers the low word of each argument;
             * a 64-bit word to store is taken whole from the tracer's
             * syscall frame (argument 4 is %r10). */
            const registers_t *sr =
                (const registers_t *)current_thread->syscall_regs;
            uint64_t word64 = sr ? sr->r10 : (uint32_t)data;
            ptrace_fault_in(tracee, (uint32_t)addr, sizeof(word64));
            if (pmap_copyout_other(tracee->pmap, (uintptr_t)(uint32_t)addr,
                                   &word64, sizeof(word64)) != sizeof(word64)) {
                return -EFAULT;
            }
            return 0;
        }
#endif
        uint32_t word = (uint32_t)data;
        ptrace_fault_in(tracee, (uint32_t)addr, sizeof(word));
        if (pmap_copyout_other(tracee->pmap, (uintptr_t)(uint32_t)addr,
                               &word, sizeof(word)) != sizeof(word)) {
            return -EFAULT;
        }
        return 0;
    }

    case PTRACE_GETREGS: {
        struct user_regs_struct urs;
        if (!frame) return -EFAULT;
#ifdef SUBSTRATE_ARCH_X86_64
        if (ptrace_tracer_is_amd64()) {
            struct user_regs_struct64 u64;
            memset(&u64, 0, sizeof(u64));
            frame_to_uregs64(frame, ptrace_user_thread(tracee), tracee, &u64);
            if (copyout(&u64, (void *)(uintptr_t)(uint32_t)data,
                        sizeof(u64)) != 0) {
                return -EFAULT;
            }
            return 0;
        }
#endif
        memset(&urs, 0, sizeof(urs));
        frame_to_uregs(frame, &urs);
        if (copyout(&urs, (void *)(uintptr_t)(uint32_t)data, sizeof(urs)) != 0) {
            return -EFAULT;
        }
        return 0;
    }

    case PTRACE_SETREGS: {
        struct user_regs_struct urs;
        if (!frame) return -EFAULT;
#ifdef SUBSTRATE_ARCH_X86_64
        if (ptrace_tracer_is_amd64()) {
            struct user_regs_struct64 u64;
            if (copyin((void *)(uintptr_t)(uint32_t)data, &u64,
                       sizeof(u64)) != 0) {
                return -EFAULT;
            }
            return uregs64_to_frame(&u64, frame, ptrace_user_thread(tracee),
                                    tracee);
        }
#endif
        if (copyin((void *)(uintptr_t)(uint32_t)data, &urs, sizeof(urs)) != 0) {
            return -EFAULT;
        }
        uregs_to_frame(&urs, frame);
        return 0;
    }

    case PTRACE_SINGLESTEP:
        if (frame) frame->eflags |= EFLAGS_TF;
        tracee->p_xsig = 0;
        signal_resume_process_threads(tracee);
        return 0;

    case PTRACE_CONT:
        /* (data carries a signal to re-inject; not yet honoured — CONT
         * currently suppresses the stop signal.) */
        if (frame) frame->eflags &= ~EFLAGS_TF;
        tracee->p_xsig = 0;
        signal_resume_process_threads(tracee);
        return 0;

    case PTRACE_DETACH:
        if (frame) frame->eflags &= ~EFLAGS_TF;
        if (tracee->p_oparent) {
            mutex_lock(&proctree_lock);
            ptrace_unlink_child(tracee->p_parent, tracee);
            tracee->p_parent = tracee->p_oparent;
            ptrace_link_child(tracee->p_oparent, tracee);
            tracee->p_oparent = NULL;
            mutex_unlock(&proctree_lock);
        }
        tracee->p_tracer = NULL;
        tracee->p_flag &= (uint16_t)~P_TRACED;
        tracee->p_xsig = 0;
        signal_resume_process_threads(tracee);
        return 0;

    case PTRACE_KILL:
        signal_resume_process_threads(tracee);
        psignal(tracee, SIGKILL);
        return 0;

    default:
        return -EINVAL;
    }
}
