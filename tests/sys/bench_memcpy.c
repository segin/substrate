#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// Baseline: Byte-by-byte copy
void *memcpy_baseline(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

// Optimized: Word-copy (Draft implementation for benchmarking)
void *memcpy_optimized(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    // Align destination to word boundary
    size_t align_mask = sizeof(unsigned long) - 1;
    while (n > 0 && ((uintptr_t)d & align_mask)) {
        *d++ = *s++;
        n--;
    }

    // Copy words
    unsigned long *wd = (unsigned long *)d;
    const unsigned long *ws = (const unsigned long *)s;
    size_t words = n / sizeof(unsigned long);

    // Note: This relies on the CPU handling unaligned reads if 's' is not aligned.
    // x86 handles this well.
    while (words--) {
        *wd++ = *ws++;
    }

    // Copy remaining bytes
    d = (unsigned char *)wd;
    s = (const unsigned char *)ws;
    n %= sizeof(unsigned long);
    while (n--) {
        *d++ = *s++;
    }

    return dest;
}

double get_time_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void run_test(const char *name, void *(*func)(void *, const void *, size_t),
              void *dst, void *src, size_t size, int iterations) {
    // Warmup
    func(dst, src, size);

    double start = get_time_sec();
    for (int i = 0; i < iterations; i++) {
        func(dst, src, size);
        // Prevent compiler from optimizing away the call (simple anti-opt)
        ((volatile char *)dst)[0] = ((volatile char *)dst)[0];
    }
    double end = get_time_sec();

    double total_time = end - start;
    double gb_per_sec = (double)size * iterations / (1024.0 * 1024.0 * 1024.0) / total_time;

    printf("%-20s Size: %8zu bytes | Time: %.6f s | Speed: %.4f GB/s\n",
           name, size, total_time, gb_per_sec);
}

int main() {
    const size_t MAX_SIZE = 16 * 1024 * 1024; // 16 MB
    void *src = malloc(MAX_SIZE);
    void *dst = malloc(MAX_SIZE);

    if (!src || !dst) {
        perror("malloc");
        return 1;
    }

    // Initialize source
    memset(src, 0xAA, MAX_SIZE);

    printf("=== memcpy Benchmark ===\n");
    printf("Word size: %zu bytes\n\n", sizeof(unsigned long));

    size_t sizes[] = {64, 512, 4096, 65536, 1024*1024, 16*1024*1024};
    int iterations[] = {10000000, 2000000, 200000, 10000, 1000, 50};

    for (int i = 0; i < 6; i++) {
        size_t s = sizes[i];
        int iter = iterations[i];

        run_test("Baseline", memcpy_baseline, dst, src, s, iter);
        run_test("Optimized", memcpy_optimized, dst, src, s, iter);
        printf("\n");
    }

    free(src);
    free(dst);
    return 0;
}
