/* _Alignof and __alignof__ of an object give the object's alignment, as gcc gives it */
#include <stdio.h>

typedef int a16 __attribute__((aligned(16)));

struct s {
    char c;
    int m __attribute__((aligned(8)));
    _Alignas(32) short n;
    int plain;
};

struct __attribute__((packed)) p {
    char c;
    int i;
};

a16 gx;
int gy __attribute__((aligned(32)));
_Alignas(64) char gz;
int gplain;

int main(void) {
    a16 x;
    int y __attribute__((aligned(32)));
    _Alignas(64) char z;
    int plain;
    struct s s;
    struct s *sp = &s;
    struct p pk;

    printf("%d %d %d %d\n", (int)_Alignof(x), (int)_Alignof(y), (int)_Alignof(z), (int)_Alignof(plain));
    printf("%d %d %d %d\n", (int)__alignof__(x), (int)__alignof__(y), (int)__alignof__(z), (int)__alignof__(plain));
    printf("%d %d %d %d\n", (int)_Alignof(gx), (int)_Alignof(gy), (int)_Alignof(gz), (int)_Alignof(gplain));
    printf("%d %d %d %d\n", (int)_Alignof(s.m), (int)__alignof__(sp->n), (int)_Alignof(s.plain),
           (int)__alignof__(s.c));
    printf("%d %d\n", (int)__alignof__(pk.i), (int)__alignof__(pk));
    x = 1;
    y = 2;
    z = 3;
    plain = 4;
    s.m = 5;
    pk.i = 6;
    return x + y + z + plain + s.m + pk.i - 21;
}
