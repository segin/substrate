inline long long add_ll(long long a, long long b) {
    return a + b;
}

int idf(float x) {
    return x;
}

int main(void) {
    const unsigned long long base = 5;
    long long v = add_ll(base, 3);
    _Bool ok = 1;
    char c = 1;
    return (v - 8) + (ok - 1) + (c - 1) + (idf(2.0) - 2);
}
