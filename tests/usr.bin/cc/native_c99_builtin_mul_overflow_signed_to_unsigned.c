int int_minus_2 = -2;
int int_1 = 1;

int main(void) {
    unsigned long long result = 123;
    int overflow = __builtin_mul_overflow(int_minus_2, int_1, &result);
    if (!overflow) return 1;
    if (result != (unsigned long long)-2) return 2;
    return 0;
}
