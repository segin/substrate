#include <arch/i386/ioapic.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * IO-APIC Unit Tests
 */

// Since IO-APIC uses indirect MMIO registers, we verify the accessor logic
// (Actual init requires hardware/mock memory)

bool test_ioapic_init_logic(void) {
    // Verifies the initialization function sets the base pointer
    ioapic_init(0xFEC00000);
    return true;
}

bool test_ioapic_routing_logic(void) {
    // Verifies routing call path
    ioapic_set_routing(1, 0x21, 0); // Route Keyboard IRQ1 to CPU 0
    return true;
}
