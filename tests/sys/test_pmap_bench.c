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

#define BENCH_PAGES 4096 // 16MB
static uintptr_t pages[BENCH_PAGES];

// Helper to convert int to string
static void itoa(uint64_t val, char *buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    char tmp[32];
    int k = 0;
    while (val > 0) {
        tmp[k++] = (val % 10) + '0';
        val /= 10;
    }

    // Reverse into buf
    int i = 0;
    while (k > 0) {
        buf[i++] = tmp[--k];
    }
    buf[i] = '\0';
}

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

    // Use kernel pmap to ensure it's active (avoids CR3 check failure)
    pmap_t pmap = pmap_kernel();
    // Use a safe range in kernel space that is likely free
    // Kernel space starts at 0xC0000000.
    // Let's use 0xE0000000 (3.5GB) - should be safe for 16MB.
    uintptr_t start_va = 0xE0000000;

    // Benchmark 1: Loop pmap_enter
    uint64_t t1 = rdtsc();
    for (int i = 0; i < BENCH_PAGES; i++) {
        pmap_enter(pmap, start_va + i * 0x1000, pages[i], VM_PROT_READ | VM_PROT_WRITE, 0);
    }
    uint64_t t2 = rdtsc();

    char buf[64];
    kprint("pmap_enter loop cycles: ");
    itoa(t2 - t1, buf);
    kprint(buf);
    kprint("\n");

    // Cleanup (unmap pages)
    for (int i = 0; i < BENCH_PAGES; i++) {
        pmap_remove(pmap, start_va + i * 0x1000);
    }

    // Benchmark 2: pmap_enter_range
    uint64_t t3 = rdtsc();

    // Batch size of 64
    #define BATCH 64
    for (int i = 0; i < BENCH_PAGES; i += BATCH) {
        int count = (BENCH_PAGES - i < BATCH) ? (BENCH_PAGES - i) : BATCH;
        pmap_enter_range(pmap, start_va + i * 0x1000, &pages[i], count, VM_PROT_READ | VM_PROT_WRITE, 0);
    }
    uint64_t t4 = rdtsc();

    kprint("pmap_enter_range cycles: ");
    itoa(t4 - t3, buf);
    kprint(buf);
    kprint("\n");

    // Calculate speedup
    uint64_t diff1 = t2 - t1;
    uint64_t diff2 = t4 - t3;
    if (diff2 > 0) {
        uint64_t speedup_x100 = (diff1 * 100) / diff2;
        kprint("Speedup: ");
        itoa(speedup_x100 / 100, buf);
        kprint(buf);
        kprint(".");
        itoa(speedup_x100 % 100, buf);
        kprint(buf);
        kprint("x\n");
    }

    // Cleanup
    for (int i = 0; i < BENCH_PAGES; i++) {
        pmap_remove(pmap, start_va + i * 0x1000);
    }
}
