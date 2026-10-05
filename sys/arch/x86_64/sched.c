/*
 * sched.c - x86_64 thread creation and context switch
 *
 * The same entry points as arch/i386/sched.c.  A thread's saved context is
 * its kernel stack pointer: switch_to() hands the old and new ones to
 * switch_stacks() (isr.S), which saves and restores the callee-saved
 * registers on the stacks themselves.  A thread that has never run gets a
 * stack in that shape whose return address is its first-run trampoline:
 *
 *   kernel thread   new_kernel_thread_trampoline: entry(arg)
 *   user thread     new_user_thread_trampoline: iretq into 32-bit code
 *   fork / clone    fork_child_return: a copied trap frame through isr_exit
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <arch/x86-common/fpu.h>
#include <arch/x86-common/intr.h>
#include <arch/x86_64/gdt.h>
#include <machine/idt.h>
#include <machine/percpu.h>
#include <machine/pmap.h>
#include <machine/pmm.h>
#include <exec/perso/personality.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <pm/pm.h>
#include <sys/acct.h>
#include <sys/ldt.h>
#include <sys/smp.h>
#include <sys/sysinfo.h>
#include <vfs/vfs.h>

/* switch_stacks() pops these, lowest address first, then returns. */
struct switch_frame {
    uint64_t r15, r14, r13, r12, rbx, rbp;
    uint64_t ret;
};

#define KSTACK_PAGES    4               /* 16 KiB, as on i386 */
#define KSTACK_BYTES    (KSTACK_PAGES * 0x1000)

void switch_stacks(uintptr_t *save_sp, uintptr_t load_sp);
void sched_kernel_thread_exit(void);

void switch_to(thread_t *prev, thread_t *next) {
    switch_stacks(&prev->kstack_ptr, next->kstack_ptr);
}

void arch_switch_to(thread_t *prev, thread_t *next) {
    if (next->proc && next->proc->pmap) {
        pmap_activate(next->proc->pmap);
    }

    if (next->proc != prev->proc) {
        ldt_activate(next->proc);
    }

    /* Lazy FPU, per thread: unless the live registers are already the
     * incoming thread's, it traps (#NM) on its first FPU use, so that its
     * registers are loaded and the owner's saved. */
    fpu_switch(next);

    /* The thread's TLS base into GDT slot 6, which its %gs selects. */
    i386_load_gs_for_thread(next);

    switch_to(prev, next);
}

void arch_set_kernel_stack(uintptr_t stack) {
    set_kernel_stack(stack);
}

