#include <arch/i386/fpu/fpu_emu.h>
#include <arch/i386/idt.h>
#include <arch/i386/io.h>
#include <kern/console.h>
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

static int fpu_present = 0;

void fpu_init(void) {
#ifndef HOST_TEST
    // Detect FPU presence using CPUID or CR0 probing
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    
    // Clear EM (emulation) bit to test for FPU
    cr0 &= ~0x04;  // Clear EM
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    
    // Try FNINIT and check status word
    uint16_t status = 0x5A5A;
    __asm__ volatile("fninit");
    __asm__ volatile("fnstsw %0" : "=m"(status));
    
    if ((status & 0xFF) == 0) {
        // FPU detected!
        fpu_present = 1;
        kprint("FPU: Hardware x87 detected\n");
        
        // Configure CR0 for native FPU with lazy switching
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= 0x02;   // Set MP (Monitor Coprocessor)
        cr0 |= 0x20;   // Set NE (Numeric Error - use internal FPU error handling)
        cr0 &= ~0x04;  // Clear EM (no emulation needed)
        cr0 |= 0x08;   // Set TS (Task Switched) for lazy context switching
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
        
        // Initialize FPU to known state
        __asm__ volatile("fninit");
    } else {
        // No FPU - enable emulation
        fpu_present = 0;
        kprint("FPU: No hardware x87 detected (emulation mode)\n");
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= 0x04;   // Set EM (emulation)
        cr0 &= ~0x02;  // Clear MP
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    }
#endif
    
    // Register INT 7 handler for #NM (Device Not Available)
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
}

