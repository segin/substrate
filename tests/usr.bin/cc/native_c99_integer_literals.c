int main(void) {
    unsigned long long maxu = 18446744073709551615ULL;
    unsigned long ul = 1UL;
    long sl = 1L;

    if (0xffffffff < 0) {
        return 1;
    }
    if (0777u != 511u) {
        return 2;
    }
    if (2147483648 < 0) {
        return 3;
    }
    if (0x80000000 < 0) {
        return 4;
    }
    if (maxu == 0ULL) {
        return 5;
    }
    if (sizeof(1L) != sizeof(sl)) {
        return 6;
    }
    if (sizeof(1UL) != sizeof(ul)) {
        return 7;
    }
    if (sizeof(long) == 8) {
        if (sizeof(2147483648) != sizeof(long)) {
            return 8;
        }
        if (sizeof(0x100000000) != sizeof(long)) {
            return 9;
        }
    } else {
        if (sizeof(2147483648) != sizeof(long long)) {
            return 10;
        }
        if (sizeof(0x100000000) != sizeof(long long)) {
            return 11;
        }
    }
    return 0;
}
