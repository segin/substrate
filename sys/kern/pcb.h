/*
 * pcb.h - Process Control Block Refinement
 * 
 * Documents the thread/process separation for context switching.
 * The PCB defines data that must be saved/restored on context switch.
 */

#ifndef _KERN_PCB_H
#define _KERN_PCB_H

#include <stdint.h>

/*
 * Architecture-Independent Thread PCB
 * 
 * Contains minimal data needed for context switching.
 * Architecture-specific context is in arch/*/pcb.h
 */
typedef struct thread_pcb {
    // Saved stack pointer (kernel stack for user threads)
    uintptr_t sp;
    
    // Saved instruction pointer (for non-preemptive switch)
    uintptr_t ip;
    
    // Flags (e.g., IF for interrupt enable state)
    uint32_t flags;
    
    // FPU state saved? (for lazy FPU)
    uint8_t fpu_saved;
    
    // Thread is currently in kernel mode?
    uint8_t in_kernel;
    
    // Reserved for alignment
    uint8_t _pad[2];
} thread_pcb_t;

/*
 * i386-specific Thread Context
 * Saved in thread's kernel stack during context switch.
 */
typedef struct i386_context {
    // General purpose registers (pushad order)
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;  // Ignored by popad
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    
    // Segment registers (if needed)
    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;
    
    // Pushed by CPU on interrupt
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    
    // Pushed by CPU if privilege change
    uint32_t user_esp;
    uint32_t user_ss;
} i386_context_t;

/*
 * Thread/Process Separation Guidelines:
 * 
 * PROCESS (process_t):
 *   - PID, PPID, PGRP, Session
 *   - Address space (pmap, vm_map)
 *   - File descriptors
 *   - Signal handlers (sigaction array)
 *   - Credentials (uid, gid)
 *   - Controlling terminal
 *   - Resource limits
 *   - Accounting data
 * 
 * THREAD (thread_t):
 *   - TID (unique within process)
 *   - Owning process pointer
 *   - Scheduling state (priority, class)
 *   - Kernel/User stack pointers
 *   - CPU context (registers)
 *   - Signal mask (per-thread)
 *   - Wait channel (sleep/wakeup)
 *   - FPU context
 *   - Runqueue linkage
 * 
 * CONTEXT SWITCH saves/restores:
 *   1. General registers (via thread's kernel stack)
 *   2. Stack pointer (thread->kstack_ptr)
 *   3. FPU state (lazy - only if used)
 *   4. Segment registers (if different)
 *   5. TSS.esp0 (for syscall return)
 * 
 * ADDRESS SPACE SWITCH (if different process):
 *   1. CR3 (page directory)
 *   2. TLB flush (or PCID if available)
 */

// Context switch function prototype
void context_switch(thread_pcb_t *old, thread_pcb_t *new);

// Save context without switching
void context_save(thread_pcb_t *pcb);

// Restore context
void context_restore(thread_pcb_t *pcb);

#endif /* _KERN_PCB_H */
