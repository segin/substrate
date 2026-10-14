typedef __builtin_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, t) __builtin_va_arg(v, t)
#define va_copy(d, s) __builtin_va_copy(d, s)

static int sum_ints(int n, ...) {
    va_list ap;
    int s = 0;
    int i;
    va_start(ap, n);
    for (i = 0; i < n; ++i) {
        s += va_arg(ap, int);
    }
    va_end(ap);
    return s;
}

static long long sum_ll(int n, ...) {
    va_list ap;
    long long s = 0;
    int i;
    va_start(ap, n);
    for (i = 0; i < n; ++i) {
        s += va_arg(ap, long long);
    }
    va_end(ap);
    return s;
}

static int copy_head_twice(int n, ...) {
    va_list ap;
    va_list cp;
    int a;
    int b;
    (void)n;
    va_start(ap, n);
    va_copy(cp, ap);
    a = va_arg(cp, int);
    b = va_arg(ap, int);
    va_end(cp);
    va_end(ap);
    return a + b;
}

int main(void) {
    int s = sum_ints(4, 1, 2, 3, 4);
    long long t = sum_ll(3, 5LL, 7LL, 11LL);
    int c = copy_head_twice(3, 9, 10, 11);
    if (s != 10) {
        return 1;
    }
    if (t != 23LL) {
        return 2;
    }
    if (c != 18) {
        return 3;
    }
    return 0;
}