/* Where a kernel thread goes when its entry function returns. */
void sched_kernel_thread_exit(void) {
    if (current_thread) {
        current_thread->state = THREAD_ZOMBIE;
    }
    sched_yield();
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void sched_init(void) {
    sched_init_generic();

    sched_smp_init(smp_get_cpu_count());

    kernel_process = proc_bootstrap_kernel(0, PERS_NATIVE);
    if (!kernel_process) {
        for (;;) { __asm__ volatile("hlt"); } /* unrecoverable */
    }
    kernel_process->root_node = fs_root;
    kernel_process->pmap = pmap_kernel();
    ldt_init_process(kernel_process);
    strlcpy(kernel_process->comm, "swapper", sizeof(kernel_process->comm));

    thread_t *t = sched_alloc_thread(kernel_process);
    t->state = THREAD_RUNNING;
    t->priority = 20;
    t->base_priority = 20;
    t->bound_cpu = 0;

    current_thread = t;
    current_process = kernel_process;
    THIS_CPU()->current = t;
    THIS_CPU()->idle = t;

    pmap_activate(kernel_process->pmap);
}

/* A 16 KiB kernel stack for `t`; returns its top, or 0. */
static uintptr_t sched_alloc_kstack(thread_t *t) {
    void *base = pmm_alloc_contiguous(KSTACK_PAGES);

    if (!base) {
        t->proc = NULL;
        t->tid = -1;
        t->state = THREAD_ZOMBIE;
        return 0;
    }
    t->kstack_base = (uintptr_t)base;
    t->kstack_top = (uintptr_t)base + KSTACK_BYTES;
    t->kstack_units = KSTACK_PAGES;
    t->kstack_type = THREAD_KSTACK_PMM_CONTIG;
    t->kstack_owned = 1;
    return t->kstack_top;
}

/*
 * Lay out a copy of `regs` (with %eax = 0 and the user stack `user_sp`)
 * on t's kernel stack under a switch frame that returns to
 * fork_child_return, which leaves through isr_exit.
 */
static void sched_build_child_frame(thread_t *t, const registers_t *regs,
                                    uint32_t user_sp) {
    uintptr_t sp = t->kstack_top;

    sp -= sizeof(registers_t);
    registers_t *child = (registers_t *)sp;
    *child = *regs;
    child->rax = 0;                     /* the child's return value */
    if (regs->cs == SEL_UCODE_RPL3) {
        /* A 64-bit child does not pass through the system-call return
         * path, which is what clears carry for "no error". */
        child->rflags &= ~1UL;
    }
    child->useresp = user_sp;

    sp -= sizeof(struct switch_frame);
    struct switch_frame *sf = (struct switch_frame *)sp;
    memset(sf, 0, sizeof(*sf));
    sf->ret = (uint64_t)(uintptr_t)fork_child_return;

    t->kstack_ptr = sp;
    t->instr_ptr = regs->eip;
    t->state = THREAD_READY;
}

int sched_fork_thread(process_t *proc, void *parent_regs) {
    const registers_t *regs = (const registers_t *)parent_regs;

    thread_t *t = sched_alloc_thread(proc);
    if (!t) return -1;

    /* The child keeps the parent thread's TLS base (see arch/i386). */
    if (current_thread)
        t->gs_base = current_thread->gs_base;

    if (!sched_alloc_kstack(t)) {
        return -1;
    }
    sched_build_child_frame(t, regs, regs->useresp);
    return proc->pid;
}

int sched_clone_thread(process_t *proc, void *parent_regs, uint32_t tls_base,
                       int *clear_child_tid) {
    const registers_t *regs = (const registers_t *)parent_regs;

    thread_t *t = sched_alloc_thread(proc);
    if (!t) return -1;

    t->gs_base = tls_base;
    t->exit_tid_ptr = clear_child_tid;

    if (!sched_alloc_kstack(t)) {
        return -1;
    }
    /* The caller already put the child's stack in parent_regs->useresp. */
    sched_build_child_frame(t, regs, regs->useresp);
    return t->tid;
}

thread_t *sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg) {
    thread_t *t = sched_alloc_thread(proc);
    if (!t) return NULL;

    /* A kernel function, or user code (libpthread's trampoline, via
     * thr_new) -- told apart by the address, as on i386. */
    int is_user = (uintptr_t)entry_point < USER32_VA_END;
    uintptr_t sp;
    struct switch_frame *sf;

    if (!is_user) {
        /* The caller's stack is the kernel stack. */
        sp = (uintptr_t)stack & ~(uintptr_t)0xF;
        t->kstack_top = sp;
        sp -= sizeof(struct switch_frame);
        sf = (struct switch_frame *)sp;
        memset(sf, 0, sizeof(*sf));
        sf->r15 = (uint64_t)(uintptr_t)entry_point;
        sf->r14 = (uint64_t)(uintptr_t)arg;
        sf->ret = (uint64_t)(uintptr_t)new_kernel_thread_trampoline;
    } else {
        sp = sched_alloc_kstack(t);
        if (!sp) {
            return NULL;
        }
        sp -= sizeof(struct switch_frame);
        sf = (struct switch_frame *)sp;
        memset(sf, 0, sizeof(*sf));
        sf->r15 = (uint64_t)(uintptr_t)entry_point;    /* user_entry */
        sf->r14 = (uint64_t)(uintptr_t)stack;          /* user_stack */
        sf->r13 = (uint64_t)(uintptr_t)arg;            /* user_arg */
        sf->ret = (proc->bitness == BITNESS_64)
            ? (uint64_t)(uintptr_t)new_user_thread_trampoline64
            : (uint64_t)(uintptr_t)new_user_thread_trampoline;
    }

    t->kstack_ptr = sp;
    t->instr_ptr = (uintptr_t)entry_point;
    t->state = THREAD_READY;
    return t;
}
