#include <sys/types.h>
#include <sys/sysarch.h>
#include <sys/copy.h>
#include <sys/proc.h>
#include <errno.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <arch/i386/syscall.h>
#include <arch/i386/percpu.h>
#include <arch/i386/gdt.h>

extern int vm86_init_bsd(void *args);
extern void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
extern thread_t *current_thread;

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

static int get_gsbase(uint32_t *out) {
    /* Use the per-thread base, not the (potentially stale) GDT slot, since
     * the slot belongs to whichever thread last touched it. */
    uint32_t base = current_thread ? current_thread->gs_base : 0;
    return copyout(&base, out, sizeof(base));
}

int sys_sysarch(int op, void *parms) {
    switch (op) {
        case I386_VM86:
            return vm86_init_bsd(parms);

        case I386_SET_GSBASE: {
            uint32_t base;
            if (copyin(parms, &base, sizeof(base)) != 0)
                return -EFAULT;
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
