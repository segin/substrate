#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <kern/console.h>
#include <stdint.h>
#include <string.h>

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void print_u64(uint64_t v) {
    char buf[32];
    int i = 0;
    if (v == 0) {
        kprint("0\n");
        return;
    }
    while (v > 0) {
        buf[i++] = (v % 10) + '0';
        v /= 10;
    }
    // Reverse
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    buf[i] = '\n';
    buf[i+1] = 0;
    kprint(buf);
}

#define BENCH_PAGES 4096 // 16MB
static uintptr_t pages[BENCH_PAGES];

void run_pmap_benchmark(void) {
    kprint("\n=== PMAP Benchmark ===\n");

    // Pre-allocate pages to exclude allocation time
    for (int i = 0; i < BENCH_PAGES; i++) {
        void *p = pmm_alloc_block();
        if (!p) {
            kprint("Failed to allocate pages for benchmark\n");
            return;
        }
        pages[i] = (uintptr_t)p - 0xC0000000; // Convert to physical
    }

    pmap_t old_pmap = curpmap;
    pmap_t pmap = pmap_create();
    uintptr_t start_va = 0x40000000; // Start at 1GB

    pmap_activate(pmap);

    // Benchmark 1: Loop pmap_enter
    uint64_t t1 = rdtsc();
    for (int i = 0; i < BENCH_PAGES; i++) {
        pmap_enter(pmap, start_va + i * 0x1000, pages[i], VM_PROT_READ | VM_PROT_WRITE, 0);
    }
    uint64_t t2 = rdtsc();
    uint64_t cycles_single = t2 - t1;

    kprint("pmap_enter loop cycles: ");
    print_u64(cycles_single);

    pmap_activate(old_pmap);
    pmap_destroy(pmap);

    // Re-allocate pages (as pmap_destroy freed them)
    for (int i = 0; i < BENCH_PAGES; i++) {
        void *p = pmm_alloc_block();
        if (!p) {
            kprint("Failed to allocate pages for benchmark 2\n");
            return;
        }
        pages[i] = (uintptr_t)p - 0xC0000000;
    }

    pmap = pmap_create();
    pmap_activate(pmap);

    // Benchmark 2: pmap_enter_range
    uint64_t t3 = rdtsc();

    // Batch size of 64
    #define BATCH 64
    for (int i = 0; i < BENCH_PAGES; i += BATCH) {
        int count = (BENCH_PAGES - i < BATCH) ? (BENCH_PAGES - i) : BATCH;
        pmap_enter_range(pmap, start_va + i * 0x1000, &pages[i], count, VM_PROT_READ | VM_PROT_WRITE, 0);
    }
    uint64_t t4 = rdtsc();
    uint64_t cycles_range = t4 - t3;

    kprint("pmap_enter_range cycles: ");
    print_u64(cycles_range);

    kprint("Improvement factor: ");
    if (cycles_range > 0)
        print_u64(cycles_single / cycles_range);
    else
        kprint("Infinite\n");

    pmap_activate(old_pmap);
    pmap_destroy(pmap);
}
