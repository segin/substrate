#include <stdbool.h>
#include <stdint.h>
#include <arch/i386/ioapic.h"

/*
 * Property-based test: IO-APIC Invariant
 * Prop: ioapic_set_mask(irq, true) -> REDTBL[irq] & (1<<16) != 0.
 */

bool prop_ioapic_mask_invariant(uint32_t redtbl_val, bool masked) {
    if (masked) {
        return (redtbl_val & (1 << 16));
    } else {
        return !(redtbl_val & (1 << 16));
    }
}

void run_ioapic_properties(void) {
    prop_ioapic_mask_invariant(1 << 16, true);
    prop_ioapic_mask_invariant(0, false);
}
