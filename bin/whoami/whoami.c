/*
 * whoami(1) — print the user name associated with the current
 * effective user ID.  Equivalent to `id -un`.
 *
 * POSIX/BSD whoami takes no operands; GNU additionally honours
 * --help and --version.  We accept those long options and reject
 * any other argument.
 */

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *prog = "whoami";

static void
usage(FILE *f)
{
    fprintf(f,
        "Usage: %s [OPTION]...\n"
        "Print the user name associated with the current effective user ID.\n"
        "Same as id -un.\n\n"
        "      --help        display this help and exit\n"
        "      --version     output version information and exit\n",
        prog);
}

int
main(int argc, char **argv)
{
    int i;
    struct passwd *pw;
    uid_t euid;

    if (argc > 0 && argv[0] != NULL) prog = argv[0];

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (strcmp(a, "--version") == 0) {
            printf("whoami (Substrate) 1.0\n");
            return 0;
        }
        if (strcmp(a, "--") == 0) {
            /* POSIX argument terminator — anything after is an
             * operand, but whoami doesn't take operands, so flag
             * the next arg if present. */
            if (i + 1 < argc) {
                fprintf(stderr, "%s: extra operand '%s'\n", prog, argv[i + 1]);
                return 1;
            }
            break;
        }
        fprintf(stderr, "%s: extra operand '%s'\n", prog, a);
        fprintf(stderr, "Try '%s --help' for more information.\n", prog);
        return 1;
    }

    euid = geteuid();
    errno = 0;
    pw = getpwuid(euid);
    if (pw == NULL) {
        /* No matching entry — POSIX leaves the behaviour
         * implementation-defined; GNU prints the uid as a fallback
         * but exits non-zero.  Match GNU. */
        if (errno != 0)
            fprintf(stderr, "%s: cannot find name for user ID %u: %s\n",
                    prog, (unsigned)euid, strerror(errno));
        else
            fprintf(stderr, "%s: cannot find name for user ID %u\n",
                    prog, (unsigned)euid);
        return 1;
    }

    if (puts(pw->pw_name) == EOF) {
        fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
        return 1;
    }
    return 0;
}
