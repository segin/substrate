#include <kern/sched.h>
#include <kern/time.h>
#include <sys/smp.h>
#include <pm/pm.h>
#include <sys/acct.h>
#include <exec/perso/personality.h>
#include <arch/i386/percpu.h>
#include <stddef.h>
#include <stdint.h>
#include "pmap.h"

// Arch-Specific Externs
extern void switch_to(thread_t *prev, thread_t *next);
extern void set_kernel_stack(uint32_t stack);
extern thread_t *current_thread; // Now defined in generic sched.c
extern fs_node_t *fs_root;

// Generic Allocation Helper
extern thread_t *sched_alloc_thread(process_t *proc);
extern void sched_init_generic(void);

#include <sys/ldt.h>

extern void i386_load_gs_for_thread(thread_t *t);

// Exposed to Generic Scheduler
void arch_switch_to(thread_t *prev, thread_t *next) {
    // Switch Address Space if needed
    if (next->proc && next->proc->pmap) {
        pmap_activate(next->proc->pmap);
    }

    // Switch LDT if needed
    if (next->proc != prev->proc) {
        ldt_activate(next->proc);
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

    // Initialize SMP scheduler with detected CPU count
    extern void sched_smp_init(int);
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
    extern char *strcpy(char *, const char *);
    strcpy(kernel_process->comm, "swapper");

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
    
    extern void *pmm_alloc_contiguous(size_t);
    // Allocate 2 pages = 8KB contiguous kernel stack
    void *kstack_base = pmm_alloc_contiguous(2);
    if (!kstack_base) {
        t->proc = NULL;
        t->tid = -1;
        t->state = THREAD_ZOMBIE;
        return -1;
    }
    
    // Stack is at top of these pages (8KB = 0x2000)
    uint32_t *kstack = (uint32_t *)((uint32_t)kstack_base + 0x2000);
    t->kstack_base = (uintptr_t)kstack_base;
    t->kstack_top = (uintptr_t)kstack;
    t->kstack_units = 2;
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
    extern void fork_child_return(void);
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

        stk--; *stk = (uint32_t)(uintptr_t)arg;
        stk--; *stk = (uint32_t)(uintptr_t)thread_exit_wrapper;
        stk--; *stk = (uint32_t)(uintptr_t)entry_point;
        stk--; *stk = 0; // ebp
        stk--; *stk = 0; // edi
        stk--; *stk = 0; // esi
        stk--; *stk = 0; // ebx

        t->kstack_ptr = (uintptr_t)stk;
        t->instr_ptr  = (uintptr_t)entry_point;
        t->state      = THREAD_READY;
        return t;
    }

    /* User thread path. */
    extern void *pmm_alloc_contiguous(size_t);
    void *kstack_base = pmm_alloc_contiguous(2);   /* 8 KiB */
    if (!kstack_base) {
        t->proc  = NULL;
        t->tid   = -1;
        t->state = THREAD_ZOMBIE;
        return NULL;
    }
    uint32_t *kstack = (uint32_t *)((uint32_t)kstack_base + 0x2000);
    t->kstack_base  = (uintptr_t)kstack_base;
    t->kstack_top   = (uintptr_t)kstack;
    t->kstack_units = 2;
    t->kstack_type  = THREAD_KSTACK_PMM_CONTIG;
    t->kstack_owned = 1;

    /* The trampoline (new_user_thread_trampoline, in isr.S) pops three
     * cdecl args off this stack: user_entry, user_stack, user_arg.
     * Below those, switch_to expects [ebx, esi, edi, ebp, ret_addr]. */
    kstack--; *kstack = (uint32_t)(uintptr_t)arg;          /* user_arg   */
    kstack--; *kstack = (uint32_t)(uintptr_t)stack;        /* user_stack */
    kstack--; *kstack = (uint32_t)(uintptr_t)entry_point;  /* user_entry */

    extern void new_user_thread_trampoline(void);
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
