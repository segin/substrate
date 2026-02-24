#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <vm/vm_map.h>

extern int pmap_remove_count;
extern int pmap_remove_range_count;
extern int pmm_free_block_count;
extern process_t *current_process;

// sys_mmap declaration
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint64_t offset);

bool run_mmap_cleanup_test(void) {
    printf("Running mmap cleanup test...\n");

    // Reset counters
    pmap_remove_count = 0;
    pmap_remove_range_count = 0;

    // Setup dummy process
    process_t dummy_proc;
    memset(&dummy_proc, 0, sizeof(dummy_proc));

    // Create map
    dummy_proc.vm_map = vm_map_create(NULL, 0x1000, 0xC0000000);

    process_t *old_proc = current_process;
    current_process = &dummy_proc;

    // Request 5MB. Mocks allow 1024 pages (4MB). So it fails.
    // Cleanup loop runs for 1024 pages.

    size_t size = 5 * 1024 * 1024; // 5MB
    void *ret = sys_mmap(NULL, size, 1, 0x22, -1, 0); // PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS

    if (ret != (void*)-1) {
        printf("sys_mmap succeeded unexpectedly? mock memory might be larger?\n");
        // Check mocks.c limit
    } else {
        printf("sys_mmap failed as expected.\n");
    }

    printf("pmap_remove_count: %d\n", pmap_remove_count);
    printf("pmap_remove_range_count: %d\n", pmap_remove_range_count);
    printf("pmm_free_block_count: %d\n", pmm_free_block_count);

    bool passed = true;

    // Assert optimization: Should use pmap_remove_range once, and 0 pmap_remove calls
    if (pmap_remove_count != 0) {
        printf("FAIL: Expected 0 pmap_remove, got %d\n", pmap_remove_count);
        passed = false;
    } else {
        printf("PASS: pmap_remove count is 0 (Good).\n");
    }

    if (pmap_remove_range_count != 1) {
        printf("FAIL: Expected 1 pmap_remove_range, got %d\n", pmap_remove_range_count);
        passed = false;
    } else {
        printf("PASS: pmap_remove_range called exactly once (Optimization verified).\n");
    }

    // Verify no regression: physical pages must still be freed!
    if (pmm_free_block_count < 1000) {
        printf("FAIL: Expected ~1024 pmm_free_block, got %d. MEMORY LEAK DETECTED!\n", pmm_free_block_count);
        passed = false;
    } else {
        printf("PASS: pmm_free_block count is %d (Physical memory freed).\n", pmm_free_block_count);
    }

    // Cleanup
    current_process = old_proc;

    return passed;
}
