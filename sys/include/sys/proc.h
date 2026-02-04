#ifndef _SYS_PROC_H
#define _SYS_PROC_H

#include <stdint.h>
#include <sys/acct.h>
#include <sys/signal.h>
#include <sys/resource.h>

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


typedef uint8_t process_state_t;

// Forward declarations
struct personality;
struct fs_node;
typedef struct fs_node fs_node_t;
struct file;
typedef struct file file_t;
struct pmap;
struct pgrp;
struct session;
struct registers;

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
    struct personality *pers; // Pointer to personality definition (internal use)
    int perso_id; // Stable personality ID (user visible)
    file_t *fds[MAX_FD]; // File Descriptor Table
    int next_fd;         // Hint for next free FD
    fs_node_t *root_node; // Per-process root (for chroot)
    
    // Signals
    struct sigaction sig_actions[NSIG];
    uint32_t sig_catch;   // Bitmask: signals with custom handlers (not SIG_DFL/SIG_IGN)
    uint32_t sig_ignore;  // Bitmask: signals set to SIG_IGN
    
    // Process State
    uint8_t state; // process_state_t
    uint16_t p_flag; // Process flags (P_CONTINUED, P_TRACED, etc.)
    
    // Hierarchy
    struct process *p_parent;     // Parent process
    struct process *p_children;   // Head of children list
    struct process *p_sibling;    // Next sibling (in parent's children list)
    
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
    uint8_t  ac_flag;
    uint8_t  is_kernel_task; // 1 if kernel thread, 0 if user process
    uint8_t  bitness;        // Process execution mode (16/32/64)
    
    struct tty *tty;      // Controlling Terminal
    fs_node_t *cwd_node; // Current working directory
    
    // Memory management
    uint32_t brk;        // Program break (heap end)
    uint32_t brk_start;  // Initial program break
    
    // FPU Context
    fpu_context_t fpu_ctx;
    
    // mmap regions
    struct vm_area *vm_areas;  // Linked list of mapped regions
    struct vm_map *vm_map;    // Substrate VM Map
    struct pmap *pmap;         // Pmap (Page Table) handle
    
    // LDT support
    void        *ldt;             // Pointer to LDT entries (gdt_entry_t*)
    int          ldt_entry_count; // Number of entries in LDT
    
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
    uintptr_t instr_ptr;  // Instruction Pointer (EIP/RIP) - for context switching
    
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
    
#define THREAD_F_INTERRUPTIBLE 0x0001 // Sleep is interruptible by signals
    
    // Scheduling - Runqueue linkage
    struct thread *rq_next;       // Next in runqueue level
    struct thread *rq_prev;       // Prev in runqueue level
    uint32_t       cpu_affinity;  // CPU affinity mask (bitmask)
    uint8_t        on_runqueue;   // Is thread currently on a runqueue?
    uint8_t        needs_resched; // Set by IPI to trigger reschedule
    
    void         *wait_chan; // Channel thread is sleeping on
    const char   *wait_reason; // Description of wait event
    
    uint64_t      sleep_expiry; // Absolute tick count for sleep timeout (0 = infinite)

    // Signals
    uint32_t      sig_pending;
    uint32_t      sig_mask;
    stack_t       sig_alt_stack;
    uint8_t       sig_on_stack;   // Nonzero if executing on alternate signal stack
    
    // Syscall restart support (for SA_RESTART)
    uint8_t       in_syscall;     // Nonzero if thread is in a syscall
    uint32_t      syscall_num;    // Syscall number for restart
    uint32_t      syscall_orig_eax; // Original EAX for restart
    
    // Trap signal info (for SA_SIGINFO from trapsignal)
    int           trap_signo;     // Signal number from trap
    int           trap_code;      // Trap-specific code (si_code)
    
    // Robust futex list (for owner death cleanup)
    struct robust_list_head *robust_list;
    size_t                   robust_list_len;
    
    // Fault Recovery (copyin/copyout)
    uintptr_t                on_fault;
    
    // Syscall registers (for fork/vfork)
    struct registers *syscall_regs;

    thread_state_t state;
    struct thread *next;
} thread_t;

// Globals
extern thread_t *current_thread;
extern process_t *current_process;
extern process_t processes[];

#endif
