int main(void) {
    unsigned long long maxu = 18446744073709551615ULL;

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
    return 0;
}
