/*
 * tabs - set terminal tab stops.
 *
 *   tabs [-N]            every N columns (default 8)
 *   tabs n1,n2,...       explicit increasing tab-stop columns
 *
 * Clears the existing tab stops with ESC[3g (NOT ESC c, which is a full
 * terminal reset — RIS — that also clears the screen and resets colors),
 * then sets the requested stops with cursor moves and HTS (ESC H).
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TABS 512
#define MAX_COL  1024

/* Clear all tab stops, then set one at each (1-based, increasing) column. */
static void
set_tabs(const int *cols, int n)
{
    int cur = 1;
    int i;

    fputs("\r\033[3g", stdout);          /* CR, then clear all tab stops */
    for (i = 0; i < n; i++) {
        if (cols[i] <= cur || cols[i] > MAX_COL)
            continue;
        printf("\033[%dC\033H", cols[i] - cur);  /* move right, set HTS */
        cur = cols[i];
    }
    fputc('\r', stdout);                 /* back to column 1 */
}

int
main(int argc, char **argv)
{
    int  cols[MAX_TABS];
    int  n = 0;
    int  every = 8;
    int  have_list = 0;
    int  i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1] == '-' && a[2] == '\0') {   /* "--" */
            i++;
            break;
        }
        if (a[0] == '-' && isdigit((unsigned char)a[1])) {
            every = atoi(a + 1);
            if (every < 1) {
                fprintf(stderr, "tabs: bad tab increment '%s'\n", a);
                return 1;
            }
        } else if (isdigit((unsigned char)a[0])) {
            /* Explicit list: comma- or space-separated columns. */
            const char *p = a;
            while (*p) {
                char *end;
                long  v = strtol(p, &end, 10);
                if (end == p) break;
                if (v >= 1 && v <= MAX_COL && n < MAX_TABS) {
                    if (n > 0 && v <= cols[n - 1]) {
                        fprintf(stderr, "tabs: tab stops must increase\n");
                        return 1;
                    }
                    cols[n++] = (int)v;
                }
                p = end;
                while (*p == ',' || *p == ' ')
                    p++;
            }
            have_list = 1;
        } else {
            fprintf(stderr, "tabs: unrecognized argument '%s'\n", a);
            return 1;
        }
    }

    if (!have_list) {
        int c;
        for (c = 1 + every; c <= MAX_COL && n < MAX_TABS; c += every)
            cols[n++] = c;
    }

    set_tabs(cols, n);
    if (fflush(stdout) != 0) {
        perror("tabs");
        return 1;
    }
    return 0;
}
