#include <vm/vm_map.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Helper for benchmarking
double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

void test_correctness(void) {
    printf("Running correctness tests...\n");
    vm_map_t *map = vm_map_create(NULL, 0x1000, 0x100000);
    if (!map) {
        printf("Failed to create map\n");
        exit(1);
    }

    // 1. Initial state: one big hole [0x1000, 0x100000)
    uintptr_t addr;
    if (vm_map_find_space(map, &addr, 0x1000) != 0 || addr != 0x1000) {
        printf("Test 1 Failed: Expected 0x1000, got %lx\n", (unsigned long)addr);
        exit(1);
    }

    // 2. Insert at 0x1000-0x2000
    if (vm_map_insert(map, NULL, 0, 0x1000, 0x2000, 0, 0, 0) != 0) {
        printf("Test 2 Failed: Insert\n");
        exit(1);
    }

    // 3. Find space again. Should be 0x2000.
    if (vm_map_find_space(map, &addr, 0x1000) != 0 || addr != 0x2000) {
        printf("Test 3 Failed: Expected 0x2000, got %lx\n", (unsigned long)addr);
        exit(1);
    }

    // 4. Insert at 0x4000-0x5000 (creating hole at 0x2000-0x4000)
    if (vm_map_insert(map, NULL, 0, 0x4000, 0x5000, 0, 0, 0) != 0) {
        printf("Test 4 Failed: Insert\n");
        exit(1);
    }

    // 5. Find space for 0x2000. Should find 0x2000.
    if (vm_map_find_space(map, &addr, 0x2000) != 0 || addr != 0x2000) {
        printf("Test 5 Failed: Expected 0x2000, got %lx\n", (unsigned long)addr);
        exit(1);
    }

    // 6. Find space for 0x3000. Should find 0x5000 (after second entry).
    // Because 0x2000-0x4000 is only 0x2000 size. 0x3000 won't fit?
    // Wait, gap is 0x2000. 0x3000 > 0x2000. Correct.
    if (vm_map_find_space(map, &addr, 0x3000) != 0 || addr != 0x5000) {
        printf("Test 6 Failed: Expected 0x5000, got %lx\n", (unsigned long)addr);
        exit(1);
    }

    // 7. Remove 0x4000-0x5000. Should merge with 0x2000-0x4000 and 0x5000-end.
    // Resulting hole: 0x2000-end.
    vm_map_remove(map, 0x4000, 0x5000);

    // 8. Find space for 0x3000. Should now find 0x2000.
    if (vm_map_find_space(map, &addr, 0x3000) != 0 || addr != 0x2000) {
        printf("Test 8 Failed: Expected 0x2000, got %lx\n", (unsigned long)addr);
        exit(1);
    }

    vm_map_destroy(map);
    printf("Correctness tests passed.\n");
}

int main(void) {
    test_correctness();

    printf("Initializing map...\n");
    // Create a 4GB map
    vm_map_t *map = vm_map_create(NULL, 0x1000, 0xFFFFFFFF);
    if (!map) {
        printf("Failed to create map\n");
        return 1;
    }

    // Fill with 10,000 entries of 4KB
    // Leaving 4KB gaps effectively? No, let's pack them tightly first.
    // To simulate fragmentation, we can then remove some.
    // Or just tightly pack small entries, and try to find a large entry.

    int n_entries = 40000;
    uintptr_t current_addr = 0x10000;

    for (int i = 0; i < n_entries; i++) {
        if (vm_map_insert(map, NULL, 0, current_addr, current_addr + 4096, 0x3, 0x3, 0) != 0) {
            printf("Insert failed at %d\n", i);
            return 1;
        }
        current_addr += 4096;
    }

    // Now map is filled from 0x10000 to 0x10000 + N*4096.

    // Create fragmentation by removing every other entry
    printf("Creating fragmentation (20,000 holes)...\n");
    current_addr = 0x10000;
    for (int i = 0; i < n_entries; i += 2) {
        if (vm_map_remove(map, current_addr, current_addr + 4096) != 0) {
            printf("Remove failed at %d\n", i);
            return 1;
        }
        current_addr += 8192; // Skip 2 entries (remove 1, skip 1)
    }

    // Now we have ~20,000 small holes (4KB each).
    // And one big hole at the end.

    // Benchmark: Find space for a large allocation (e.g. 1MB)
    // The allocator should skip all small holes efficiently using the tree.
    // Linear scan would check all small holes.

    printf("Benchmarking find_space with %d entries...\n", n_entries);

    int n_iters = 5000;
    double start_time = get_time_sec();

    uintptr_t addr;
    size_t alloc_size = 1024 * 1024; // 1MB

    for (int i = 0; i < n_iters; i++) {
        // We just find space, we don't insert (or we insert and remove)
        // vm_map_find_space is what we want to test.
        if (vm_map_find_space(map, &addr, alloc_size) != 0) {
            printf("find_space failed at iter %d\n", i);
            return 1;
        }

        // To prevent the hint from simply staying at the end and making subsequent lookups O(1)
        // (because vm_map_find_space starts at hint),
        // we need to reset the hint or force a search from start.
        // vm_map_find_space starts at hint.
        // If we don't insert, hint doesn't move.
        // But where is the hint currently?
        // After filling, hint is at the last inserted entry (end of list).

        // If hint is at end:
        // 1. Scan from hint to end (tail). Checks tail gap immediately.
        //    Returns tail address.
        // THIS IS FAST.

        // We want to force it to scan the LIST.
        // We need the hint to be at the BEGINNING.
        // We can manually reset map->hint if we had access, but we don't from here (opaque struct? No, header is included).
        // Yes, vm_map.h exposes the struct.

        map->hint = map->header;
        // Now it scans from header->next (start of list).
        // It will scan all 20,000 entries.
    }

    double end_time = get_time_sec();
    printf("Time taken: %.6f seconds\n", end_time - start_time);
    printf("Ops per second: %.2f\n", n_iters / (end_time - start_time));

    vm_map_destroy(map);
    return 0;
}
