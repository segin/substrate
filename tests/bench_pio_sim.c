#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BUFFER_SIZE (1024 * 1024 * 10) // 10 MB buffer
#define SECTOR_SIZE 512
#define WORDS_PER_SECTOR (SECTOR_SIZE / 2)
#define ITERATIONS 100

// Mock input function to prevent compiler from optimizing away the read
// In a real scenario, this would be reading from I/O port
static inline uint16_t mock_inw(uint16_t *src) {
    volatile uint16_t val = *src;
    return val;
}

// Baseline: Manual Loop
void manual_loop_copy(uint16_t *dst, uint16_t *src, size_t count) {
    for (size_t i = 0; i < count; i++) {
        *dst++ = mock_inw(src++);
    }
}

// Optimized: REP MOVSW (Proxy for REP INSW)
// We use inline assembly to force "rep movsw" usage
void rep_movsw_copy(uint16_t *dst, uint16_t *src, size_t count) {
    __asm__ volatile (
        "cld\n\t"
        "rep movsw"
        : "+S"(src), "+D"(dst), "+c"(count)
        :
        : "memory"
    );
}

int main() {
    uint16_t *src = (uint16_t *)malloc(BUFFER_SIZE);
    uint16_t *dst = (uint16_t *)malloc(BUFFER_SIZE);

    if (!src || !dst) {
        fprintf(stderr, "Allocation failed\n");
        return 1;
    }

    // Initialize source
    for (size_t i = 0; i < BUFFER_SIZE/2; i++) {
        src[i] = (uint16_t)i;
    }

    printf("Benchmarking PIO Transfer Simulation (Proxy: Memory Copy)\n");
    printf("Buffer Size: %d bytes\n", BUFFER_SIZE);
    printf("Iterations: %d\n", ITERATIONS);
    printf("--------------------------------------------------\n");

    // Benchmark Manual Loop
    clock_t start = clock();
    for (int k = 0; k < ITERATIONS; k++) {
        // Simulate sector-by-sector transfer to match driver behavior
        uint16_t *s = src;
        uint16_t *d = dst;
        for (size_t i = 0; i < BUFFER_SIZE / SECTOR_SIZE; i++) {
            manual_loop_copy(d, s, WORDS_PER_SECTOR);
            s += WORDS_PER_SECTOR;
            d += WORDS_PER_SECTOR;
        }
    }
    clock_t end = clock();
    double time_manual = (double)(end - start) / CLOCKS_PER_SEC;
    printf("Manual Loop: %.6f seconds\n", time_manual);

    // Benchmark REP MOVSW
    start = clock();
    for (int k = 0; k < ITERATIONS; k++) {
        uint16_t *s = src;
        uint16_t *d = dst;
        for (size_t i = 0; i < BUFFER_SIZE / SECTOR_SIZE; i++) {
            rep_movsw_copy(d, s, WORDS_PER_SECTOR);
            s += WORDS_PER_SECTOR;
            d += WORDS_PER_SECTOR;
        }
    }
    end = clock();
    double time_opt = (double)(end - start) / CLOCKS_PER_SEC;
    printf("REP MOVSW:   %.6f seconds\n", time_opt);

    printf("--------------------------------------------------\n");
    printf("Speedup: %.2fx\n", time_manual / time_opt);

    free(src);
    free(dst);
    return 0;
}
