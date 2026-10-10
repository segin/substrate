/*
 * What test_compiler_output.sh has the host's gcc compile, this assembler
 * assemble, and the host run: a little of everything a compiler writes --
 * loops and the alignments before them, a switch with its jump table,
 * calls through a pointer and of variadic functions, 64-bit arithmetic,
 * floating point, strings in mergeable sections, a struct array on the
 * stack.  Its output is compared with the same program assembled by the
 * host's own assembler.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct item {
    int key;
    char name[12];
};

static int cmp(const void *a, const void *b) {
    return ((const struct item *)a)->key - ((const struct item *)b)->key;
}

static unsigned collatz(unsigned n) {
    unsigned steps = 0;

    while (n != 1) {
        n = (n & 1) ? 3 * n + 1 : n / 2;
        steps++;
    }
    return steps;
}

static unsigned long long fib(int n) {
    unsigned long long a = 0, b = 1;
    int i;

    for (i = 0; i < n; i++) {
        unsigned long long t = a + b;

        a = b;
        b = t;
    }
    return a;
}

static void sieve(int n) {
    char *composite = calloc((size_t)n + 1, 1);
    int count = 0;
    int i, j;

    if (composite == NULL) {
        return;
    }
    for (i = 2; i <= n; i++) {
        if (!composite[i]) {
            count++;
            for (j = i * 2; j <= n; j += i) {
                composite[j] = 1;
            }
        }
    }
    printf("primes<=%d: %d\n", n, count);
    free(composite);
}

/*
 * Variables addressed by name, with constants stored to them and compared
 * with them: `movl $5, counter(%rip)`, `cmpl $0, flag`, `addl $3,
 * counter`, `movw $0x1234, narrow` -- instructions where an immediate
 * follows the address.  (This program ran in every build while each of
 * those was assembled wrong, because it had none.)  They are global and
 * the functions are not inlined, so that the stores and the comparisons
 * are each an instruction on memory.
 */
int counter;
int flag;
short narrow;
char tiny;
long long wide;

__attribute__((noinline)) void globals_set(void) {
    counter = 5;
    flag = 0;
    narrow = 0x1234;
    tiny = 1;
    wide = 1000;
}

__attribute__((noinline)) void globals_step(void) {
    counter += 3;
    if (flag == 0) {
        flag = 7;
    } else {
        flag -= 1;
    }
    if (narrow != 0x1234) {
        counter += 100000;
    }
    if (tiny == 1) {
        wide += 100000;
    }
    tiny ^= 1;
    if (counter > 20) {
        narrow = 0x1235;
    }
}

int main(int argc, char **argv) {
    struct item v[16];
    unsigned x = 12345;
    double sum = 0;
    int i;

    for (i = 0; i < 16; i++) {
        x = x * 1103515245u + 12345u;
        v[i].key = (int)(x >> 16) % 1000;
        snprintf(v[i].name, sizeof v[i].name, "n%d", i);
    }
    globals_set();
    for (i = 0; i < 9; i++) {
        globals_step();
    }
    printf("globals %d %d %d %d %lld\n", counter, flag, (int)narrow, (int)tiny, wide);
    qsort(v, 16, sizeof v[0], cmp);
    for (i = 0; i < 16; i++) {
        printf("%d:%s ", v[i].key, v[i].name);
    }
    printf("\ncollatz(27)=%u fib(80)=%llu\n", collatz(27), fib(80));
    sieve(10000);
    for (i = 1; i <= 100; i++) {
        sum += 1.0 / (i * (double)i);
    }
    printf("sum=%.6f len=%zu argc=%d\n", sum, strlen(argv[0]) > 0 ? (size_t)3 : (size_t)0, argc);
    switch (argc) {
    case 1: puts("one"); break;
    case 2: puts("two"); break;
    case 3: puts("three"); break;
    case 4: puts("four"); break;
    case 5: puts("five"); break;
    default: puts("many"); break;
    }
    return 0;
}
