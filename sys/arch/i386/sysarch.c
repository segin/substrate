#include <sys/types.h>
#include <sys/sysarch.h>
#include <sys/copy.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <arch/i386/gdt.h>
#include <arch/i386/idt.h>
#include <arch/i386/intr.h>
#include <arch/i386/percpu.h>
#include <arch/i386/syscall.h>
#include <arch/i386/vm86.h>
#include <exec/perso/personality.h>
#include <exec/perso/freebsd/freebsd_user.h>

/*
 * Set up a GDT data segment with the given base for use as a GS TLS descriptor.
 * Uses GDT_TLS_START slot (one past the standard user segments).
 *
 * The slot is shared across all threads, so we also stash the per-thread base
 * in current_thread->gs_base.  arch_switch_to() reloads the slot from the
 * incoming thread's gs_base on every context switch — without that, the
 * second process to set its TLS clobbers everyone else's, and on resume the
 * %gs selector still loads but reads from the wrong (or zeroed) base.
 */
void i386_load_gs_for_thread(thread_t *t) {
    /* Only touch the GDT TLS slot if this thread actually established a
     * gs base via sysarch(I386_SET_GSBASE).  Kernel-only threads (swapper,
     * syncer, USB poll, vm_pagedaemon, kinit before exec) never set one;
     * unconditionally rewriting the slot to 0 on every context switch
     * gains nothing and risks confusing whatever userspace thread held
     * the slot's contents previously. */
    if (!t || t->gs_base == 0) return;
    gdt_set_gate(GDT_TLS_START, t->gs_base, 0xFFFFF, 0xF2, 0xC0);
}

static int set_gsbase(uint32_t base) {
    /* Ring-3 32-bit data segment: present, DPL=3, writable, 4GB limit */
    gdt_set_gate(GDT_TLS_START, base, 0xFFFFF, 0xF2, 0xC0);

    uint16_t selector = (GDT_TLS_START << 3) | 3;
    __asm__ volatile("mov %0, %%gs" : : "r"(selector));

    /* Persist into the saved trap frame so iret reloads the descriptor. */
    if (current_thread) {
        current_thread->gs_base = base;
        if (current_thread->syscall_regs)
            ((registers_t *)current_thread->syscall_regs)->gs = selector;
    }

    return 0;
}

/* Public wrapper: install a TLS base from a kernel caller (no copyin). */
int i386_set_gsbase(uint32_t base) {
    return set_gsbase(base);
}

/* sys_set_gsbase(base) — native-personality syscall that takes the
 * TLS base by value rather than via a userspace pointer.  Used by
 * /sbin/ld.so to install the TCB pointer after allocating the TLS
 * region.  Mirrors `i386_set_gsbase` but at the syscall boundary. */
int sys_set_gsbase(uint32_t base) {
    return set_gsbase(base);
}

static int get_gsbase(uint32_t *out) {
    /* Use the per-thread base, not the (potentially stale) GDT slot, since
     * the slot belongs to whichever thread last touched it. */
    uint32_t base = current_thread ? current_thread->gs_base : 0;
    return copyout(&base, out, sizeof(base));
}

/*
 * freebsd_tls_seed_tcb_thread - give a freshly installed FreeBSD TCB a valid
 * "curthread" before libthr has one.
 *
 * FreeBSD's variant-II i386 TCB keeps curthread (the struct pthread *) in
 * tcb_thread at %gs:8.  libthr reads it and dereferences it from its very
 * first call (e.g. __pthread_cleanup_push_imp touches curthread->cleanup at
 * +0x188) — before libthr's own _thr_init has populated it.  The rtld/csu
 * install the program's TLS via sysarch(I386_SET_GSBASE) with a fresh TCB
 * whose tcb_thread is NULL, so without help that first libthr call faults on
 * a near-NULL curthread (SIGSEGV at ~0x188).
 *
 * exec recorded a zeroed, permanently-mapped placeholder pthread for the main
 * thread (current_thread->fbsd_init_curthread).  When a FreeBSD process
 * installs a TCB whose tcb_thread slot is still NULL, point it at that
 * placeholder.  Once libthr stores the real main-thread pthread (a non-NULL
 * tcb_thread), this becomes a no-op and the real pointer is preserved.  New
 * (pthread_create) threads set tcb_thread before their sysarch call, so they
 * are untouched.  Best-effort: any copyin/out failure leaves the TCB as built.
 */
static void freebsd_tls_seed_tcb_thread(uint32_t new_base) {
    if (!current_process || current_process->perso_id != PERS_FREEBSD)
        return;
    if (!current_thread || current_thread->fbsd_init_curthread == 0)
        return;
    if (new_base == 0)
        return;

    uint32_t new_thr = 0;
    if (copyin((void *)(uintptr_t)(new_base + FREEBSD_TCB_THREAD_OFFSET),
               &new_thr, sizeof(new_thr)) != 0)
        return;
    if (new_thr == 0) {
        uint32_t seed = current_thread->fbsd_init_curthread;
        (void)copyout(&seed,
                      (void *)(uintptr_t)(new_base + FREEBSD_TCB_THREAD_OFFSET),
                      sizeof(seed));
    }
}

int sys_sysarch(int op, void *parms) {
    switch (op) {
        case I386_VM86:
            return vm86_init_bsd(parms);

        case I386_SET_GSBASE: {
            uint32_t base;
            if (copyin(parms, &base, sizeof(base)) != 0)
                return -EFAULT;
            /* Seed the FreeBSD main-thread pointer (tcb_thread, %gs:8) into
             * this TCB if the rtld/csu left it NULL, so curthread is valid for
             * libthr's earliest calls before its own _thr_init runs. */
            freebsd_tls_seed_tcb_thread(base);
            return set_gsbase(base);
        }

        case I386_GET_GSBASE:
            return get_gsbase(parms);

        case I386_SET_FSBASE:
            /* FS TLS not needed for FreeBSD i386 nologin; ignore silently */
            return 0;

        case I386_GET_FSBASE:
            return -EINVAL;

        default:
            return -EINVAL;
    }
}
