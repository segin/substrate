/*
 * Property tests for mmap
 */

#include "../vm/vm_area.h"
#include <sys/mman.h>
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

// Property 1: mmap + munmap is idempotent
void property_mmap_munmap_idempotent(void) {
    kprint("Property: mmap+munmap is idempotent\n");
    
    extern uint32_t pmm_used_blocks;
    uint32_t initial_used = pmm_used_blocks;
    
    // Map and unmap 50 times
    for (int i = 0; i < 50; i++) {
        void *addr = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        PROP_ASSERT(addr != MAP_FAILED, "mmap succeeded");
        
        sys_munmap(addr, 4096);
    }
    
    uint32_t final_used = pmm_used_blocks;
    PROP_ASSERT(initial_used == final_used, "memory usage returned to baseline");
    
    kprint("  PASS\n");
}

// Property 2: All mappings are page-aligned
void property_mappings_aligned(void) {
    kprint("Property: all mappings are page-aligned\n");
    
    for (int i = 0; i < 20; i++) {
        void *addr = sys_mmap(NULL, (i + 1) * 100, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        PROP_ASSERT(addr != MAP_FAILED, "mmap succeeded");
        
        uint32_t a = (uint32_t)addr;
        PROP_ASSERT((a & 0xFFF) == 0, "address is 4KB aligned");
        
        sys_munmap(addr, (i + 1) * 100);
    }
    
    kprint("  PASS\n");
}

// Property 3: Mappings don't overlap
void property_no_overlap(void) {
    kprint("Property: mappings don't overlap\n");
    
    #define N_MAPS 30
    void *addrs[N_MAPS];
    uint32_t sizes[N_MAPS];
    
    // Create N mappings
    for (int i = 0; i < N_MAPS; i++) {
        sizes[i] = (i + 1) * 4096;
        addrs[i] = sys_mmap(NULL, sizes[i], PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        PROP_ASSERT(addrs[i] != MAP_FAILED, "mmap succeeded");
    }
    
    // Check no overlaps
    for (int i = 0; i < N_MAPS; i++) {
        uint32_t start_i = (uint32_t)addrs[i];
        uint32_t end_i = start_i + sizes[i];
        
        for (int j = i + 1; j < N_MAPS; j++) {
            uint32_t start_j = (uint32_t)addrs[j];
            uint32_t end_j = start_j + sizes[j];
            
            // No overlap if: end_i <= start_j or start_i >= end_j
            PROP_ASSERT(end_i <= start_j || start_i >= end_j, "no overlap");
        }
    }
    
    // Cleanup
    for (int i = 0; i < N_MAPS; i++) {
        sys_munmap(addrs[i], sizes[i]);
    }
    
    kprint("  PASS\n");
}

// Property 4: Written data persists
void property_data_persistence(void) {
    kprint("Property: written data persists\n");
    
    void *addr = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    PROP_ASSERT(addr != MAP_FAILED, "mmap succeeded");
    
    char *ptr = (char *)addr;
    
    // Write pattern
    for (int i = 0; i < 4096; i++) {
        ptr[i] = (char)(i & 0xFF);
    }
    
    // Verify pattern
    for (int i = 0; i < 4096; i++) {
        PROP_ASSERT(ptr[i] == (char)(i & 0xFF), "data persists");
    }
    
    sys_munmap(addr, 4096);
    
    kprint("  PASS\n");
}

void run_mmap_property_tests(void) {
    kprint("\n=== MMAP Property Tests ===\n");
    
    property_mmap_munmap_idempotent();
    property_mappings_aligned();
    property_no_overlap();
    property_data_persistence();
    
    kprint("\nProperty Test Results: ");
    kprint("Passed: ");
    kprint(" Failed: ");
    kprint("\n");
}
