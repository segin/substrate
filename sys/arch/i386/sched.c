#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <arch/i386/fpu/fpu_emu.h>
#include <arch/i386/intr.h>
#include <arch/i386/percpu.h>
#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <exec/perso/personality.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <pm/pm.h>
#include <sys/acct.h>
#include <sys/ldt.h>
#include <sys/smp.h>
#include <vfs/vfs.h>

// Exposed to Generic Scheduler
void arch_switch_to(thread_t *prev, thread_t *next) {
    // Switch Address Space if needed
    if (next->proc && next->proc->pmap) {
        pmap_activate(next->proc->pmap);
    }

    // Switch LDT if needed
    if (next->proc != prev->proc) {
        ldt_activate(next->proc);

        /* Re-arm CR0.TS (lazy FPU): the incoming process must trap (#NM) on
         * its first FPU/SSE use so fpu_handler saves the outgoing owner's
         * live registers and loads the incoming one's, instead of running
         * with another process's x87/SSE state (ARCH-01). */
        fpu_switch();
    }

    /* Reload per-thread %gs TLS base into the shared GDT_TLS_START slot.
     * No-op when next->gs_base == 0 (kernel-only threads and user threads
     * pre-rtld).  Required for multi-threaded user processes — without
     * this, every thread sees whichever thread set its TCB last, and the
     * resulting jemalloc/__free crashes look like fixed-address SEGVs. */
    i386_load_gs_for_thread(next);

    switch_to(prev, next);
}

void arch_set_kernel_stack(uintptr_t stack) {
    set_kernel_stack((uint32_t)stack);
}

// Thread exit wrapper - called when a thread's entry_point returns
static void thread_exit_wrapper(void) {
    if (current_thread) {
        current_thread->state = THREAD_ZOMBIE;
    }
    sched_yield();
    // Should never reach here, but halt if we do
    while(1) { __asm__ volatile("hlt"); }
}

void sched_init(void) {
    sched_init_generic();

    sched_smp_init(smp_get_cpu_count());

    /* Bootstrap the swapper (PID 0).  kmalloc is up by this point, so
     * proc_bootstrap_kernel allocates a real process_t and links it
     * into allproc / pid_hash[0].  No static array involved. */
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

    /* Ensure we start effectively in kernel pmap (already loaded but
     * helpful for consistency). */
    pmap_activate(kernel_process->pmap);
}

