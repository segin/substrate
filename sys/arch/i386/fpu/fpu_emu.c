#include "fpu_emu.h"
#include "../idt.h"
#include "../io.h"
#include "../../drivers/video/vga.h"

extern void isr7(void);

// FPU Device Not Available Exception (Interrupt 7)
void fpu_handler(registers_t *regs) {
    (void)regs;
    // Clear TS bit in CR0
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~0x08; // Clear TS
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    
    // In a real OS, we would restore the FPU state for the current process here.
    // Since we don't have per-process FPU state tracking fully implemented yet,
    // we just re-enable it.
    
    // For emulation, we would interpret the instruction at regs->eip.
    // If we were doing soft-float emulation.
    // But x86 usually has an FPU (or SSE) these days.
    // To properly emulate a 387, we would decode the instruction.
    
    vga_write("FPU Device Not Available (NM) - Emulation stub.\n", 44);
    
    // HACK: Increment EIP to skip instruction? No, that crashes.
    // We must handle it or hang.
    // For now, assume hardware FPU exists and we lazily enabled it.
    // Re-executing the instruction should work now that TS is cleared.
}

void fpu_init(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    
    // Set EM (Emulation) bit if we want to force trap on FPU use
    // cr0 |= 0x04; 
    
    // Set MP (Monitor Coprocessor) bit
    cr0 |= 0x02;
    
    // Set NE (Numeric Error)
    cr0 |= 0x20;
    
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    
    // Register INT 7 handler
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    
    // Reset FPU
    __asm__ volatile("fninit");
}
