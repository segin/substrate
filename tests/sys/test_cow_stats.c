#include <sys/types.h>
#include <stdint.h>
#include <arch/i386/pmap.h>
#include <kern/console.h>
#include <string.h>

#define ASSERT(c) if(!(c)) { kprint("Assertion failed: " #c "\n"); return; }

extern int sys_pmap_stats(struct pmap_stats *out);

// Test 1: Read Stats
void test_cow_stats_read(void) {
    kprint("Test: PMAP Stats Read... ");
    
    struct pmap_stats stats;
    int ret = sys_pmap_stats(&stats);
    
    if (ret != 0) {
        kprint("FAIL (syscall returned error)\n");
        return;
    }
    
    // Check reasonable values
    if (stats.faults == 0) kprint("WARN (faults == 0) ");
    if (stats.total_pmaps == 0) {
         kprint("FAIL (total_pmaps == 0)\n");
         return;
    }
    if (stats.active_pmaps == 0) {
         kprint("FAIL (active_pmaps == 0)\n");
         return;
    }
    
    kprint("PASS\n");
}

void run_cow_stats_tests(void) {
    kprint("\n=== PMAP Stats Tests ===\n");
    test_cow_stats_read();
    kprint("\n");
}
