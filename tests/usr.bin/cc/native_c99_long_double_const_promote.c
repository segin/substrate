int main(void) {
    long double v = 123.0L;
    long double w = 2.0L;

    v *= 10;
    w = w * 10;

    if (v != 1230.0L) {
        return 1;
    }
    if (w != 20.0L) {
        return 2;
    }
    {
        long long i = 76627963145224193LL;
        long double ld = (long double)i;
        if (i < ld) {
            return 3;
        }
        if (ld < i) {
            return 4;
        }
    }
    return 0;
}
