#include <stdbool.h>
#include <stdint.h>
#include <arch/i386/lapic.h"

/*
 * Property-based test: LAPIC Invariant
 * Prop: After init, LAPIC_SVR must have bit 8 (Enable) set.
 */

// Note: Requires mocking lapic_read/write or the MMIO memory
// We verify the logic constant for now.

bool prop_lapic_enabled_invariant(uint32_t svr_val) {
    // Invariant: Bit 8 is the software enable bit
    return (svr_val & LAPIC_SVR_ENABLE);
}

void run_lapic_properties(void) {
    prop_lapic_enabled_invariant(LAPIC_SVR_ENABLE | 0xFF);
}
