#include <sys/types.h>
#include <sys/sysarch.h>
#include <sys/copy.h>
#include <sys/proc.h>
#include <sys/ldt.h>
#include <errno.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <arch/i386/syscall.h>
#include <arch/i386/percpu.h>
#include <arch/i386/gdt.h>

extern int vm86_init_bsd(void *args);
extern void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
extern thread_t *current_thread;
extern process_t *current_process;

/*
 * LDT slot reserved for the *BSD %gs TLS descriptor.  Each process owns
 * its own LDT (loaded via lldt on context switch in arch_switch_to), so
 * placing the TLS descriptor here gives full per-process isolation —
 * unlike the legacy GDT_TLS_START approach where every personality
 * shared one slot and the most-recent setter clobbered everyone else.
 */
#define BSD_LDT_TLS_SLOT 0
#define BSD_LDT_TLS_SEL  ((BSD_LDT_TLS_SLOT << 3) | 0x4 | 3)

/*
 * Linux's set_thread_area still uses the GDT (glibc's selector formula
 * `(entry_number << 3) | 3` cannot encode TI=1).  This function reloads
 * GDT_TLS_START from the incoming thread's saved gs_base on context
 * switch — required for Linux multi-threaded processes and for any
 * Linux thread whose TLS slot was overwritten by another process
 * before our cross-personality cleanup.
 *
 * No-op for *BSD threads (their gs_base is 0; their TLS lives in the
 * per-process LDT and is restored by lldt).
 */
void i386_load_gs_for_thread(thread_t *t) {
    if (!t || t->gs_base == 0) return;
    gdt_set_gate(GDT_TLS_START, t->gs_base, 0xFFFFF, 0xF2, 0xC0);
}

static int set_gsbase(uint32_t base) {
    /* *BSD path: install the TLS descriptor into the current process's
     * own LDT slot (process-isolated — context switch reloads via lldt). */
    if (!current_process) return -ESRCH;
    int rc = ldt_set_tls_base(current_process, BSD_LDT_TLS_SLOT, base);
    if (rc < 0) return rc;

    uint16_t selector = (uint16_t)BSD_LDT_TLS_SEL;
    __asm__ volatile("mov %0, %%gs" : : "r"(selector));

    if (current_thread) {
        /* gs_base persists for I386_GET_GSBASE.  The per-thread reload
         * path may also write this to GDT_TLS_START — harmless for BSD
         * threads (their %gs is LDT-based and ignores the GDT slot)
         * and useful for Linux threads sharing the same field. */
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
