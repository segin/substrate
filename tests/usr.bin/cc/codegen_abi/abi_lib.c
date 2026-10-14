/*
 * The half of the ABI test that run_codegen_abi.sh builds with one
 * compiler while abi_main.c is built with the other.
 */
struct Q {
    int a, b;
};

struct R {
    int a, b, c, d, e;
};

struct Q lib_q(int a) {
    struct Q q = {a, a * 2};
    return q;
}

struct R lib_r(int a, int b) {
    struct R r = {a, b, a + b, a * b, 9};
    return r;
}

int lib_sum(char c, short s, int i, long long ll, double d, struct Q q) {
    return c + s + i + (int)(ll % 1000) + (int)(d * 10) + q.a * 100 + q.b;
}

long long lib_wide(long long a, unsigned long long b) {
    return a * 3 + (long long)(b >> 4);
}
