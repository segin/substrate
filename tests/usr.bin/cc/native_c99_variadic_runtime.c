#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int check_args(const char *fmt, ...) {
    va_list ap;
    va_list ap_fmt;
    va_list cp;
    int a;
    long b;
    int ca;
    long cb;
    char out[64];
    int n;

    va_start(ap, fmt);
    va_copy(ap_fmt, ap);
    va_copy(cp, ap);

    a = va_arg(ap, int);
    b = va_arg(ap, long);
    ca = va_arg(cp, int);
    cb = va_arg(cp, long);

    n = vsnprintf(out, sizeof(out), fmt, ap_fmt);
    va_end(ap_fmt);
    va_end(cp);
    va_end(ap);

    if (a != 7 || b != 11) return 1;
    if (ca != 7 || cb != 11) return 2;
    if (n != 4) return 3;
    if (strcmp(out, "7:11") != 0) return 4;
    return 0;
}

int main(void) {
    return check_args("%d:%ld", 7, 11L);
}
