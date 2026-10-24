/*
 * A parameter every call of a static function passes the same constant is
 * that constant (run_constant_args.sh checks what that lets it drop): one
 * narrower than the constant takes it truncated, a signed char extended
 * and an unsigned one not; a parameter calls pass different constants, or
 * a variable, stays one; and a function whose address is taken is left
 * alone, whatever its direct calls pass.
 */
#include <stdio.h>

static volatile int vol = 3;

static int __attribute__((noinline)) unused_helper(int x) {
    return x;
}

static int narrow(signed char c, unsigned char u, short s, int k) {
    int r = c * 1000 + u * 10 + s + k;

    if (vol > 100)
        r += unused_helper(r);
    return r;
}

static int varies(int a, int b) {
    return a * 7 + b;
}

static int taken(int a) {
    return a + 1;
}

int (*volatile fp)(int) = taken;

int main(void) {
    int x = vol;

    printf("%d %d\n", narrow(0x1ff, 0x1ff, 0x18000, 5), narrow(0x1ff, 0x1ff, 0x18000, 5) + 1);
    printf("%d %d %d\n", varies(1, 2), varies(3, 2), varies(x, 2));
    printf("%d %d\n", taken(4), fp(9));
    return 0;
}
