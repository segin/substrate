/*
 * A compound literal of a decimal floating type or of _Float128 holds the
 * value its initializer converts to (C11 6.5.2.5): (_Decimal64){ x } is x
 * converted, as gcc has it, not x's bits taken for an encoding.  The
 * results are shown by their encodings.
 *
 * cc-std: gnu2x
 */
#include <stdio.h>
#include <string.h>
static void show(const char *t, const void *p, int n) {
    const unsigned char *b = p;
    printf("%s ", t);
    while (n-- > 0) printf("%02x", b[n]);
    printf("\n");
}
int main(void) {
    volatile double x = 1;
    volatile int k = 7;
    _Decimal64 d = (_Decimal64){x};
    _Decimal32 e = (_Decimal32){k};
    _Decimal128 f = (_Decimal128){2.5};
    _Float128 q = (_Float128){x};
    _Float128 r = (_Float128){3};
    _Decimal32 s = (_Decimal32){5u};
    show("d", &d, 8); show("e", &e, 4); show("f", &f, 16); show("q", &q, 16); show("r", &r, 16); show("s", &s, 4);
    show("p", &(_Float128){0.5}, 16);
    return 0;
}
