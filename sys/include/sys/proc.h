#ifndef _SYS_PROC_H
#define _SYS_PROC_H

#include <stdint.h>
#include <sys/acct.h>
#include <sys/signal.h>

// Forward declarations
struct personality;
struct fs_node;
typedef struct fs_node fs_node_t;
struct file;
typedef struct file file_t;
struct pmap;

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
    int pgrp;     // Process Group ID
    int session;  // Session ID
    struct personality *pers;
    file_t *fds[MAX_FD]; // File Descriptor Table
    fs_node_t *root_node; // Per-process root (for chroot)
    
    // Signals
    struct sigaction sig_actions[NSIG];
    
    // Accounting & Credentials
    char comm[AC_COMM_LEN];
    uint32_t start_time;
    uint32_t utime;
    uint32_t stime;
    uint32_t uid;
    uint32_t gid;
    uint8_t  ac_flag;
    uint8_t  is_kernel_task; // 1 if kernel thread, 0 if user process
    
    fs_node_t *tty;      // Controlling Terminal
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
    
    // Scheduling - Runqueue linkage
    struct thread *rq_next;       // Next in runqueue level
    struct thread *rq_prev;       // Prev in runqueue level
    uint32_t       cpu_affinity;  // CPU affinity mask (bitmask)
    uint8_t        on_runqueue;   // Is thread currently on a runqueue?
    uint8_t        needs_resched; // Set by IPI to trigger reschedule
    
    void         *wait_chan; // Channel thread is sleeping on
    const char   *wait_reason; // Description of wait event
    
    // Signals
    uint32_t      sig_pending;
    uint32_t      sig_mask;
    
    thread_state_t state;
    struct thread *next;
} thread_t;

// Globals
extern thread_t *current_thread;
extern process_t *current_process;
extern process_t processes[];

#endif
