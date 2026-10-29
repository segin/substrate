/*
 * Operations on _Atomic objects are atomic (C11 6.5.2.4p2, 6.5.16p3,
 * 6.5.16.2p3): two threads each update the same objects -- an int, a
 * long long, a char, a double and a pointer, named, through a pointer and
 * as members -- with ++, --, compound assignment of every operator and
 * plain assignment, and no update may be lost.  The values each operation
 * gives are printed too, so a build of it by gcc judges them.
 * run_atomic_qualified.sh builds it with cc for each target and -march.
 */
#include <pthread.h>
#include <stdio.h>

#define ROUNDS 200000

struct counters {
    _Atomic int i;
    _Atomic long long ll;
};

static _Atomic int n;
static _Atomic long long w;
static _Atomic char c;
static _Atomic double d;
static _Atomic unsigned u;
static int cells[4];
static int *_Atomic cursor = cells;
static struct counters k;

static void *work(void *arg) {
    _Atomic int *pn = &n;
    struct counters *pk = &k;
    int i;

    (void)arg;
    for (i = 0; i < ROUNDS; ++i) {
        n++;
        ++*pn;
        n -= 1;
        w += 3;
        w--;
        c += 1;
        d += 0.5;
        u |= 1u << (i & 31);
        pk->i += 2;
        k.ll++;
        cursor++;
        cursor--;
    }
    return NULL;
}

int main(void) {
    pthread_t t;
    int a, b;
    long long l;
    double x;

    /* the values the operations give, one thread */
    n = 5;
    a = n++;
    b = ++n;
    printf("%d %d %d\n", a, b, n);
    a = n--;
    b = (n *= 3);
    printf("%d %d %d\n", a, b, n);
    b = (n /= 4);
    a = (n <<= 3);
    printf("%d %d %d\n", a, b, n);
    a = (n %= 5);
    b = (n ^= 0x55);
    printf("%d %d %d\n", a, b, n);
    a = (n >>= 2);
    b = (n &= 0x1c);
    printf("%d %d %d\n", a, b, n);
    l = (w = -7);
    l += (w *= 1000000007LL);
    printf("%lld %lld\n", l, (long long)w);
    c = 120;
    a = (c += 10); /* wraps as a char */
    printf("%d %d\n", a, (int)c);
    d = 1.25;
    x = d++;
    printf("%g ", x);
    x = (d *= 3);
    printf("%g %g\n", x, (double)d);
    u = 0;
    n = 0;
    w = 0;
    c = 0;
    d = 0;
    k.i = 0;
    k.ll = 0;

    /* two threads at once */
    if (pthread_create(&t, NULL, work, NULL) != 0) {
        return 2;
    }
    work(NULL);
    pthread_join(t, NULL);
    printf("n %d w %lld c %d d %g u %x k %d %lld cursor %d\n", (int)n, (long long)w, (int)c, (double)d, (unsigned)u,
           (int)k.i, (long long)k.ll, (int)(cursor - cells));
    return 0;
}
