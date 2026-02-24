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

int main(void) {
    vm_map_t *map = vm_map_create(NULL, 0x1000, 0xFFFFFFFF);
    if (!map) {
        printf("Failed to create map\n");
        return 1;
    }

    printf("Inserting entries...\n");
    int n_entries = 2000;
    uintptr_t current_addr = 0x10000;
    for (int i = 0; i < n_entries; i++) {
        if (vm_map_insert(map, NULL, 0, current_addr, current_addr + 4096, 0x3, 0x3, 0) != 0) {
            printf("Insert failed at %d\n", i);
            return 1;
        }
        current_addr += 4096;
    }

    printf("Benchmarking lookups...\n");
    int n_lookups = 100000;
    double start_time = get_time_sec();

    // Random lookups
    srand(12345); // deterministic seed
    for (int i = 0; i < n_lookups; i++) {
        // Pick a random entry index
        int idx = rand() % n_entries;
        uintptr_t addr = 0x10000 + idx * 4096;

        vm_map_entry_t *entry = vm_map_lookup(map, addr);
        if (!entry) {
            printf("Lookup failed for addr %lx (idx %d)\n", (unsigned long)addr, idx);
            return 1;
        }
        if (entry->start != addr) {
             printf("Lookup mismatch: expected %lx, got %lx\n", (unsigned long)addr, (unsigned long)entry->start);
             return 1;
        }
    }

    double end_time = get_time_sec();
    printf("Time taken: %.6f seconds\n", end_time - start_time);
    printf("Lookups per second: %.2f\n", n_lookups / (end_time - start_time));

    vm_map_destroy(map);
    return 0;
}
