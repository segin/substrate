#ifndef _SYS_PROC_H
#define _SYS_PROC_H

#include <stddef.h>
#include <stdint.h>
#include <sys/acct.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <sys/lock.h>

// Process States (BSD style)
#define SIDL   1 // Process being created by fork
#define SRUN   2 // Currently runnable
#define SSLEEP 3 // Sleeping on an address
#define SSTOP  4 // Process suspended/stopped
#define SZOMB  5 // Process exited but not reaped
#define SDYING 6 // Process is dying (in exit path)

// Process Flags (p_flag)
#define P_CONTINUED  0x0001  // Process has been continued (for WCONTINUED)
#define P_TRACED     0x0002  // Being traced (ptrace)
#define P_WAITED     0x0004  // Stopped state already reported
#define P_AUTOREAP   0x0008  // Zombie should be reaped asynchronously
#define P_SIGEXIT    0x0010  // Zombie status is signal/core encoded already

#define PROC_ITIMER_COUNT 3
#define PROC_CMDLINE_MAX  512

/*
 * POSIX.1b per-process interval timers (timer_create(2)).  A small fixed
 * table lives at the tail of process_t; the timer_t handed to userspace is
 * the slot index.  Expiry is evaluated against the kernel tick in
 * proc_ptimers_fire() using absolute nanoseconds in the timer's clock
 * domain, so overrun counts reflect real elapsed time rather than tick
 * granularity.  Timers are NOT inherited across fork() and are disarmed +
 * deleted on exec().
 */
#define POSIX_TIMER_MAX 32
struct posix_timer {
    uint8_t  used;            /* slot allocated by timer_create()          */
    uint8_t  armed;           /* it_value != 0 (counting toward next_ns)   */
    uint8_t  notify;          /* SIGEV_SIGNAL / SIGEV_NONE                 */
    uint8_t  sig_outstanding; /* a signal was generated, not yet accepted  */
    uint8_t  abs_real;        /* next_ns is an absolute CLOCK_REALTIME time
                               * (follows clock_settime); else it is in the
                               * clock-step-immune monotonic base           */
    int      signo;           /* signal delivered on expiry (SIGEV_SIGNAL) */
    int      clockid;         /* CLOCK_REALTIME / CLOCK_MONOTONIC          */
    union sigval value;       /* sigev_value (delivered as SI_TIMER value) */
    uint64_t next_ns;         /* absolute clock-ns of next expiry (0=off)  */
    uint64_t interval_ns;     /* periodic reload in ns (0 = one-shot)      */
    int      overrun;         /* overrun count latched at last delivery    */
    int      overrun_pending; /* overruns accrued since signal generation  */
};

typedef uint8_t process_state_t;

// Forward declarations
struct personality;
struct fs_node;
typedef struct fs_node fs_node_t;
struct runqueue;
struct file;
struct thread;
typedef struct file file_t;
struct pmap;
struct pgrp;
struct session;
struct registers;
struct mutex;

#define MAX_FD 4096
#define FD_BITMAP_WORDS (MAX_FD / 32)

/* The allocated-fd and close-on-exec sets are word-array bitmaps so
 * they scale past 32 descriptors.  Use these accessors everywhere
 * instead of `bm & (1U << fd)` — that only works for fd < 32. */
static inline int fdset_test(const uint32_t *bm, int fd) {
    return (bm[(unsigned)fd >> 5] >> ((unsigned)fd & 31)) & 1u;
}
static inline void fdset_set(uint32_t *bm, int fd) {
    bm[(unsigned)fd >> 5] |= 1u << ((unsigned)fd & 31);
}
static inline void fdset_clear(uint32_t *bm, int fd) {
    bm[(unsigned)fd >> 5] &= ~(1u << ((unsigned)fd & 31));
}

// FPU Context Structure
typedef struct {
    uint8_t fpu_state[512] __attribute__((aligned(16)));  // FXSAVE area (512 bytes, 16-byte aligned)
    int fpu_used;                                          // Flag: has this process used FPU?
} fpu_context_t;

