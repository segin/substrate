static int inc(int x) {
    return x + 1;
}

static int first(int *p) {
    return p[0];
}

static int apply(int (*fn)(int), int v) {
    return fn(v);
}

int main(void) {
    int a[3] = {7, 8, 9};
    int (*fp)(int) = inc;

    if (first(a) != 7) {
        return 1;
    }
    if (fp(4) != 5) {
        return 2;
    }
    if (apply(inc, 6) != 7) {
        return 3;
    }
    return 0;
}
