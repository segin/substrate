/*
 * An object with static storage is as aligned as its type needs, not
 * only as its size suggests: long double, __int128, a structure one of
 * whose members _Alignas raises, initialized or not, at file scope and in
 * a function.
 */
#include <stdint.h>
#include <stdio.h>

struct over {
    _Alignas(32) char c;
};

char p0;
long double ld = 1.5L;
char p1;
struct over so;
char p2;
struct over so_init = {7};
char p3;
_Alignas(64) char explicit_a[3];
char p4;
#ifdef __SIZEOF_INT128__
__int128 i128;
char p5;
#endif

int main(void) {
    static char q0;
    static long double sl = 2.0L;
    static char q1;
    static struct over ss;

    printf("%d %d %d %d\n", (int)((uintptr_t)&ld % _Alignof(long double)), (int)((uintptr_t)&so % 32),
           (int)((uintptr_t)&so_init % 32), (int)((uintptr_t)explicit_a % 64));
    printf("%d %d\n", (int)((uintptr_t)&sl % _Alignof(long double)), (int)((uintptr_t)&ss % 32));
#ifdef __SIZEOF_INT128__
    printf("%d\n", (int)((uintptr_t)&i128 % 16));
#endif
    return p0 + p1 + p2 + p3 + p4 + q0 + q1 + (ld != 1.5L) + (sl != 2.0L) + (so_init.c != 7);
}
