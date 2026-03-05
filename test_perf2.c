typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;
typedef long long int64_t;

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

int main() {
    volatile uint64_t sum = 0;
    for (uint64_t i = 0; i < 100000; i++) {
        sum += div64_32(i, OLD_HZ);
    }
    return 0;
}
