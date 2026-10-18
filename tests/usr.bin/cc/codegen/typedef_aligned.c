/*
 * gcc's `aligned` on a typedef gives the type that alignment, raised or
 * lowered: _Alignof and __alignof__ of the name, structure members,
 * objects at file scope and in functions, and a typedef of the typedef
 * all have it.  A pointer to the type is an ordinary pointer.
 */
#include <stdint.h>
#include <stdio.h>

typedef int a16 __attribute__((aligned(16)));
typedef a16 b16;
typedef char c8 __attribute__((aligned(8)));
typedef int u1 __attribute__((aligned(1)));
typedef struct { char c; } __attribute__((aligned(32))) s32;

struct m { char c; a16 x; };
struct n { char c; b16 y; c8 z; };
struct lo { char c; u1 x; };
struct p { char c; a16 *q; };

char pad;
a16 g;
c8 gc;
s32 gs;

int main(void) {
    char lp;
    a16 l;
    b16 lb;

    printf("%zu %zu %zu %zu %zu %zu\n", sizeof(a16), _Alignof(a16), __alignof__(b16), __alignof__(c8), _Alignof(u1),
           __alignof__(s32));
    printf("%zu %zu %zu %zu %zu %zu %zu %zu\n", sizeof(struct m), _Alignof(struct m), sizeof(struct n),
           _Alignof(struct n), sizeof(struct lo), _Alignof(struct lo), sizeof(struct p), _Alignof(struct p));
    printf("%d %d %d %d %d\n", (int)((uintptr_t)&g % 16), (int)((uintptr_t)&gc % 8), (int)((uintptr_t)&gs % 32),
           (int)((uintptr_t)&l % 16), (int)((uintptr_t)&lb % 16));
    return pad + lp * 0;
}
