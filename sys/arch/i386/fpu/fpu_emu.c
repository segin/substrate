#include "fpu_emu.h"
#include "../idt.h"
#include "../io.h"
#include "../../../kern/console.h"
#include <sys/proc.h>

extern void isr7(void);
extern process_t *current_process;

// Save FPU context for a process
void fpu_save_context(struct process *p) {
    if (!p) return;
    
#ifndef HOST_TEST
    // Use FXSAVE to save FPU/SSE state
    __asm__ volatile("fxsave %0" : "=m"(p->fpu_ctx.fpu_state));
#endif
}

// Restore FPU context for a process
void fpu_restore_context(struct process *p) {
    if (!p) return;
    
#ifndef HOST_TEST
    // Use FXRSTOR to restore FPU/SSE state
    __asm__ volatile("fxrstor %0" : : "m"(p->fpu_ctx.fpu_state));
#endif
}

// FPU Device Not Available Exception (Interrupt 7)
void fpu_handler(registers_t *regs) {
    (void)regs;
#ifndef HOST_TEST
    // Clear TS bit in CR0 to allow FPU access
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~0x08; // Clear TS
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
#endif
    
    // Restore FPU state for current process (lazy FPU switching)
    if (current_process) {
        if (!current_process->fpu_ctx.fpu_used) {
            // First use of FPU by this process - initialize it
#ifndef HOST_TEST
            __asm__ volatile("fninit");
#endif
            current_process->fpu_ctx.fpu_used = 1;
        } else {
            // Restore previously saved FPU state
            fpu_restore_context(current_process);
        }
    }
    
    // Re-executing the instruction will now work since FPU is enabled
}

void fpu_init(void) {
#ifndef HOST_TEST
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    
    // Set MP (Monitor Coprocessor) bit
    cr0 |= 0x02;
    
    // Set NE (Numeric Error)
    cr0 |= 0x20;
    
    // Set TS (Task Switched) to enable lazy FPU switching
    cr0 |= 0x08;
    
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
#endif
    
    // Register INT 7 handler
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    
#ifndef HOST_TEST
    // Reset FPU
    __asm__ volatile("fninit");
#endif
}
