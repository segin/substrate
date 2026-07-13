/*
 * nproc - print the number of processing units available.
 *
 *   nproc [--all] [--ignore=N]
 *
 * Defaults to the number of processors currently online
 * (sysconf(_SC_NPROCESSORS_ONLN)); --all reports the number configured
 * (_SC_NPROCESSORS_CONF).  The previous stub always printed "1", which
 * silently serialized `make -j$(nproc)` on every SMP machine.
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(FILE *f)
{
    fputs("Usage: nproc [--all] [--ignore=N]\n", f);
}

int
main(int argc, char **argv)
{
    int   all = 0;
    long  ignore = 0;
    int   i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--all") == 0) {
            all = 1;
        } else if (strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "--version") == 0) {
            printf("nproc (Substrate) 1.0\n");
            return 0;
        } else if (strncmp(a, "--ignore=", 9) == 0) {
            char *end;
            errno = 0;
            ignore = strtol(a + 9, &end, 10);
            if (end == a + 9 || *end != '\0' || errno == ERANGE || ignore < 0) {
                fprintf(stderr, "nproc: invalid number '%s'\n", a + 9);
                return 1;
            }
        } else if (strcmp(a, "--ignore") == 0 && i + 1 < argc) {
            char *end;
            errno = 0;
            ignore = strtol(argv[++i], &end, 10);
            if (*end != '\0' || errno == ERANGE || ignore < 0) {
                fprintf(stderr, "nproc: invalid number '%s'\n", argv[i]);
                return 1;
            }
        } else {
            fprintf(stderr, "nproc: invalid option '%s'\n", a);
            usage(stderr);
            return 1;
        }
    }

    long n = sysconf(all ? _SC_NPROCESSORS_CONF : _SC_NPROCESSORS_ONLN);
    if (n < 1)
        n = 1;                 /* sysconf failed or reported nonsense */

    if (ignore > 0) {
        if (ignore >= n)
            n = 1;             /* GNU: never report fewer than one */
        else
            n -= ignore;
    }

    printf("%ld\n", n);
    return 0;
}
