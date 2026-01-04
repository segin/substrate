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
    
    fs_node_t *tty;      // Controlling Terminal
    fs_node_t *cwd_node; // Current working directory
    
    // Memory management
    uint32_t brk;        // Program break (heap end)
    uint32_t brk_start;  // Initial program break
    
    // FPU Context
    fpu_context_t fpu_ctx;
    
    // mmap regions
    struct vm_area *vm_areas;  // Linked list of mapped regions
    struct vm_map *vm_map;    // TestUnix VM Map
    struct pmap *pmap;         // Pmap (Page Table) handle
    
    // Resource limits, FDs, etc. would go here
} process_t;

// Thread State
typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_ZOMBIE
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
    
    // Scheduling
    int           priority;
    int           base_priority;
    sched_class_t sched_class;
    
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
