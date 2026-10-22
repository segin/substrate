/*
 * An empty structure as a value: returned, passed, assigned, a member.  A
 * uniprocessor Linux makes spinlock_t one, and passes and returns it by
 * value (__SPIN_LOCK_UNLOCKED, lock guards).  cc refused to return one
 * ("unsupported aggregate return size in return lowering"), to assign one,
 * or to read one as a member, and on x86-64 passed one as an argument
 * where gcc passes nothing, shifting the arguments after it.
 */
#include <stdio.h>

typedef struct { } lock_t;
struct holder { int a; lock_t l; int b; };

lock_t g_lock;
struct holder gh = { 1, { }, 2 };

static lock_t mk(int *k) {
    lock_t r;

    *k += 1;
    return r;
}

static int take(int a, lock_t l, int b) {
    (void)l;
    return a * 10 + b;
}

int main(void) {
    int k = 0;
    static lock_t sl;
    lock_t l, *p = &l;
    struct holder h = { 3, { }, 4 };

    l = mk(&k);
    g_lock = l;
    sl = g_lock;
    h.l = mk(&k);
    *p = h.l;
    gh.l = *p;
    printf("%d %d %d %d %d %d\n", k, h.a, h.b, gh.a, gh.b, take(5, l, 6));
    printf("%d %d %d\n", (int)sizeof h, (int)__builtin_offsetof(struct holder, b), (int)sizeof sl);
    return 0;
}
