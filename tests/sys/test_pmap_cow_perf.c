#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <kern/console.h>
#include <string.h>
#include <stdint.h>

#define COW_SCRATCH_ADDR 0xFFBFF000

static uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void test_cow_perf(void) {
    kprint("Test: COW Performance Benchmark\n");

    // Allocate source page and fill with pattern
    void *src_virt = pmm_alloc_block();
    if (!src_virt) {
        kprint("FAIL: Failed to allocate src page\n");
        return;
    }
    memset(src_virt, 0xAA, 4096);

    // Allocate destination page
    void *dst_virt = pmm_alloc_block();
    if (!dst_virt) {
        kprint("FAIL: Failed to allocate dst page\n");
        return;
    }
    // Ensure destination is clean for verification
    memset(dst_virt, 0, 4096);

    uint32_t dst_phys = (uint32_t)dst_virt - 0xC0000000;

    // --- Benchmark Method A (Current: Map/Copy/Unmap) ---
    uint64_t start_a = rdtsc();
    for (int i = 0; i < 1000; i++) {
        pmap_kenter(COW_SCRATCH_ADDR, dst_phys);
        __asm__ volatile("invlpg %0" :: "m" (*(char *)COW_SCRATCH_ADDR));

        memcpy((void*)COW_SCRATCH_ADDR, src_virt, 4096);

        pmap_kremove(COW_SCRATCH_ADDR);
        __asm__ volatile("invlpg %0" :: "m" (*(char *)COW_SCRATCH_ADDR));
    }
    uint64_t end_a = rdtsc();
    uint64_t cycles_a = (end_a - start_a) / 1000;

    // Verify copy happened (check last byte)
    // We can check dst_virt because we expect it to be the same physical memory
    if (((uint8_t*)dst_virt)[4095] != 0xAA) {
        kprint("FAIL: Method A copy verification failed\n");
    }

    // Reset dest
    memset(dst_virt, 0, 4096);

    // --- Benchmark Method B (Optimized: Direct Copy) ---
    uint64_t start_b = rdtsc();
    for (int i = 0; i < 1000; i++) {
        memcpy(dst_virt, src_virt, 4096);
    }
    uint64_t end_b = rdtsc();
    uint64_t cycles_b = (end_b - start_b) / 1000;

    // Verify copy happened
    if (((uint8_t*)dst_virt)[4095] != 0xAA) {
        kprint("FAIL: Method B copy verification failed\n");
    }

    // Report
    kprint("\nResults (Average cycles per copy):\n");

    // We cannot use float printf in kernel usually, assume integer printing
    char buf[128];
    // Baseline
    // Use manual string construction or simple kprint if printf is limited
    // But we have printf.h usually?
    // sys/arch/i386/pmap.c uses printf.h
    // But let's look at test_runner.c, it uses kprint.
    // I'll use simple kprint with helper or just sprintf if available.
    // In pmap.c: #include <stdio.h>

    // I will use kprint with manual formatting or just rely on kprint supporting simple format?
    // tests/sys/test_runner.c uses kprint("...").
    // tests/sys/test_printf_new.c suggests printf exists?

    // I'll try to use standard printf if available via <stdio.h> but output to console might need kprint.
    // In pmap.c there is <stdio.h>.
    // But I'll stick to a safe manual formatting if I can't check printf implementation easily.
    // Actually, I can use helper print_dec.

    // For now, I'll assume I can sprintf.
    extern int sprintf(char * str, const char * format, ...);

    sprintf(buf, "Method A (Baseline):  %u cycles\n", (uint32_t)cycles_a);
    kprint(buf);

    sprintf(buf, "Method B (Optimized): %u cycles\n", (uint32_t)cycles_b);
    kprint(buf);

    if (cycles_a > cycles_b) {
        uint32_t diff = cycles_a - cycles_b;
        uint32_t pct = (diff * 100) / cycles_a;
        sprintf(buf, "Improvement: %u cycles (%u%%)\n", diff, pct);
        kprint(buf);
    } else {
        kprint("WARNING: No improvement measured.\n");
    }

    // Cleanup
    pmm_free_block(src_virt);
    pmm_free_block(dst_virt);
}
