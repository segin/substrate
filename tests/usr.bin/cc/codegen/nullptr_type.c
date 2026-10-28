/*
 * nullptr has its own type, nullptr_t (C23 6.4.4.6, 7.21.2): pointer-sized,
 * the type typeof (nullptr) and <stddef.h> name, converting to any pointer
 * type (a null pointer) and to bool (false), comparing equal to null
 * pointers, and passed to a variadic function as a void * would be.
 *
 * cc-std: c23
 * cc-gcc: 14 (-std=c23)
 */
#include <stddef.h>
#include <stdio.h>

static int is_null(nullptr_t n, const void *p) { return n == p; }

static const char *kind(void *p) { return p == nullptr ? "null" : "set"; }

int main(void) {
    int x = 3;
    int *p = nullptr;
    char *q = &(char){'a'};
    bool b = nullptr;
    nullptr_t z = nullptr;
    void *v = z;
    typeof(nullptr) w = 0;

    printf("%d %d %d %d\n", p == nullptr, nullptr == p, q != nullptr, nullptr == nullptr);
    printf("%d %d %d %d\n", (int)b, v == nullptr, w == nullptr, is_null(nullptr, p));
    printf("%s %s\n", kind(nullptr), kind(&x));
    printf("%u %d\n", (unsigned)(sizeof(nullptr_t) == sizeof(void *)),
           _Generic(nullptr, nullptr_t: 1, void *: 2, default: 3));
    printf("%d %d\n", nullptr ? 1 : 0, !nullptr);
    printf("%s\n", (char *)nullptr == NULL ? "cast ok" : "cast bad");
    return 0;
}
