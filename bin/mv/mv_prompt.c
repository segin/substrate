#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>

#include "mv_prompt.h"

bool mv_prompt_yn(const char *fmt, ...)
{
    va_list ap;
    int ch;
    int first = EOF;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);

    /* Read first byte then drain to newline.  EOF means "no". */
    while ((ch = getchar()) != EOF && ch != '\n') {
        if (first == EOF) {
            first = ch;
        }
    }

    return first != EOF && (first == 'y' || first == 'Y');
}
