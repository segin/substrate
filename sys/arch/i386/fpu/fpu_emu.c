#include <sys/proc.h>
#include <kern/console.h>
#include <arch/i386/cpu.h>
#include <arch/i386/idt.h>
#include <arch/i386/fpu/fpu_emu.h>
#include <arch/x86-common/io.h>
static int fpu_use_fxsave = 0;

/*
 * Lazy-FPU owner: the process whose x87/SSE register state is currently live
 * in the hardware.  NULL means the registers belong to no live process (fresh
 * boot, or the owner exited).  The invariant that makes lazy save/restore
 * correct is: whenever a *different* process is scheduled, CR0.TS is re-armed
 * (fpu_switch below) so that process traps (#NM) before it can touch the FPU;
 * the handler then saves fpu_owner's still-live registers and loads the
 * faulting process's.  Because TS is set on every process change, no process
 * other than fpu_owner can dirty the registers between ownership changes, so
 * the save in the handler always captures the correct owner's state.
 */
static struct process *fpu_owner = NULL;

/* FXSAVE/FXRSTOR fault (#GP) on a non-16-byte-aligned operand, and the
 * enclosing struct process is kmalloc'd with no 16-byte guarantee, so align
 * the save area within its 15-byte-slack buffer at runtime. */
static inline void *fpu_area(struct process *p) {
    return (void *)(((uintptr_t)p->fpu_ctx.fpu_state + 15) & ~(uintptr_t)15);
}

// Save FPU context for a process
void fpu_save_context(struct process *p) {
    if (!p) return;

#ifndef HOST_TEST
    void *area = fpu_area(p);
    if (fpu_use_fxsave) {
        __asm__ volatile("fxsave (%0)" : : "r"(area), "m"(*(char (*)[512])area) : "memory");
    } else {
        __asm__ volatile("fnsave (%0)" : : "r"(area), "m"(*(char (*)[108])area) : "memory");
    }
#endif
}

// Restore FPU context for a process
void fpu_restore_context(struct process *p) {
    if (!p) return;

#ifndef HOST_TEST
    void *area = fpu_area(p);
    if (fpu_use_fxsave) {
        __asm__ volatile("fxrstor (%0)" : : "r"(area), "m"(*(const char (*)[512])area));
    } else {
        __asm__ volatile("frstor (%0)" : : "r"(area), "m"(*(const char (*)[108])area));
    }
#endif
}

/*
 * Re-arm CR0.TS so the next process to touch the FPU traps (#NM).  Called from
 * arch_switch_to on a process change.  Setting TS is what preserves the
 * fpu_owner invariant: after this, only the eventual #NM handler may clear it.
 */
void fpu_switch(void) {
#ifndef HOST_TEST
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x08; // Set TS
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
#endif
}

/*
 * A process is being torn down: if it currently owns the live FPU registers,
 * drop the ownership so the handler never tries to fnsave into freed storage.
 * Its register contents are discarded (the process is dead).
 */
void fpu_forget_process(struct process *p) {
    if (fpu_owner == p)
        fpu_owner = NULL;
}

// FPU Device Not Available Exception (Interrupt 7)
void fpu_handler(registers_t *regs) {
    (void)regs;
#ifndef HOST_TEST
    // Clear TS bit in CR0 to allow FPU access for the faulting instruction.
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~0x08; // Clear TS
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
#endif

    /* Kernel context with no current process: just enable the FPU for this
     * transient use and leave ownership untouched (the live registers still
     * belong to fpu_owner; a kernel path must not clobber user FP state). */
    if (!current_process)
        return;

    /* We are already the owner: our registers are live and intact (TS kept
     * every other process out since we last ran), so restoring here would
     * overwrite them with a stale save.  Nothing to do but keep TS clear. */
    if (fpu_owner == current_process)
        return;

    /* Ownership is changing.  Save the outgoing owner's still-live registers
     * before we load ours, so a later switch back to it restores correctly. */
    if (fpu_owner)
        fpu_save_context(fpu_owner);

    if (!current_process->fpu_ctx.fpu_used) {
        // First use of the FPU by this process - initialize to a known state.
#ifndef HOST_TEST
        __asm__ volatile("fninit");
#endif
        current_process->fpu_ctx.fpu_used = 1;
    } else {
        // Restore this process's previously saved FPU state.
        fpu_restore_context(current_process);
    }

    fpu_owner = current_process;

    // Re-executing the faulting instruction will now work.
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
        fpu_use_fxsave = i386_cpu_has_fxsr();
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
        if (fpu_use_fxsave) {
            kprint("FPU: Using FXSAVE/FXRSTOR context format\n");
            /* SSE / SSE2 are gated by CR4.OSFXSR (bit 9): without it
             * the CPU raises #UD on any SSE opcode in user mode, even
             * if CPUID reports SSE/SSE2 support.  Also set OSXMMEXCPT
             * (bit 10) so SIMD FP exceptions raise #XF rather than the
             * legacy #UD fallback.  We gate this on FXSR availability
             * because OSFXSR without FXSAVE/FXRSTOR is meaningless. */
            if (i386_cpu_has_cr4()) {
                uint32_t cr4;
                __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
                cr4 |= 0x200;   /* CR4.OSFXSR */
                cr4 |= 0x400;   /* CR4.OSXMMEXCPT */
                __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
                kprint("FPU: SSE enabled (CR4.OSFXSR + OSXMMEXCPT)\n");
            }
        } else {
            kprint("FPU: Using FNSAVE/FRSTOR context format\n");
        }
    } else {
        // No FPU - enable emulation
        fpu_present = 0;
        fpu_use_fxsave = 0;
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
