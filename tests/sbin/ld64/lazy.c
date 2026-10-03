/*
 * lazy.c - lazy binding under /sbin/ld64.so.
 *
 * This program has a PLT slot for ld64_absent(), which nothing defines.
 *
 *   lazy            loads and runs: the slot is never used.  The first
 *                   calls into libld64lazy.so go through the binding
 *                   trampoline, with every argument register in use.
 *   lazy missing    calls ld64_absent(); the linker reports it and exits
 *                   with status 127 at that point, after "before call"
 *   LD_BIND_NOW=1   the program is not started at all
 *
 * dlopen() of libld64undef.so, which has the same kind of slot, succeeds
 * with RTLD_LAZY and fails with RTLD_NOW.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

extern long   lazymod_ints(long, long, long, long, long, long, long, long);
extern double lazymod_doubles(double, double, double, double, double,
                              double, double, double, double);
extern double lazymod_sum(int, ...);
extern void   lazymod_tick(void);
extern long   lazymod_calls;
extern void   ld64_absent(void);

int main(int argc, char **argv) {
    int bad = 0;

    if (argc > 1 && strcmp(argv[1], "missing") == 0) {
        printf("before call\n");
        fflush(stdout);
        ld64_absent();
        printf("lazy: FAIL (the call returned)\n");
        return 1;
    }

    /* First calls: each arrives in the trampoline. */
    long i1 = lazymod_ints(1, 2, 3, 4, 5, 6, 7, 8);
    double d1 = lazymod_doubles(1, 2, 3, 4, 5, 6, 7, 8, 9);
    double s1 = lazymod_sum(3, 1, 0.5, 2, 0.25, 3, 0.125);
    /* Second calls: straight through the bound slots. */
    long i2 = lazymod_ints(1, 2, 3, 4, 5, 6, 7, 8);
    double d2 = lazymod_doubles(1, 2, 3, 4, 5, 6, 7, 8, 9);
    double s2 = lazymod_sum(3, 1, 0.5, 2, 0.25, 3, 0.125);
    printf("ints=%ld,%ld doubles=%d,%d sum=%d/1000,%d/1000\n", i1, i2,
           (int)d1, (int)d2, (int)(s1 * 1000), (int)(s2 * 1000));
    if (i1 != 204 || i2 != 204 || d1 != 285 || d2 != 285 ||
        s1 != 6.875 || s2 != 6.875)
        bad = 1;

    for (int i = 0; i < 1000; i++) lazymod_tick();
    printf("ticks=%ld\n", lazymod_calls);
    if (lazymod_calls != 1000) bad = 1;

    if (dlopen("libld64undef.so", RTLD_NOW) != NULL) {
        printf("dlopen(RTLD_NOW) accepted an undefined function\n");
        bad = 1;
    } else {
        printf("dlopen(RTLD_NOW): %s\n", dlerror());
    }
    void *h = dlopen("libld64undef.so", RTLD_LAZY);
    if (!h) {
        printf("dlopen(RTLD_LAZY) failed: %s\n", dlerror());
        bad = 1;
    } else {
        int (*ok)(void) = (int (*)(void))dlsym(h, "lazyundef_ok");
        int r = ok ? ok() : -1;
        printf("dlopen(RTLD_LAZY) ok, lazyundef_ok()=%d\n", r);
        if (r != 7) bad = 1;
    }

    printf("lazy: %s\n", bad ? "FAIL" : "OK");
    return bad;
}
