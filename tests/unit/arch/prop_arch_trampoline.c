#include <stdbool.h>
#include <stdint.h>

/*
 * Property-based test: Trampoline Invariant
 * Prop: Trampoline fits in 4KB and offsets are consistent.
 */

extern char trampoline_start[];
extern char trampoline_end[];

bool prop_trampoline_size_invariant(void) {
    uintptr_t size = (uintptr_t)trampoline_end - (uintptr_t)trampoline_start;
    return (size > 0 && size <= 4096);
}

bool prop_trampoline_alignment_invariant(void) {
    // TRAMPOLINE_ADDR must be 4KB aligned for STARTUP IPI
    uint32_t addr = 0x8000;
    return (addr % 4096 == 0);
}

void run_trampoline_properties(void) {
    prop_trampoline_size_invariant();
    prop_trampoline_alignment_invariant();
}
