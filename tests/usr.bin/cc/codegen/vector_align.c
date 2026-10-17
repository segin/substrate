/*
 * A vector type is aligned to its size, as gcc aligns it, in structures,
 * globals and locals and when passed by value; gcc's _Alignof caps the
 * alignment a vector gives at 16 while __alignof__ does not, and neither
 * caps one _Alignas or aligned asks for.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef int v8 __attribute__((vector_size(32)));
typedef char v64 __attribute__((vector_size(64)));
typedef short v4 __attribute__((vector_size(8)));

struct s { char c; v8 v; };
struct t { char c; v64 w; short d; };
struct over { _Alignas(32) char c; };
struct sa { char c; } __attribute__((aligned(64)));

v8 g8 = {1, 2, 3, 4, 5, 6, 7, 8};
char pad;
struct t gt;

static v8 add(v8 a, v8 b) { return a + b; }
static int sum(struct s x) { return x.v[0] + x.v[7] + x.c; }

int main(void) {
    v8 l = {10, 20, 30, 40, 50, 60, 70, 80};
    v8 r = add(l, g8);
    struct s x = {3, {1, 0, 0, 0, 0, 0, 0, 2}};

    printf("%u %u %u %u %u\n", (unsigned)sizeof(struct s), (unsigned)offsetof(struct s, v), (unsigned)sizeof(struct t),
           (unsigned)offsetof(struct t, d), (unsigned)__alignof__(struct t));
    printf("%zu %zu %zu %zu %zu %zu\n", _Alignof(v8), _Alignof(v64), _Alignof(v4), _Alignof(struct s),
           _Alignof(struct over), _Alignof(struct sa));
    printf("%zu %zu %zu %zu\n", __alignof__(v8), __alignof__(v64), __alignof__(struct s), __alignof__(struct over));
    printf("%d %d %d\n", r[0], r[7], sum(x));
    printf("%d %d %d\n", (int)((uintptr_t)&gt % 64), (int)((uintptr_t)&l % 32), (int)((uintptr_t)&g8 % 32));
    return pad;
}
