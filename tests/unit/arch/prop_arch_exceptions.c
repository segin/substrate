#include <stdbool.h>
#include <stdint.h>
#include <arch/i386/idt.h>

/*
 * Property-based test: IDT Invariant
 * Prop: Gates 0-31 are always valid exception handlers.
 */

extern idt_entry_t idt_entries[256];

bool prop_idt_range_valid(void) {
    for (int i = 0; i < 32; i++) {
        // Must point to Kernel CS (0x08)
        if (idt_entries[i].sel != 0x08) return false;
        
        // Must be Present and interrupt gate (0x8E)
        if (idt_entries[i].flags != 0x8E) return false;
    }
    return true;
}

void run_exception_properties(void) {
    prop_idt_range_valid();
}
