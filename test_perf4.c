#include <stdio.h>
#include <stdint.h>
#include <time.h>

static inline int64_t div64_32(int64_t dividend, uint32_t divisor) {
    int64_t quotient = 0;
    int64_t remainder = 0;
    int negative = 0;

    if (dividend < 0) {
        negative = 1;
        dividend = -dividend;
    }

    for (int i = 63; i >= 0; i--) {
        remainder = (remainder << 1) | ((dividend >> i) & 1);
        if ((uint64_t)remainder >= divisor) {
            remainder -= divisor;
            quotient |= (1LL << i);
        }
    }

    return negative ? -quotient : quotient;
}

uint64_t ticks = 0;

#define OLD_HZ 100
#define NEW_HZ 128

void test_old() {
    volatile uint64_t sum = 0;
    for (uint64_t i = 0; i < 100000; i++) {
        sum += div64_32(i, OLD_HZ);
    }
}

void test_new() {
    volatile uint64_t sum = 0;
    for (uint64_t i = 0; i < 100000; i++) {
        sum += i >> 7; // shift by 7 for 128
    }
}

int main() {
    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    test_old();
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("Old HZ time: %f ms\n", (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0);

    clock_gettime(CLOCK_MONOTONIC, &start);
    test_new();
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("New HZ time: %f ms\n", (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0);

    return 0;
}