// Process Structure
typedef struct process {
    int pid;
    int ppid; // Parent PID
    int exit_code;
    struct pgrp *p_pgrp;       // Process group (NULL = none)
    struct process *p_pgrp_link; // Next process in same pgrp (linked list)
    int perso_id; // Personality ID (PERS_NATIVE, PERS_LINUX, etc.)
    struct personality *pers; // Pointer to personality
    file_t *fds[MAX_FD]; // File Descriptor Table
    int next_fd;         // Hint for next free FD
    uint32_t fd_bitmap[FD_BITMAP_WORDS];  // Bitmap of allocated FDs
    uint32_t fd_cloexec[FD_BITMAP_WORDS]; // Descriptors closed on successful exec
    fs_node_t *root_node; // Per-process root (for chroot)
    
    // Signals
    struct sigaction sig_actions[NSIG];
    void *linux_sig_restorer[NSIG];
    uint32_t sig_catch;   // Bitmask: signals with custom handlers (not SIG_DFL/SIG_IGN)
    uint32_t sig_ignore;  // Bitmask: signals set to SIG_IGN
    /* sigqueue(2) payloads.  Substrate's pending set is a bitmask (signals do
     * not truly queue), so one union sigval slot per signal number carries the
     * value of the most recent queued instance; sig_qpend marks which slots
     * hold a value to deliver as siginfo.si_value with si_code == SI_QUEUE. */
    uint32_t sig_qpend;
    union sigval sig_qval[NSIG];
    
    // Process State
    uint8_t state; // process_state_t
    uint8_t p_xsig; // Signal that stopped this process (for WSTOPSIG)
    uint16_t p_flag; // Process flags (P_CONTINUED, P_TRACED, etc.)
    
    // Hierarchy
    struct process *p_parent;     // Parent process
    struct process *p_children;   // Head of children list
    struct process *p_sibling;    // Next sibling (in parent's children list)
    struct thread *vfork_waiter;  // Parent thread blocked in vfork()

    // ptrace(2): the tracing process (NULL if untraced; see P_TRACED).  For
    // PTRACE_ATTACH the tracee is reparented onto the tracer so wait4() reports
    // its stops; p_oparent remembers the real parent to restore on detach/exit.
    struct process *p_tracer;
    struct process *p_oparent;
    
    // Accounting & Credentials
    char comm[AC_COMM_LEN];
    uint32_t start_time;
    struct rusage rusage;         // Resource usage stats
    struct rusage rusage_children;// Child resource usage stats (accumulated)
    
    uint32_t utime; // Legacy ticks (keep for now, or sync with rusage)
    uint32_t stime;
    uint32_t uid;
    uint32_t gid;
    uint32_t euid;
    uint32_t egid;
    uint32_t suid;
    uint32_t sgid;
    uint16_t umask;
    uint8_t  ac_flag;
    uint8_t  is_kernel_task; // 1 if kernel thread, 0 if user process
    uint8_t  bitness;        // Process execution mode (16/32/64)
    struct rlimit rlimits[RLIM_NLIMITS];
    /* RLIMIT_MEMLOCK soft/hard limit, tracked directly (rlimits[] is only
     * sized for RLIMIT_CORE).  RLIM_INFINITY == "unset/no limit". */
    rlim_t   rlim_memlock_cur;
    rlim_t   rlim_memlock_max;
    uint32_t mlockall_flags;   /* MCL_CURRENT/MCL_FUTURE from mlockall(2) */

    struct tty *tty;      // Controlling Terminal
    fs_node_t *cwd_node; // Current working directory
    char exec_path[256];
    char cwd_path[256];
    uint16_t cmdline_tail_len;
    uint16_t cmdline_tail_argc;
    char cmdline_tail[PROC_CMDLINE_MAX];
    /* Live argv region on the user stack, captured by exec.  When set,
     * /proc/<pid>/cmdline reads [arg_start, arg_end) straight out of the
     * process's address space, so it reflects in-place argv rewrites
     * (setproctitle) and is not capped to cmdline_tail's size.  0 = unset
     * (kernel thread / pre-exec) -> fall back to the cmdline_tail snapshot. */
    uint32_t arg_start;
    uint32_t arg_end;

    // Memory management
    uint32_t brk;        // Program break (heap end)
    uint32_t brk_start;  // Initial program break
    uint32_t brk_committed; // Pages of heap charged to the strict commit
                            // counter (vm_commit_charge); released on brk
                            // shrink, exec, and process exit.
    uint64_t itimer_value_ticks[PROC_ITIMER_COUNT];
    uint64_t itimer_interval_ticks[PROC_ITIMER_COUNT];
    spinlock_t itimer_lock;
    
    // FPU Context
    fpu_context_t fpu_ctx;
    
    // mmap regions
    struct vm_area *vm_areas;  // Linked list of mapped regions
    struct vm_map *vm_map;    // Substrate VM Map
    struct pmap *pmap;         // Pmap (Page Table) handle
    
    // LDT support
    void        *ldt;             // Pointer to LDT entries (gdt_entry_t*)
    int          ldt_entry_count; // Number of entries in LDT
    uint8_t      ldt_is_uma;      // Nonzero if the active LDT came from the UMA LDT zone
    spinlock_t   ldt_lock;        // Protects LDT pointer/count replacement

    /* Supplementary group list (setgroups/initgroups).  Placed at
     * the END of proc_t deliberately: putting it earlier shifted
     * every subsequent field's offset and exposed a regression
     * elsewhere (still under investigation).  Keep this at the
     * tail until that's understood. */
    uint32_t supp_groups[32];
    int      n_supp_groups;

    /* allproc list link + pid_hash chain link.  Every live process is
     * on the allproc singly-linked list and on exactly one pid_hash
     * bucket.  Both protected by pm.c's pid_lock. */
    struct process *p_allproc_next;
    struct process *p_pidhash_next;

    /* User stack bounds for demand-paged grow-down.  exec maps only a
     * small region at ustack_top; a not-present fault anywhere in
     * [ustack_limit, ustack_top) maps a fresh page on the fly, so a
     * process only ever costs the stack it actually touches.  Kept at
     * the end of the struct so adding them doesn't shift the offset
     * of any field touched by offset-hardcoded (asm) code. */
    uintptr_t   ustack_top;    // highest stack address (exclusive)
    uintptr_t   ustack_limit;  // lowest address the stack may grow to

    /* POSIX scheduling policy + priority (sched_setscheduler(2) /
     * sched_setparam(2)).  Stored and reported for conformance; the
     * substrate MLFQ/SMP scheduler is not perturbed by these values.
     * Zero-initialized process => SCHED_OTHER (0), priority 0, which is
     * the correct POSIX default.  Inherited by fork() in proc_create().
     * Kept at the END of the struct so the offset of no asm-referenced
     * field shifts (see the same note on the fields above). */
    int         sched_policy;       // POSIX_SCHED_* (see <sys/sched.h>)
    int         sched_rt_priority;  // sched_param.sched_priority

    /* POSIX.1b per-process timers (timer_create(2)).  Kept at the END of
     * the struct — see the offset-stability note above.  Guarded by the
     * existing itimer_lock; the tick fast-paths on n_ptimers_armed == 0.
     * sig_timer_pend marks signal numbers whose pending instance carries an
     * SI_TIMER si_value in sig_qval[] (parallel to sig_qpend for SI_QUEUE). */
    struct posix_timer ptimers[POSIX_TIMER_MAX];
    uint8_t     n_ptimers;          // allocated (used) timer slots
    uint8_t     n_ptimers_armed;    // armed timer slots (tick fast-path guard)
    uint32_t    sig_timer_pend;     // bitmask: signals pending as SI_TIMER

    /* POSIX.1b real-time signal queue.  A signal in [SIGRTMIN,SIGRTMAX]
     * sent via sigqueue(2) or generated by kill()/psignal() enqueues a
     * distinct instance here instead of collapsing into the sig_pending
     * bitmask.  signal_handle_pending() dequeues exactly one instance per
     * handler invocation (lowest signo first via the pending scan, FIFO
     * within a signo via rt_seq) and clears the per-thread pending bit
     * only when this signo's queue drains — so a queued RT signal fires
     * once per enqueue: no coalescing, no over-delivery.  Bounded by
     * RTSIG_QUEUE_MAX (sigqueue returns EAGAIN when full).  Guarded by
     * rtsig_lock with local IRQs disabled (psignal may run from IRQ
     * context, e.g. a TTY ^C).  Timers keep their own sig_timer_pend /
     * sig_qval path and do NOT use this queue.  Kept at the END of the
     * struct — see the offset-stability note above. */
    struct rtsig_entry rtsig_q[RTSIG_QUEUE_MAX];
    uint32_t    rtsig_seq;          // next FIFO sequence number
    uint16_t    rtsig_count;        // occupied slots (0..RTSIG_QUEUE_MAX)
    spinlock_t  rtsig_lock;         // guards rtsig_q/seq/count (IRQ-safe)

    // Resource limits, FDs, etc. would go here
} process_t;

