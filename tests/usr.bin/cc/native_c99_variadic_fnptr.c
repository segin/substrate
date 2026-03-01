#include <stdarg.h>
#include <string.h>

typedef void pfunc_t(const char *, ...);

static char outbuf[64];

static void sink(const char *tag, ...) {
    va_list ap;
    const char *s;
    int n;

    va_start(ap, tag);
    s = va_arg(ap, const char *);
    n = va_arg(ap, int);
    va_end(ap);

    outbuf[0] = '\0';
    strcat(outbuf, tag);
    strcat(outbuf, ":");
    strcat(outbuf, s);
    strcat(outbuf, ":");
    if (n == 7)
        strcat(outbuf, "7");
    else
        strcat(outbuf, "X");
}

int main(void) {
    pfunc_t *fp = sink;
    fp("A", "B", 7);
    return strcmp(outbuf, "A:B:7");
}
