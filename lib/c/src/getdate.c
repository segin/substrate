/*
 * getdate.c — reentrant getdate_r(3): parse a date/time string against the
 * strptime(3) templates listed in the file named by $DATEMSK.
 *
 * Unspecified fields default to the current local date/time.  Returns 0 on
 * success, or one of the standard getdate error codes:
 *   1 $DATEMSK unset or empty   2 template file cannot be opened
 *   7 no template matched the input
 */

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int getdate_err;        /* set by the non-reentrant getdate(); see SUSv4 */

int
getdate_r(const char *string, struct tm *tm)
{
    const char *datemsk;
    FILE       *f;
    char        line[256];
    time_t      now;
    int         matched = 0;

    if (string == NULL || tm == NULL)
        return 1;
    datemsk = getenv("DATEMSK");
    if (datemsk == NULL || datemsk[0] == '\0')
        return 1;
    f = fopen(datemsk, "r");
    if (f == NULL)
        return 2;

    now = time(NULL);

    while (fgets(line, sizeof line, f) != NULL) {
        size_t     n = strlen(line);
        struct tm  base;
        char      *end;

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (line[0] == '\0')
            continue;

        /* Seed unspecified fields with "now" so a template that only sets,
         * say, the time still yields today's date. */
        localtime_r(&now, &base);
        end = strptime(string, line, &base);
        if (end != NULL) {
            /* Accept a full match, or one with only trailing whitespace. */
            while (*end == ' ' || *end == '\t')
                end++;
            if (*end == '\0') {
                *tm = base;
                matched = 1;
                break;
            }
        }
    }
    fclose(f);
    return matched ? 0 : 7;
}