// Thread State
typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_ZOMBIE,
    THREAD_STOPPED
} thread_state_t;

typedef enum {
    THREAD_KSTACK_NONE = 0,
    THREAD_KSTACK_PMM_BLOCK,
    THREAD_KSTACK_PMM_CONTIG,
    THREAD_KSTACK_KMALLOC
} thread_kstack_type_t;

// Scheduling Classes
typedef enum {
    SCHED_REALTIME,
    SCHED_TIMESHARE,
    SCHED_IDLE
} sched_class_t;

// Architecture Independent Thread Control Block
// Arch-specific context (registers) should be stored in 'context' or similar, 
// but for simplicity in this prototype we use generic names.
typedef struct thread {
    int tid;
    process_t *proc; // Owner process
    
    // CPU Context (Abstracted)
    uintptr_t kstack_ptr; // Kernel Stack Pointer (ESP/RSP)
    uintptr_t kstack_top; // Top of Kernel Stack (for TSS esp0)
    uintptr_t kstack_base; // Base of owned kernel stack allocation
    uintptr_t instr_ptr;  // Instruction Pointer (EIP/RIP) - for context switching
    uint32_t  kstack_units; // Pages for PMM stacks, bytes for kmalloc stacks
    uint8_t   kstack_type; // thread_kstack_type_t
    uint8_t   kstack_owned; // Nonzero if the scheduler owns the kernel stack

    /* Per-thread %gs TLS base (FreeBSD/Linux i386).  Stored here because the
     * GDT_TLS_START slot is shared across all threads — without per-context
     * reload on switch, the most recent thread's TCB would be visible to all
     * others, and stale TCBs would have user-mode TLS reads return garbage
     * (manifesting as SEGV in libc's TLS-relative loads — e.g. jemalloc
     * __free reads %gs:0 which becomes 0 when the slot is empty). */
    uint32_t  gs_base;

    // Scheduling - Basic
    int           priority;
    int           base_priority;
    sched_class_t sched_class;
    
    // Scheduling - Interactivity (ULE-style)
    uint32_t      sleep_time;     // Accumulated sleep/wait time (ticks)
    uint32_t      run_time;       // Accumulated CPU time this epoch (ticks)
    int16_t       interactivity;  // Interactivity score (-128 to +127), positive = interactive
    uint16_t      time_slice;     // Remaining time slice (ticks)
    uint16_t      time_slice_max; // Full time slice for this priority
    uint32_t      flags;          // Thread flags
    struct mutex *held_mutexes;   // Sleep mutexes currently owned by this thread
    
#define THREAD_F_INTERRUPTIBLE 0x0001 // Sleep is interruptible by signals
#define THREAD_F_NO_PREEMPT    0x0002 // Suppress timer-driven reschedule
#define THREAD_F_WAKE_PENDING  0x0004 // thr_wake() seen before thr_suspend() — latched

    // Scheduling - Runqueue linkage
    struct thread *rq_next;       // Next in runqueue level
    struct thread *rq_prev;       // Prev in runqueue level
    struct runqueue *current_queue; // The runqueue this thread is currently on
    uint32_t       cpu_affinity;  // CPU affinity mask (bitmask)
    int16_t        bound_cpu;     // Hard CPU binding (-1 = floating)
    int16_t        exec_saved_bound_cpu; // Saved binding across exec pin window
    uint8_t        on_runqueue;   // Is thread currently on a runqueue?
    uint8_t        needs_resched; // Reschedule requested (timer in kernel mode, or IPI)
    uint8_t        exec_pin_active; // Exec path temporarily pinned this thread
    uint8_t        exec_saved_no_preempt; // Preserve preempt state across exec pin
    uint8_t        vfs_symlink_depth; // Current symlink-follow recursion depth
    uint32_t       preempt_count; // Non-preemptible nesting depth (spinlocks).
                                  // Kernel preemption only fires when 0.
    
    void         *wait_chan; // Channel thread is sleeping on
    const char   *wait_reason; // Description of wait event

    /* Currently-active file for read / poll on this thread.  Drivers
     * that need per-fd state (f_offset, f_flag) but only get an
     * fs_node_t* in their callback can consult this, set by
     * sys_read / kern_poll across the driver invocation.  The
     * canonical motivating case is the input_subsys: its global
     * event queue needs to know each reader's current sequence to
     * decide POLLIN vs nothing, and each reader's O_NONBLOCK flag
     * to decide block-vs-EAGAIN.  Cleared back to NULL by the
     * caller when the driver returns so a deeper-stack driver
     * call doesn't see a stale outer-call file. */
    struct file  *io_file;

    // Sleep Timeout
    uint64_t      sleep_expiry; // Absolute tick count when sleep expires (0 = none)
    int           sleep_status; // Return status of sleep (e.g., -ETIMEDOUT)
    uint32_t      futex_bitset; // FUTEX_WAIT_BITSET wait mask (FUTEX_WAKE_BITSET filter)

    // Signals
    uint32_t      sig_pending;
    uint32_t      sig_mask;
    /* sigsuspend stashes the pre-suspend mask here.  When set, the
     * next signal_handle_pending pass uses this value as the "mask
     * to restore" in the signal frame (so sigreturn restores the
     * original mask) instead of the temporary suspend-mask that's
     * live in sig_mask while a handler runs.  sig_mask_suspend_active
     * gates the field — sig_mask_suspend may legitimately be 0.       */
    uint32_t      sig_mask_suspend;
    uint8_t       sig_mask_suspend_active;
    stack_t       sig_alt_stack;
    uint8_t       sig_on_stack;   // Nonzero if executing on alternate signal stack
    
    // Syscall restart support (for SA_RESTART)
    uint8_t       in_syscall;     // Nonzero if thread is in a syscall
    uint8_t       frame_replaced; // Set by sigreturn/rt_sigreturn: the trapframe
                                  // now IS the restored user context; the syscall
                                  // dispatcher must not write eax/edx/eflags back
                                  // over it (the EDX writeback was clobbering a
                                  // live user register after every SIGALRM)
    uint32_t      syscall_num;    // Syscall number for restart
    uint32_t      syscall_orig_eax; // Original EAX for restart
    
    // Trap signal info (for SA_SIGINFO from trapsignal)
    int           trap_signo;     // Signal number from trap
    int           trap_code;      // Trap-specific code (si_code)
    uintptr_t     trap_addr;      // Fault address for siginfo_t.si_addr when applicable
    
    // Robust futex list (for owner death cleanup)
    struct robust_list_head *robust_list;
    size_t                   robust_list_len;
    
    // Fault Recovery (copyin/copyout)
    uintptr_t                on_fault;
    /*
     * Recursive-fault breaker for the trap handler — when vm_fault
     * keeps claiming SUCCESS at the same EIP+CR2 but the access keeps
     * re-faulting, fall through to on_fault rather than spin forever.
     * Reset on every on_fault recovery so the next copyin starts
     * fresh.
     */
    uint32_t                 fault_loop_eip;
    uint32_t                 fault_loop_cr2;
    uint32_t                 fault_loop_count;

    // Exec recursion tracking (shebang scripts)
    int                      script_depth;

    /*
     * Personality-allocated args that must outlive the syscall layer because
     * a successful exec never returns through the caller's frame.  Pushed by
     * personalities that kmalloc argv/envp from segmented user memory (ELKS),
     * drained by the format handler immediately before the userspace jump,
     * or by kern_execve on failure.  Keep small — only the vector heads need
     * tracking; per-string frees happen inside the registered free_fn.
     */
#define EXEC_CLEANUP_MAX 4
    struct {
        void (*free_fn)(void *);
        void *ptr;
    } exec_cleanup[EXEC_CLEANUP_MAX];
    uint8_t exec_cleanup_count;

    // Syscall registers (for fork/vfork)
    void *syscall_regs;

    // ptrace(2): saved user-mode trapframe (registers_t*) captured when this
    // thread stops in signal_handle_pending(), so a tracer's PTRACE_GETREGS /
    // SETREGS can read and write the registers it will resume with.
    void *user_frame;

    // Thread exit status for join/exit
    void *retval;
    // FreeBSD-style exit notification
    int *exit_tid_ptr;

    /* Thread name (FreeBSD thr_set_name).  Visible in ps -L and gdb's
     * `info threads`.  NUL-terminated.  Empty string = unnamed. */
    char name[16];

    thread_state_t state;

    /*
     * `next` is the in-queue link used by sleepq.c / turnstile.c to
     * chain a thread onto whichever wait queue it's currently parked
     * on.  Don't reuse it for the registry — see t_allthread_next /
     * t_tidhash_next below for the kernel-wide list / hash links.
     */
    struct thread *next;

    /*
     * Registry links — protected by sched.c's tid_lock.  Every live
     * thread is on `allthread` exactly once and in one tid_hash[]
     * bucket exactly once.
     */
    struct thread *t_allthread_next;
    struct thread *t_tidhash_next;

    /* FreeBSD initial-thread bootstrap pointer (a zeroed, mapped placeholder
     * "struct pthread").  exec records it for FreeBSD images; the sysarch
     * I386_SET_GSBASE handler injects it into the freshly installed TCB's
     * tcb_thread slot (%gs:8) when that slot is NULL, so libthr's early calls
     * (e.g. __pthread_cleanup_push_imp, which dereferences curthread at
     * +0x188) see a valid curthread before libthr's own _thr_init runs.
     * Zero for non-FreeBSD threads.  Kept at the END of the struct: adding a
     * field earlier shifts every later field's offset and breaks the
     * offset-hardcoded asm paths (see the same note on process_t). */
    uint32_t  fbsd_init_curthread;

    /* Scratch describing the queued RT-signal instance currently being
     * delivered.  signal_handle_pending() sets these when it dequeues an
     * instance from the process rtsig_q[]; the arch populate_siginfo()
     * reads them to fill the SA_SIGINFO frame's si_value/si_code.
     * rtsig_deliver_active gates the pair and is cleared at the top of
     * every signal_handle_pending() pass.  Kept at the END of thread_t so
     * no offset-hardcoded asm field shifts (see note on process_t). */
    uint8_t      rtsig_deliver_active;
    int          rtsig_deliver_code;
    union sigval rtsig_deliver_value;
    int          rtsig_deliver_pid;   /* sender pid of the delivered RT instance  */
    uint32_t     rtsig_deliver_uid;   /* sender uid of the delivered RT instance  */

    /* Set of signals this thread is synchronously waiting for in
     * sigwait(2)/sigtimedwait(2) (0 = not in a synchronous wait).  A signal
     * posted to a thread with a matching bit here must wake it even when the
     * signal is masked in sig_mask — the whole point of sigwait is to accept
     * a normally-blocked signal.  Without this, psignal()/thr_kill() only
     * wake a blocked thread whose signal is UNMASKED, so a sigtimedwait()
     * waiter (which masks the very signals it awaits) slept the full timeout
     * instead of returning on arrival.  Kept at the END of thread_t — see the
     * offset-stability note above. */
    uint32_t     sig_wait_mask;

    /* CFS-style virtual runtime for weighted fair-share scheduling of the
     * SCHED_TIMESHARE class.  Each tick a running timeshare thread accrues
     * vruntime scaled inversely by its nice weight, so a niced-down thread's
     * vruntime climbs faster and it is picked less often -- CPU share ends up
     * proportional to weight, and nobody starves because the pick always
     * chooses the lowest vruntime.  Kept at the END of thread_t -- see the
     * offset-stability note above. */
    uint64_t     vruntime;
} thread_t;

// Globals
extern thread_t *current_thread;
extern process_t *current_process;

// File Descriptor Management
int  proc_alloc_fd(process_t *p);
void proc_set_fd(process_t *p, int fd, file_t *f);
void proc_clear_fd(process_t *p, int fd);

extern mutex_t proctree_lock;

void rusage_add_tick(struct process *p, int is_usermode);

/* ptrace(2) support (sys/kern/ptrace.c, sys/kern/signal.c). */
struct registers;
void  signal_resume_process_threads(struct process *p);
void *ptrace_user_frame(struct process *p);
void  ptrace_exec_stop(struct registers *frame);

#endif
