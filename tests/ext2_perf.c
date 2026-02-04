#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

// Mocking the environment
#define BLOCK_SIZE 4096
#define BLOCKS_PER_GROUP (BLOCK_SIZE * 8)

static uint8_t bitmap_buf[BLOCK_SIZE] __attribute__((aligned(4)));

// Naive implementation
int find_first_zero_naive(uint8_t *bitmap, uint32_t bits_in_group) {
    for (uint32_t i = 0; i < bits_in_group; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;

        if (!(bitmap[byte_idx] & (1 << bit_idx))) {
            return i;
        }
    }
    return -1;
}

// Optimized implementation
int find_first_zero_optimized(uint8_t *bitmap, uint32_t bits_in_group) {
    uint32_t *bitmap32 = (uint32_t *)bitmap;
    uint32_t i = 0;

    // Fast path: skip full words
    for (; i + 32 <= bits_in_group; i += 32) {
        if (bitmap32[i / 32] != 0xFFFFFFFF) {
            break;
        }
    }

    // Slow path
    for (; i < bits_in_group; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;

        if (!(bitmap[byte_idx] & (1 << bit_idx))) {
            return i;
        }
    }
    return -1;
}

int main() {
    // Initialize bitmap (mostly full)
    memset(bitmap_buf, 0xFF, BLOCK_SIZE);

    // Create some holes
    // Clear bit 100
    bitmap_buf[100/8] &= ~(1 << (100%8));
    // Clear bit 20000
    bitmap_buf[20000/8] &= ~(1 << (20000%8));
    // Clear last bit
    bitmap_buf[(BLOCKS_PER_GROUP-1)/8] &= ~(1 << ((BLOCKS_PER_GROUP-1)%8));

    // Verify correctness
    int naive = find_first_zero_naive(bitmap_buf, BLOCKS_PER_GROUP);
    int opt = find_first_zero_optimized(bitmap_buf, BLOCKS_PER_GROUP);

    if (naive != opt) {
        printf("Mismatch! Naive: %d, Opt: %d\n", naive, opt);
        return 1;
    }
    printf("Correctness verified. Found bit: %d\n", naive);

    // Benchmark
    int iterations = 10000;
    clock_t start, end;
    double cpu_time_used;
    volatile int dummy = 0;

    // 1. Full bitmap (worst case) - set all to 1
    memset(bitmap_buf, 0xFF, BLOCK_SIZE);

    printf("Running benchmark (Full Bitmap - Worst Case)...\n");

    // Naive
    start = clock();
    for (int k = 0; k < iterations; k++) {
        dummy = find_first_zero_naive(bitmap_buf, BLOCKS_PER_GROUP);
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Naive (Full): %f seconds\n", cpu_time_used);

    // Optimized
    start = clock();
    for (int k = 0; k < iterations; k++) {
        dummy = find_first_zero_optimized(bitmap_buf, BLOCKS_PER_GROUP);
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Optimized (Full): %f seconds\n", cpu_time_used);

    // 2. Empty bitmap (best case) - set all to 0
    memset(bitmap_buf, 0, BLOCK_SIZE);

    printf("Running benchmark (Empty Bitmap - Best Case)...\n");

    // Naive
    start = clock();
    for (int k = 0; k < iterations; k++) {
        dummy = find_first_zero_naive(bitmap_buf, BLOCKS_PER_GROUP);
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Naive (Empty): %f seconds\n", cpu_time_used);

    // Optimized
    start = clock();
    for (int k = 0; k < iterations; k++) {
        dummy = find_first_zero_optimized(bitmap_buf, BLOCKS_PER_GROUP);
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Optimized (Empty): %f seconds\n", cpu_time_used);

    // 3. Middle hole
    memset(bitmap_buf, 0xFF, BLOCK_SIZE);
    int mid = BLOCKS_PER_GROUP / 2;
    bitmap_buf[mid/8] &= ~(1 << (mid%8));

    printf("Running benchmark (Middle Hole)...\n");

    // Naive
    start = clock();
    for (int k = 0; k < iterations; k++) {
        dummy = find_first_zero_naive(bitmap_buf, BLOCKS_PER_GROUP);
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Naive (Mid): %f seconds\n", cpu_time_used);

    // Optimized
    start = clock();
    for (int k = 0; k < iterations; k++) {
        dummy = find_first_zero_optimized(bitmap_buf, BLOCKS_PER_GROUP);
    }
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Optimized (Mid): %f seconds\n", cpu_time_used);

    return 0;
}
