/*
 * At -O1 and up cc copies a small static inline function's body into its
 * callers, as gcc does: each parameter is its argument, each return a
 * move into the call's result.  The values must be what the calls gave:
 * several returns, a result narrower than the word returned (a _Bool
 * from a compare, a signed char, an unsigned short), a loop over a local
 * array, a static local shared by every copy, inlined calls of inlined
 * functions, and asm goto, whose labels are the caller's after the copy.
 * run_inline_dead_call.sh checks what the inlining is for: a call that a
 * constant inline helper guards goes with its dead arm.
 */
#include <stdio.h>

static inline int classify(int x) {
    if (x < 0) {
        return -1;
    }
    if (x == 0) {
        return 0;
    }
    return 1;
}

static inline _Bool is_odd(unsigned x) {
    return (x & 1) != 0;
}

static inline signed char low_signed(int x) {
    return (signed char)x;
}

static inline unsigned short low_unsigned(long x) {
    return (unsigned short)x;
}

static inline int sum_squares(int n) {
    int a[8];
    int i;
    int s = 0;

    for (i = 0; i < 8; ++i) {
        a[i] = i < n ? i * i : 0;
    }
    for (i = 0; i < 8; ++i) {
        s += a[i];
    }
    return s;
}

static inline int counter(void) {
    static int count;

    return ++count;
}

static inline int twice_classified(int x) {
    return classify(x) * 2 + is_odd((unsigned)x);
}

static inline long long wide_add(long long a, long long b) {
    return a + b;
}

/* narrow parameters given wider arguments: a bool's compare word, an int for a char or short */
static inline __attribute__((__always_inline__)) int narrow_params(_Bool b, signed char sc, unsigned char uc,
                                                                    short sh) {
    return b * 1000000 + sc * 10000 + uc * 10 + sh;
}

#if defined(__i386__) || defined(__x86_64__)
static inline __attribute__((__always_inline__)) int branch_if_nonzero(int x) {
    __asm__ goto("testl %0, %0\n\tjnz %l[yes]" : : "r"(x) : "cc" : yes);
    return 0;
yes:
    return 1;
}
#else
static inline int branch_if_nonzero(int x) {
    return x != 0;
}
#endif

int main(int argc, char **argv) {
    int k = argc - 1; /* 0, unknown to the compiler */
    int i;
    int c1;
    int c2;
    int c3;

    (void)argv;
    for (i = -2; i <= 2; ++i) {
        printf("%d %d %d ", classify(i + k), twice_classified(i + k), is_odd((unsigned)(i + k)));
    }
    printf("\n%d %d %u %u\n", low_signed(0x1ff + k), low_signed(0x17f + k), low_unsigned(0x12345678L + k),
           low_unsigned(-1L + k));
    printf("%d %d %d\n", sum_squares(3 + k), sum_squares(8 + k), sum_squares(k));
    c1 = counter();
    c2 = counter();
    c3 = counter();
    printf("%d %d %d\n", c1, c2, c3);
    printf("%lld\n", wide_add(0x100000000LL + k, 0xffffffffLL));
    printf("%d %d\n", narrow_params(k == 0, 0x17f + k, 0x1fe + k, 0x18000 + k),
           narrow_params(k != 0, -1 + k, -1 + k, -1 + k));
    printf("%d %d\n", branch_if_nonzero(k), branch_if_nonzero(k + 5));
    return 0;
}