int sched_fork_thread(process_t *proc, void *parent_regs) {
    // Cast parent_regs to registers_t pointer
    // Cast parent_regs to registers_t pointer
    typedef struct {
        uint32_t gs;
        uint32_t fs, es;
        uint32_t ds;
        uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
        uint32_t int_no, err_code;
        uint32_t eip, cs, eflags, useresp, ss;
    } fork_regs_t;
    
    fork_regs_t *regs = (fork_regs_t *)parent_regs;
    
    thread_t *t = sched_alloc_thread(proc);
    if (!t) return -1;

    /* Inherit the parent thread's TLS base.  The GS *selector* is restored from
     * the trap frame below, but the per-thread GS *base* (loaded into the GDT
     * TLS slot by arch_switch_to) lives in thread_t.gs_base — a fresh child
     * thread has 0, so without this its first %gs-relative TLS access faults.
     * This is what broke any forked, threaded child (e.g. rpcbind's daemon). */
    if (current_thread)
        t->gs_base = current_thread->gs_base;


    /* Allocate 4 pages = 16 KiB contiguous kernel stack.  8 KiB
     * overflows: a deep network TX syscall path (sys_write -> tcp_send
     * -> ... -> rtl_xmit) can take a nested NIC IRQ that runs the whole
     * RX -> IP -> TCP input path on the same stack; the combined depth
     * scribbles past an 8 KiB stack into adjacent kernel heap.  Matches
     * the kthread stack size in kern/kthread.c. */
    void *kstack_base = pmm_alloc_contiguous(4);
    if (!kstack_base) {
        t->proc = NULL;
        t->tid = -1;
        t->state = THREAD_ZOMBIE;
        return -1;
    }

    // Stack is at top of these pages (16KB = 0x4000)
    /* uintptr_t, not uint32_t: rounding a pointer through a 32-bit integer
     * truncates it on any host where pointers are wider, which is exactly
     * what the tests/ host build is.  Identical on i386, where uintptr_t
     * is 32 bits. */
    uint32_t *kstack = (uint32_t *)((uintptr_t)kstack_base + 0x4000);
    t->kstack_base = (uintptr_t)kstack_base;
    t->kstack_top = (uintptr_t)kstack;
    t->kstack_units = 4;
    t->kstack_type = THREAD_KSTACK_PMM_CONTIG;
    t->kstack_owned = 1;

    // Build IRET frame on child's kernel stack
    // push in reverse order (stack grows down)
    kstack--; *kstack = regs->ss;         // SS
    kstack--; *kstack = regs->useresp;    // User ESP
    kstack--; *kstack = regs->eflags;     // EFLAGS
    kstack--; *kstack = regs->cs;         // CS
    kstack--; *kstack = regs->eip;        // EIP - resume at same instruction
    kstack--; *kstack = regs->err_code;   // Error code
    kstack--; *kstack = regs->int_no;     // Int number
    
    // Push pusha registers (child gets EAX = 0 = fork return value)
    kstack--; *kstack = 0;                // EAX = 0 (child's return value!)
    kstack--; *kstack = regs->ecx;
    kstack--; *kstack = regs->edx;
    kstack--; *kstack = regs->ebx;
    kstack--; *kstack = regs->esp;        // Original ESP (ignored by popa)
    kstack--; *kstack = regs->ebp;
    kstack--; *kstack = regs->esi;
    kstack--; *kstack = regs->edi;
    
    // Push Segment Registers (DS, ES, FS, GS)
    kstack--; *kstack = regs->ds;
    kstack--; *kstack = regs->es;
    kstack--; *kstack = regs->fs;
    kstack--; *kstack = regs->gs;
    
    // switch_to expects: [EBX, ESI, EDI, EBP, RetAddr] at stack top
    // RetAddr is at highest address, EBX is at lowest (where ESP will point)
    uint32_t fcr_addr = (uint32_t)fork_child_return;
    kstack--; *kstack = fcr_addr; // Return address for switch_to
    
    kstack--; *kstack = 0;  // EBP
    kstack--; *kstack = 0;  // EDI  
    kstack--; *kstack = 0;  // ESI
    kstack--; *kstack = 0;  // EBX
    
    t->kstack_ptr = (uintptr_t)kstack;
    t->instr_ptr = regs->eip;
    t->state = THREAD_READY; // Ready to be scheduled

    return proc->pid;
}

/*
 * sched_clone_thread - create a Linux clone(CLONE_THREAD) thread.
 *
 * Like sched_fork_thread, but the new thread joins *proc* (the caller's own
 * process, so it shares the address space, fds and signal state) instead of a
 * forked child, and it carries its own TLS base and CHILD_CLEARTID pointer.
 * The child resumes from the copied trap frame with EAX=0 on its new user
 * stack (already set in parent_regs->useresp by the caller) -- exactly the
 * clone(2) contract: the child "returns from the syscall" on child_stack.
 *
 * Returns the new thread's TID, or -1 on failure.
 */
