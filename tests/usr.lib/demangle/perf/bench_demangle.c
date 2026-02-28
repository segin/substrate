#include <demangle.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double
now_sec(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }

    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

int
main(void)
{
    static const char *symbols[] = {
        "_Z3foov",
        "_Z3fooi",
        "_ZN3Foo3barEv",
        "_ZNK3Foo3barEi",
        "_ZN3FooplERKS_",
        "_ZN3FooD2Ev",
        "_ZTV3Foo",
        "_D3foo3barFiZi",
        "_D3foo3barPi",
        "_d_allocmemory"
    };
    const size_t symbol_count = sizeof(symbols) / sizeof(symbols[0]);
    const size_t iterations = 100000u;
    double t0;
    double t1;
    double elapsed;

    t0 = now_sec();
    for (size_t i = 0u; i < iterations; i++) {
        char *out = demangle(symbols[i % symbol_count], DEMANGLE_AUTO);
        free(out);
    }
    t1 = now_sec();

    elapsed = t1 - t0;
    printf("demangled %zu symbols in %.6f seconds\n", iterations, elapsed);

    if (elapsed >= 1.0) {
        fprintf(stderr, "performance target missed: expected < 1.0s\n");
        return 1;
    }

    return 0;
}
