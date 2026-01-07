#include <sys/types.h>
#include <sys/proc.h>
#include "../arch/i386/pmap.h"
#include "../kern/console.h"
#include <string.h>

#define ASSERT(c) if(!(c)) { kprint("Assertion failed: " #c "\n"); return 0; }

extern int sys_get_cow_stats(struct pmap_stats *out);

// Test ability to read COW stats
int test_cow_stats_read(void) {
    kprint("Testing COW stats read...\n");
    
    struct pmap_stats stats;
    memset(&stats, 0xFF, sizeof(stats)); // Fill with garbage
    
    int ret = sys_get_cow_stats(&stats);
    ASSERT(ret == 0);
    
    // Check if values are reasonable (should be >= 0, and likely 0 for new boot)
    // Actually some might be non-zero if system has been running
    kprint("COW Stats:\n");
    // Manual integer to string for kprint (std libs might not be fully linked or convenient)
    // Actually we can use kprint format? No, kprint is basic string.
    // We'll trust the syscall output if ret is 0 and it overwrote garbage.
    
    ASSERT(stats.cow_faults != 0xFFFFFFFF);
    ASSERT(stats.cow_pages_mapped != 0xFFFFFFFF);
    
    kprint("test_cow_stats_read passed\n");
    return 1;
}

int run_cow_stats_tests(void) {
    if (!test_cow_stats_read()) return 0;
    return 1;
}
