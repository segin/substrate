/*
 * Property tests for pmap
 * Properties are invariants that should always hold
 */

#include "../arch/i386/pmap.h"
#include "../arch/i386/pmm.h"
#include "../kern/console.h"

static int props_passed = 0;
static int props_failed = 0;

#define PROP_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("PROPERTY VIOLATION: "); kprint(msg); kprint("\n"); \
        props_failed++; \
        return; \
    } \
    props_passed++; \
} while(0)

// Property 1: Creating N pmaps and destroying them leaves system in same state
void property_creation_destruction_idempotent(void) {
    kprint("Property: creation/destruction is idempotent\n");
    
    extern uint32_t pmm_used_blocks;
    uint32_t initial_used = pmm_used_blocks;
    
    // Create and destroy 100 pmaps
    for (int i = 0; i < 100; i++) {
        pmap_t pmap = pmap_create();
        PROP_ASSERT(pmap != 0, "pmap creation succeeded");
        pmap_destroy(pmap);
    }
    
    uint32_t final_used = pmm_used_blocks;
    PROP_ASSERT(initial_used == final_used, 
                "memory usage returns to initial state");
    
    kprint("  PASS\n");
}

// Property 2: All created pmaps are page-aligned
void property_pmaps_are_aligned(void) {
    kprint("Property: all pmaps are page-aligned\n");
    
    pmap_t pmaps[20];
    
    for (int i = 0; i < 20; i++) {
        pmaps[i] = pmap_create();
        PROP_ASSERT(pmaps[i] != 0, "pmap created");
        
        uint32_t phys = (uint32_t)pmaps[i];
        PROP_ASSERT((phys & 0xFFF) == 0, "pmap is 4KB aligned");
    }
    
    // Cleanup
    for (int i = 0; i < 20; i++) {
        pmap_destroy(pmaps[i]);
    }
    
    kprint("  PASS\n");
}

// Property 3: All pmaps are unique
void property_pmaps_are_unique(void) {
    kprint("Property: all pmaps have unique addresses\n");
    
    #define N_PMAPS 50
    pmap_t pmaps[N_PMAPS];
    
    // Create N pmaps
    for (int i = 0; i < N_PMAPS; i++) {
        pmaps[i] = pmap_create();
        PROP_ASSERT(pmaps[i] != 0, "pmap created");
    }
    
    // Check all are unique
    for (int i = 0; i < N_PMAPS; i++) {
        for (int j = i + 1; j < N_PMAPS; j++) {
            PROP_ASSERT(pmaps[i] != pmaps[j], "pmaps are unique");
        }
    }
    
    // Cleanup
    for (int i = 0; i < N_PMAPS; i++) {
        pmap_destroy(pmaps[i]);
    }
    
    kprint("  PASS\n");
}

// Property 4: Kernel pmap never changes
void property_kernel_pmap_immutable(void) {
    kprint("Property: kernel pmap is immutable\n");
    
    pmap_t kernel1 = pmap_kernel();
    
    // Create and destroy some pmaps
    for (int i = 0; i < 10; i++) {
        pmap_t p = pmap_create();
        pmap_destroy(p);
    }
    
    pmap_t kernel2 = pmap_kernel();
    
    PROP_ASSERT(kernel1 == kernel2, "kernel pmap unchanged");
    
    kprint("  PASS\n");
}

void run_pmap_property_tests(void) {
    kprint("\n=== PMAP Property Tests ===\n");
    
    property_creation_destruction_idempotent();
    property_pmaps_are_aligned();
    property_pmaps_are_unique();
    property_kernel_pmap_immutable();
    
    kprint("\nProperty Test Results: ");
    kprint("Passed: ");
    kprint(" Failed: ");
    kprint("\n");
}
