/*
 * A type qualifier among the declaration specifiers qualifies the type
 * they give (C11 6.7.3): `const int *` is a pointer to `const int`, which
 * is not compatible with a pointer to `int` (6.7.6.1p2), so
 * __builtin_types_compatible_p tells them apart and _Generic picks the
 * association that names the qualified pointee.  A _Generic controlling
 * expression loses only its own top-level qualifiers (6.5.1.1p2).  A
 * pointer to const is itself modifiable, and qualified objects are read
 * and written as their unqualified types are.
 */
#include <stdio.h>
#include <string.h>

struct s { int a; };

const int ci = 3;
volatile int vi;
const char *msg = "hi";

static int len(const char *s) {
    return (int)strlen(s);
}

int main(void) {
    const int *pc = &ci;
    int *const cp = (int *)&vi;
    char buf[8];
    const struct s cs = {5};
    const char *walk = msg;

    printf("%d %d %d %d\n", __builtin_types_compatible_p(int *, const int *),
           __builtin_types_compatible_p(const int, int), __builtin_types_compatible_p(typeof(ci), const int),
           __builtin_types_compatible_p(typeof(pc), const int *));
    printf("%d %d %d %d\n", _Generic(pc, const int *: 1, int *: 2, default: 0), _Generic(ci, int: 1, default: 0),
           _Generic(&ci, int *: 1, const int *: 2, default: 0), _Generic(cp, int *: 1, default: 0));
    strcpy(buf, msg);
    *cp = 4;
    walk++;
    printf("%d %d %s %d %d %c %zu\n", *pc, vi, buf, len(buf), cs.a, *walk, sizeof(const volatile long));
    return 0;
}
