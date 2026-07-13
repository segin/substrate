/*
 * yes - repeatedly output a line until killed.
 *
 *   yes [STRING...]
 *
 * With operands, prints them space-joined; otherwise prints "y". Exits
 * (nonzero) when the output can no longer be written — e.g. the reader of
 * a pipe closed it (EPIPE) — instead of looping forever.
 */

#include <stdio.h>

int
main(int argc, char *argv[])
{
    if (argc > 1) {
        for (;;) {
            int i;
            for (i = 1; i < argc; i++) {
                if (fputs(argv[i], stdout) == EOF)
                    return 1;
                if (putchar(i + 1 < argc ? ' ' : '\n') == EOF)
                    return 1;
            }
        }
    } else {
        for (;;)
            if (fputs("y\n", stdout) == EOF)
                return 1;
    }
    return 0;
}
