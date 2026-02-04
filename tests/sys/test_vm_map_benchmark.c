#include <stdint.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <arch/i386/pmap.h>
#include <kern/console.h>
#include <sys/time.h>
#include <sys/kern/time.h>

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void run_vm_map_benchmark(void) {
    kprint("\n=== VM Map Benchmark ===\n");

    // Setup map
    pmap_t pmap = pmap_create();
    if (!pmap) {
        kprint("Failed to create pmap\n");
        return;
    }
    // Large range to accommodate many allocations
    vm_map_t *map = vm_map_create(pmap, 0x1000, 0x40000000);
    if (!map) {
        kprint("Failed to create map\n");
        return;
    }

    uint64_t start_cycles, end_cycles;

    int iterations = 5000;
    size_t alloc_size = 0x1000;

    kprint("Starting benchmark (5000 iterations)...\n");

    extern int kprintf(const char *fmt, ...);

    start_cycles = rdtsc();

    for (int i = 0; i < iterations; i++) {
        uintptr_t addr = 0;
        int ret = vm_map_find_space(map, &addr, alloc_size);
        if (ret != 0) {
            kprint("vm_map_find_space failed\n");
            break;
        }

        // Insert to occupy space
        // Using NULL object, anonymous memory
        ret = vm_map_insert(map, NULL, 0, addr, addr + alloc_size, 7, 7, 1);
        if (ret != 0) {
            kprint("vm_map_insert failed\n");
            break;
        }
    }

    end_cycles = rdtsc();

    uint64_t diff = end_cycles - start_cycles;

    // kprintf handles %lld? The syscall.c debug code used it manually.
    // Let's print upper/lower 32-bit if unsure, or cast to long long if supported.
    // sys/lib/printf.c might support ll.
    // To be safe:
    kprintf("Benchmark completed in %d cycles (approx)\n", (uint32_t)diff);
    // If diff > 4B, this truncates, but for 5000 it might fit?
    // 5000 iterations * N^2 behavior.
    // If N=5000, N^2/2 = 12.5M comparisons.
    // If 100 cycles per comparison -> 1.2G cycles.
    // It might overflow 32-bit.
    // Let's print in Millions of cycles.
    kprintf("Cycles: %d million\n", (uint32_t)(diff / 1000000));

    vm_map_destroy(map);
}
