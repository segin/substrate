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
    for (uint64_t i = 0; i < 10000000; i++) {
        sum += div64_32(i, OLD_HZ);
    }
}

void test_new() {
    volatile uint64_t sum = 0;
    for (uint64_t i = 0; i < 10000000; i++) {
        sum += i / NEW_HZ;
    }
}

int main() {
    clock_t start, end;

    start = clock();
    test_old();
    end = clock();
    printf("Old HZ time: %f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);

    start = clock();
    test_new();
    end = clock();
    printf("New HZ time: %f seconds\n", (double)(end - start) / CLOCKS_PER_SEC);

    return 0;
}
