#include "../../../sys/arch/i386/smp.h"
#include <stdbool.h>
#include <string.h>

/*
 * SMP Discovery Unit Tests
 */

bool test_smp_initial_count(void) {
    // Before discovery, count should be 0 or 1 (if init called)
    int count = smp_get_cpu_count();
    return (count >= 0 && count <= MAX_CPUS);
}

bool test_acpi_discovery_logic(void) {
    // Mocking ACPI tables in a unit test is complex, 
    // we verify the discovery call doesn't crash and behaves predictably
    // when no RSDP is found.
    smp_discover_cores();
    int count = smp_get_cpu_count();
    
    // On most test environments without ACPI mapped, it should fallback to 1 (UP)
    return (count == 1);
}
