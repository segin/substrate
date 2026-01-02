#include <stdbool.h>
#include <stdint.h>
#include "../../../sys/arch/i386/smp.h"

/*
 * Property-based test: SMP Invariant
 * Prop: 1 <= cpu_count <= MAX_CPUS.
 */

bool prop_smp_cpu_count_range(void) {
    smp_discover_cores();
    int count = smp_get_cpu_count();
    
    return (count >= 1 && count <= MAX_CPUS);
}

bool prop_smp_unique_lapic_ids(void) {
    smp_discover_cores();
    int count = smp_get_cpu_count();
    
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (cpus[i].lapic_id == cpus[j].lapic_id) return false;
        }
    }
    return true;
}

void run_smp_properties(void) {
    prop_smp_cpu_count_range();
    prop_smp_unique_lapic_ids();
}
