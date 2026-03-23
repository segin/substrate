#include <stddef.h>

struct zero_align_member {
    char c;
    _Alignas(0) char x;
};

struct int_align_member {
    char c;
    _Alignas(int) char x;
};

struct anonymous_c11 {
    union {
        struct {
            int i;
            int j;
        };
        struct {
            int k;
            long l;
        } w;
    };
    int m;
};

char _Alignas(double) aligned_as_double;
char _Alignas(0) no_special_alignment;
extern char aligned_as_int;
char _Alignas(0) _Alignas(int) aligned_as_int;

_Static_assert(offsetof(struct zero_align_member, x) == 1, "_Alignas(0) should be a no-op");
_Static_assert(offsetof(struct int_align_member, x) == _Alignof(int), "_Alignas(int) should align member");
_Static_assert(offsetof(struct anonymous_c11, i) == offsetof(struct anonymous_c11, w.k),
               "anonymous aggregate members should overlay");

int main(void) {
    struct anonymous_c11 value;

    value.i = 2;
    value.w.k = 5;
    if (value.i != 5) {
        return 1;
    }

    if (((unsigned long)(void *)&aligned_as_int) % _Alignof(int) != 0) {
        return 2;
    }
    if (((unsigned long)(void *)&aligned_as_double) % _Alignof(double) != 0) {
        return 3;
    }
    if (((unsigned long)(void *)&no_special_alignment) % _Alignof(char) != 0) {
        return 4;
    }

    return 0;
}
