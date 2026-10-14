int main(void) {
    long double a = 1.0L;
    long double b = 2.0L;
    long double c = a + b;

    if (sizeof(long double) < sizeof(double))
        return 1;
    if (sizeof(1.0L) != sizeof(long double))
        return 2;
    if (sizeof(a + b) != sizeof(long double))
        return 3;
    if ((double)c < 2.9 || (double)c > 3.1)
        return 4;
    return 0;
}
