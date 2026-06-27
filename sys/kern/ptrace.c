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

#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/ptrace.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <arch/i386/idt.h>
#include <arch/i386/pmap.h>
#include <vm/vm_fault.h>
#include <pm/pm.h>
#include <string.h>

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
        if (tracee->p_flag & P_TRACED) return -EPERM;

        mutex_lock(&proctree_lock);
        tracee->p_oparent = tracee->p_parent;
        ptrace_unlink_child(tracee->p_parent, tracee);
        tracee->p_parent = me;
        ptrace_link_child(me, tracee);
        mutex_unlock(&proctree_lock);

        tracee->p_tracer = me;
        tracee->p_flag |= P_TRACED;
        psignal(tracee, SIGSTOP);
        return 0;
    }

    /* Everything else requires us to be this process's tracer. */
    if (tracee->p_tracer != me) return -ESRCH;

    frame = (registers_t *)ptrace_user_frame(tracee);

    switch (req) {
    case PTRACE_PEEKTEXT:
    case PTRACE_PEEKDATA: {
        /* Read one word from the tracee at `addr`; store it through the
         * tracer's `data` pointer.  Returns 0/-errno (the libc wrapper turns
         * this back into the classic "PEEK returns the word"). */
        uint32_t word = 0;
        ptrace_fault_in(tracee, (uint32_t)addr, sizeof(word));
        if (pmap_copyin_other(tracee->pmap, (uintptr_t)(uint32_t)addr,
                              &word, sizeof(word)) != sizeof(word)) {
            return -EFAULT;
        }
        if (copyout(&word, (void *)(uint32_t)data, sizeof(word)) != 0) {
            return -EFAULT;
        }
        return 0;
    }

    case PTRACE_POKETEXT:
    case PTRACE_POKEDATA: {
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
        memset(&urs, 0, sizeof(urs));
        frame_to_uregs(frame, &urs);
        if (copyout(&urs, (void *)(uint32_t)data, sizeof(urs)) != 0) {
            return -EFAULT;
        }
        return 0;
    }

    case PTRACE_SETREGS: {
        struct user_regs_struct urs;
        if (!frame) return -EFAULT;
        if (copyin((void *)(uint32_t)data, &urs, sizeof(urs)) != 0) {
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