int sched_clone_thread(process_t *proc, void *parent_regs, uint32_t tls_base,
                       int *clear_child_tid) {
    typedef struct {
        uint32_t gs;
        uint32_t fs, es;
        uint32_t ds;
        uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
        uint32_t int_no, err_code;
        uint32_t eip, cs, eflags, useresp, ss;
    } fork_regs_t;

    fork_regs_t *regs = (fork_regs_t *)parent_regs;

    thread_t *t = sched_alloc_thread(proc);
    if (!t) return -1;

    /* Per-thread TLS base supplied by clone(CLONE_SETTLS); arch_switch_to
     * reloads the GDT TLS slot from this on every switch-in. */
    t->gs_base = tls_base;
    /* CHILD_CLEARTID: the scheduler zeroes *clear_child_tid and futex-wakes
     * it when this thread becomes a zombie (sched_context_switch), which is
     * how pthread_join() observes thread exit. */
    t->exit_tid_ptr = clear_child_tid;


    void *kstack_base = pmm_alloc_contiguous(4);   /* 16 KiB */
    if (!kstack_base) {
        t->proc = NULL;
        t->tid = -1;
        t->state = THREAD_ZOMBIE;
        return -1;
    }

    /* uintptr_t, not uint32_t: rounding a pointer through a 32-bit integer
     * truncates it on any host where pointers are wider, which is exactly
     * what the tests/ host build is.  Identical on i386, where uintptr_t
     * is 32 bits. */
    uint32_t *kstack = (uint32_t *)((uintptr_t)kstack_base + 0x4000);
    t->kstack_base = (uintptr_t)kstack_base;
    t->kstack_top = (uintptr_t)kstack;
    t->kstack_units = 4;
    t->kstack_type = THREAD_KSTACK_PMM_CONTIG;
    t->kstack_owned = 1;

    /* Build the IRET frame the child resumes through (mirrors
     * sched_fork_thread): same user context as the parent at the clone
     * call site, but EAX=0 and the new user stack. */
    kstack--; *kstack = regs->ss;
    kstack--; *kstack = regs->useresp;     /* child user stack */
    kstack--; *kstack = regs->eflags;
    kstack--; *kstack = regs->cs;
    kstack--; *kstack = regs->eip;
    kstack--; *kstack = regs->err_code;
    kstack--; *kstack = regs->int_no;

    kstack--; *kstack = 0;                  /* EAX = 0 (clone child return) */
    kstack--; *kstack = regs->ecx;
    kstack--; *kstack = regs->edx;
    kstack--; *kstack = regs->ebx;
    kstack--; *kstack = regs->esp;
    kstack--; *kstack = regs->ebp;
    kstack--; *kstack = regs->esi;
    kstack--; *kstack = regs->edi;

    kstack--; *kstack = regs->ds;
    kstack--; *kstack = regs->es;
    kstack--; *kstack = regs->fs;
    kstack--; *kstack = regs->gs;

    kstack--; *kstack = (uint32_t)fork_child_return;
    kstack--; *kstack = 0;  /* EBP */
    kstack--; *kstack = 0;  /* EDI */
    kstack--; *kstack = 0;  /* ESI */
    kstack--; *kstack = 0;  /* EBX */

    t->kstack_ptr = (uintptr_t)kstack;
    t->instr_ptr = regs->eip;
    t->state = THREAD_READY;

    return t->tid;
}

