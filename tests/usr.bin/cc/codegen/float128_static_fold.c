/* a static _Float128 initialized by a constant expression of another binary floating type */
/* cc-std: c11 */
#include <stdio.h>
#include <string.h>

static _Float128 a = 1.0 / 3;
_Float128 b = 2.5 * 2;
_Float128 c = (float)0.1 + 1;
static _Float128 d[3] = {1.0 / 7, 3, -0.5f * 3};
struct s {
    int n;
    _Float128 q;
} e = {1, 2.0L / 3};

static void show(const char *name, const _Float128 *p) {
    unsigned long long w[2];

    memcpy(w, p, sizeof(w));
    printf("%s %016llx%016llx\n", name, w[1], w[0]);
}

int main(void) {
    static _Float128 f = 1.0f / 3 + 0.25;

    show("a", &a);
    show("b", &b);
    show("c", &c);
    show("d0", &d[0]);
    show("d1", &d[1]);
    show("d2", &d[2]);
    show("e", &e.q);
    show("f", &f);
    return 0;
}
