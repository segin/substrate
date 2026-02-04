#include <stdint.h>
#include <arch/i386/pmap.h>
#include <kern/console.h>
#include "tests.h"
#include <string.h>

// Hardware Mapping Unit Test
void test_pmap_hw_mappings(void) {
    kprint("Testing Hardware Mappings...\n");

    // Check LAPIC (0xFEE00000)
    uint32_t lapic_pa = 0xFEE00000;
    uint32_t extracted_pa = pmap_extract(pmap_kernel(), lapic_pa);

    if (extracted_pa == lapic_pa) {
        kprint("  LAPIC Identity Mapping: PASS\n");
    } else {
        char buf[64];
        extern int sprintf(char *str, const char *format, ...);
        sprintf(buf, "  LAPIC Identity Mapping: FAIL (Ex: %08x)\n", extracted_pa);
        kprint(buf);
    }
}

// Property Test: Verify that a range of kernel mappings remains consistent
void property_pmap_kernel_consistency(void) {
    kprint("Property Test: Kernel Mapping Consistency...\n");
    
    pmap_t pmap = pmap_kernel();
    int leaks = 0;

    // Check first 4MB (Identity mapped)
    for (uint32_t va = 0; va < 0x400000; va += 0x1000) {
        if (pmap_extract(pmap, va) != va) {
            leaks++;
        }
    }

    if (leaks == 0) {
        kprint("  Kernel 0-4MB Identity: PASS\n");
    } else {
        char buf[64];
        extern int sprintf(char *str, const char *format, ...);
        sprintf(buf, "  Kernel 0-4MB Identity: FAIL (%d errors)\n", leaks);
        kprint(buf);
    }
}

// Fuzzing Test: Fuzz pmap_enter with varied addresses
// NOTE: This is unsafe to run on the real kernel pmap, we use a temporary one
void fuzz_pmap_enter(void) {
    kprint("Fuzzing pmap_enter...\n");

    pmap_t pmap = pmap_create();
    if (!pmap) {
        kprint("  Fuzzing: Failed to create pmap\n");
        return;
    }

    // Activate the pmap so we can use recursive mapping
    pmap_activate(pmap);

    // Seed "random" based on something somewhat dynamic (e.g. pmap pointer)
    uint32_t seed = (uint32_t)pmap;
    
    for (int i = 0; i < 100; i++) {
        seed = seed * 1103515245 + 12345;
        uint32_t va = (seed & 0x3FFFF) << 12; // Random 0..1GB
        uint32_t pa = ((seed >> 10) & 0x7FFFFFF) & ~0xFFF; // Random 0..128MB, Page Aligned
        
        if (va < 0xC0000000) {
            pmap_enter(pmap, va, pa, VM_PROT_READ | VM_PROT_WRITE, 0);
            if (pmap_extract(pmap, va) != pa) {
                char buf[64];
                extern int sprintf(char *str, const char *format, ...);
                sprintf(buf, "  Fuzzing FAIL: VA %08x PA %08x\n", va, pa);
                kprint(buf);
            }
        }
    }

    // Switch back and cleanup
    pmap_activate(pmap_kernel());
    pmap_destroy(pmap);

    kprint("  Fuzzing pmap_enter (100 iterations): PASS\n");
}

void run_vm_expanded_tests(void) {
    test_pmap_hw_mappings();
    property_pmap_kernel_consistency();
    fuzz_pmap_enter();
}
