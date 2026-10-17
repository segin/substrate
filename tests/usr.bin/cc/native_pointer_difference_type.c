/*
 * The difference of two pointers has type ptrdiff_t (C11 6.5.6p9): long
 * on x86-64 and int on i386, as gcc gives it.  cc typed it int on both
 * targets, so sizeof (p - q) was 4 at -m64.
 */
typedef __PTRDIFF_TYPE__ ptrdiff_t;

int main(void) {
    static char buf[16];
    char *p = buf + 12;
    char *q = buf;

    if (sizeof(p - q) != sizeof(ptrdiff_t)) {
        return 1;
    }
    if (_Generic(p - q, ptrdiff_t: 0, default: 1)) {
        return 2;
    }
    return p - q == 12 && q - p == -12 ? 0 : 3;
}
