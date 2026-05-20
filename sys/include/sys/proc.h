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

#define MAX_FD 32

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
    uint32_t fd_bitmap;  // Bitmap of allocated FDs
    uint32_t fd_cloexec; // Bitmap of descriptors closed on successful exec
    fs_node_t *root_node; // Per-process root (for chroot)
    
    // Signals
    struct sigaction sig_actions[NSIG];
    void *linux_sig_restorer[NSIG];
    uint32_t sig_catch;   // Bitmask: signals with custom handlers (not SIG_DFL/SIG_IGN)
    uint32_t sig_ignore;  // Bitmask: signals set to SIG_IGN
    
    // Process State
    uint8_t state; // process_state_t
    uint8_t p_xsig; // Signal that stopped this process (for WSTOPSIG)
    uint16_t p_flag; // Process flags (P_CONTINUED, P_TRACED, etc.)
    
    // Hierarchy
    struct process *p_parent;     // Parent process
    struct process *p_children;   // Head of children list
    struct process *p_sibling;    // Next sibling (in parent's children list)
    struct thread *vfork_waiter;  // Parent thread blocked in vfork()
    
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
    
    struct tty *tty;      // Controlling Terminal
    fs_node_t *cwd_node; // Current working directory
    char exec_path[256];
    char cwd_path[256];
    uint16_t cmdline_tail_len;
    uint16_t cmdline_tail_argc;
    char cmdline_tail[PROC_CMDLINE_MAX];
    
    // Memory management
    uint32_t brk;        // Program break (heap end)
    uint32_t brk_start;  // Initial program break
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
    uint8_t        needs_resched; // Set by IPI to trigger reschedule
    uint8_t        exec_pin_active; // Exec path temporarily pinned this thread
    uint8_t        exec_saved_no_preempt; // Preserve preempt state across exec pin
    uint8_t        vfs_symlink_depth; // Current symlink-follow recursion depth
    
    void         *wait_chan; // Channel thread is sleeping on
    const char   *wait_reason; // Description of wait event
    
    // Sleep Timeout
    uint64_t      sleep_expiry; // Absolute tick count when sleep expires (0 = none)
    int           sleep_status; // Return status of sleep (e.g., -ETIMEDOUT)

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

#endif
