/* Variable-length arrays and alloca live on the stack: a VLA declared in a
 * loop reuses its storage instead of taking more each time round, both are
 * 16-aligned, and a VLA's storage is given back when its function returns.
 * (The VLA's elements ask for 16: gcc aligns a VLA only to its type's
 * alignment, and a char one tuned for an i686, which accumulates outgoing
 * arguments, lands above them, wherever that is.) */
#include <alloca.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    _Alignas(16) char b[16];
} chunk16;

static int sum(int n) {
    int a[n];
    int i, s = 0;

    for (i = 0; i < n; i++) {
        a[i] = i;
    }
    for (i = 0; i < n; i++) {
        s += a[i];
    }
    return s + (int)sizeof a;
}

int main(void) {
    long i;
    char *lowest = 0, *highest = 0, *p;
    int t = 0, aligned = 1;
    long calls = 0;

    for (i = 0; i < 1000000; i++) {
        chunk16 buf[7 + (i & 3)];
        char *b = (char *)buf;

        memset(buf, (int)i, sizeof buf);
        if (highest == 0 || b > highest) {
            highest = b;
        }
        if (lowest == 0 || b < lowest) {
            lowest = b;
        }
        if (((unsigned long)b & 15) != 0) {
            aligned = 0;
        }
        t += b[5] & 1;
    }
    for (i = 0; i < 100000; i++) {
        calls += sum(64);
    }
    p = alloca(32);
    memcpy(p, "alloca ok", 10);
    printf("%d %d %d %ld %s\n", sum(10), sum(1), t, calls, p);
    printf("spread<4k %d aligned %d alloca-aligned %d\n", highest - lowest < 4096, aligned,
           ((unsigned long)p & 15) == 0);
    return 0;
}