thread_t *sched_create_thread(process_t *proc, void (*entry_point)(void*), void *stack, void *arg) {
    thread_t *t = sched_alloc_thread(proc);
    if (!t) return NULL;

    /* Branch on entry mode:
     *
     *   Kernel thread  (entry_point in kernel virtual range, >= 0xC0000000):
     *     entry_point is a plain kernel function reached by `ret`.  Set
     *     up the user-supplied stack as the kernel stack with
     *     [arg, exit_wrapper, entry_point, saved-regs] — this is the
     *     historical path and remains correct for kthreads (the swapper,
     *     bio sync, etc).
     *
     *   User thread    (entry_point in user range, < 0xC0000000):
     *     entry_point is user code (libpthread's __pthread_trampoline).
     *     Allocate a dedicated kernel stack (8KB) and arrange for
     *     switch_to's first `ret` to land in
     *     new_user_thread_trampoline (asm helper in isr.S), which
     *     builds the user IRET frame and switches to CPL=3.
     *     The user-supplied `stack` becomes the user-mode stack
     *     (handed to the trampoline, which adjusts it for cdecl). */
    int is_user = (uint32_t)(uintptr_t)entry_point < 0xC0000000U;

    if (!is_user) {
        uint32_t *stk = (uint32_t*)stack;
        t->kstack_top = (uintptr_t)stk;

        /*
         * Kernel threads must begin with interrupts ENABLED.  switch_to()
         * runs under the scheduler's intr_disable() and does not restore
         * EFLAGS, so a kthread would otherwise inherit IF=0 and run forever
         * with interrupts masked — fatal for any kthread that busy-polls
         * with a timer-tick-based timeout (e.g. usb_hid_poll_thread spinning
         * on get_uptime_ms(), which never advances because the tick IRQ is
         * masked → total boot hang).  Land switch_to's `ret` on a tiny
         * `sti; ret` trampoline that re-enables interrupts and then falls
         * through to entry_point — the kernel-side analogue of what
         * new_user_thread_trampoline does for user threads via its iret.
         */
        stk--; *stk = (uint32_t)(uintptr_t)arg;
        stk--; *stk = (uint32_t)(uintptr_t)thread_exit_wrapper;
        stk--; *stk = (uint32_t)(uintptr_t)entry_point;
        stk--; *stk = (uint32_t)(uintptr_t)&new_kernel_thread_trampoline;
        stk--; *stk = 0; // ebp
        stk--; *stk = 0; // edi
        stk--; *stk = 0; // esi
        stk--; *stk = 0; // ebx

        t->kstack_ptr = (uintptr_t)stk;
        t->instr_ptr  = (uintptr_t)entry_point;
        t->state      = THREAD_READY;
        return t;
    }

    /* User thread path.  16 KiB kernel stack — see sched_fork_process
     * for why 8 KiB overflows under the nested TX/RX network path. */

    void *kstack_base = pmm_alloc_contiguous(4);   /* 16 KiB */
    if (!kstack_base) {
        t->proc  = NULL;
        t->tid   = -1;
        t->state = THREAD_ZOMBIE;
        return NULL;
    }
    /* uintptr_t, not uint32_t: rounding a pointer through a 32-bit integer
     * truncates it on any host where pointers are wider, which is exactly
     * what the tests/ host build is.  Identical on i386, where uintptr_t
     * is 32 bits. */
    uint32_t *kstack = (uint32_t *)((uintptr_t)kstack_base + 0x4000);
    t->kstack_base  = (uintptr_t)kstack_base;
    t->kstack_top   = (uintptr_t)kstack;
    t->kstack_units = 4;
    t->kstack_type  = THREAD_KSTACK_PMM_CONTIG;
    t->kstack_owned = 1;

    /* The trampoline (new_user_thread_trampoline, in isr.S) pops three
     * cdecl args off this stack: user_entry, user_stack, user_arg.
     * Below those, switch_to expects [ebx, esi, edi, ebp, ret_addr]. */
    kstack--; *kstack = (uint32_t)(uintptr_t)arg;          /* user_arg   */
    kstack--; *kstack = (uint32_t)(uintptr_t)stack;        /* user_stack */
    kstack--; *kstack = (uint32_t)(uintptr_t)entry_point;  /* user_entry */

    kstack--; *kstack = (uint32_t)(uintptr_t)&new_user_thread_trampoline; /* ret-addr */
    kstack--; *kstack = 0; /* ebp */
    kstack--; *kstack = 0; /* edi */
    kstack--; *kstack = 0; /* esi */
    kstack--; *kstack = 0; /* ebx */

    t->kstack_ptr = (uintptr_t)kstack;
    t->instr_ptr  = (uintptr_t)entry_point;
    t->state      = THREAD_READY;
    return t;
}
