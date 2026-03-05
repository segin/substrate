typedef unsigned long long uint64_t;
typedef unsigned int uint32_t;
typedef long long int64_t;

uint64_t ticks = 0;

#define OLD_HZ 100
#define NEW_HZ 128

int main() {
    volatile uint64_t sum = 0;
    for (uint64_t i = 0; i < 100000; i++) {
        sum += i / NEW_HZ;
    }
    return 0;
}
