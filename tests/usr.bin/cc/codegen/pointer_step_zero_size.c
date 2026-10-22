/*
 * Pointer arithmetic on a pointer to a complete type of no size -- an
 * empty structure (GNU) or a zero-length array -- steps by 0, as gcc has
 * it.  A uniprocessor Linux's spinlock_t is an empty structure, and
 * lib/bucket_locks.c takes &tlocks[i] of an array of them; cc refused
 * ("unsupported pointer base type in lowering").
 */
#include <stdio.h>

typedef struct {
} spinlock_t;

struct holder {
    spinlock_t *p;
    int zero[0];
};

static spinlock_t locks[4];
static spinlock_t *third = &locks[3];
static int (*zp)[0];

int main(int argc, char **argv) {
    spinlock_t *t = locks;
    struct holder h = {locks};
    unsigned int i;
    int n = 0;

    (void)argv;
    for (i = 0; i < 4; ++i) {
        n += &t[i] == t;
    }
    n += t + argc == t;
    t++;
    --t;
    h.p++;
    n += h.p == locks;
    n += third == locks;
    n += zp + 5 == zp;
    printf("%d %d\n", n, (int)sizeof(spinlock_t));
    return 0;
}
