#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <sys/proc.h>
#include <vm/vm_map.h>

// External mocks
extern int mock_pmap_enter_count;
extern int mock_pmap_enter_batch_count;
extern void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint64_t offset);

bool test_mmap_batch_run(void) {
    // Setup process environment
    process_t mock_proc;
    memset(&mock_proc, 0, sizeof(mock_proc));

    vm_map_t map;
    // Initialize map with range 0x1000 to 0x10000000
    vm_map_init(&map, NULL, 0x1000, 0x10000000);
    mock_proc.vm_map = &map;
    mock_proc.pmap = (pmap_t)1; // Mock pmap

    // Set current_process
    extern process_t *current_process;
    process_t *old_process = current_process;
    current_process = &mock_proc;

    // Reset counters
    mock_pmap_enter_count = 0;
    mock_pmap_enter_batch_count = 0;

    // Call sys_mmap with 10 pages
    // PROT_READ | PROT_WRITE = 3
    // MAP_PRIVATE | MAP_ANONYMOUS = 0x2 | 0x20 = 0x22
    // We request specific address 0x20000 with MAP_FIXED (0x10) | MAP_ANONYMOUS (0x20) | MAP_PRIVATE (0x2)
    // Actually, let's use 0x22 (no fixed) and see where it lands, or 0x32 (fixed)
    // sys_mmap(addr, len, prot, flags, fd, offset)

    // Using MAP_FIXED ensures we hit the mapping logic we want,
    // but sys_mmap handles non-fixed too.
    // Let's use simple anonymous mapping.
    void *ret = sys_mmap((void*)0x20000, 4096 * 10, 3, 0x22, -1, 0);

    if (ret == (void*)-1) {
        printf("sys_mmap failed\n");
        current_process = old_process;
        return false;
    }

    printf("sys_mmap returned %p\n", ret);
    printf("pmap_enter called: %d\n", mock_pmap_enter_count);
    printf("pmap_enter_batch called: %d\n", mock_pmap_enter_batch_count);

    bool passed = false;

    // We expect either unoptimized (10 single calls) or optimized (batch calls)
    // But for THIS step (reproduction), we specifically want to see N calls if unoptimized.
    // However, the test should PASS if it detects the state correctly, so we can run it later to verify optimization.

    if (mock_pmap_enter_count == 10 && mock_pmap_enter_batch_count == 0) {
        printf("State: Unoptimized (N calls detected)\n");
        passed = true;
    } else if (mock_pmap_enter_count == 0 && mock_pmap_enter_batch_count > 0) {
        printf("State: Optimized (Batched calls detected)\n");
        passed = true;
    } else {
        printf("State: Unexpected (Mixed or Zero calls?)\n");
        passed = false;
    }

    // Cleanup
    current_process = old_process;

    return passed;
}
