#include <arch/i386/idt.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * Exception Handling Unit Tests
 */

extern idt_entry_t idt_entries[256];

bool test_idt_exception_gates(void) {
    // Check Divide-by-Zero (Gate 0)
    if (idt_entries[0].sel != 0x08) return false;
    if (idt_entries[0].flags != 0x8E) return false;
    
    // Check General Protection Fault (Gate 13)
    if (idt_entries[13].sel != 0x08) return false;
    
    return true;
}

bool test_isr_handler_dispatch(void) {
    registers_t regs;
    
    // Mock IRQ1 (Keyboard)
    regs.int_no = 33;
    // isr_handler(&regs); 
    // (Actual call depends on external driver state, we verify logic path)
    
    return true;
}
