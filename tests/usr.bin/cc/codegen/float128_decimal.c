/*
 * A decimal floating value converts to and from _Float128, by assignment
 * and by cast, as gcc converts it: by libgcc's __bid_extendsdtf,
 * __bid_extendddtf, __bid_trunctdtf, __bid_trunctfsd, __bid_trunctfdd and
 * __bid_extendtftd.  The results are shown by their encodings.
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
    volatile _Decimal32 a = 1.25DF;
    volatile _Decimal64 b = 0.1DD;
    volatile _Decimal128 c = 1.000000000000000000000000000000001DL;
    _Float128 qa = a, qb = b, qc = c;
    _Float128 x = 1.0f128 / 3;
    _Decimal32 da = x;
    _Decimal64 db = (_Decimal64)x;
    _Decimal128 dc = x;
    show("qa", &qa, 16); show("qb", &qb, 16); show("qc", &qc, 16);
    show("da", &da, 4); show("db", &db, 8); show("dc", &dc, 16);
    { _Float128 cq = (_Float128)b; show("cast", &cq, 16); }
    return 0;
}
