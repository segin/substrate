/*
 * pwd - print the working directory.
 *
 *   pwd [-L | -P]
 *
 * -L (default) prints $PWD when it is a valid absolute path (no "."/".."
 * components) naming the current directory; -P always resolves the
 * physical path via getcwd(3).
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

static const char *prog = "pwd";

/* getcwd into a buffer grown past PATH_MAX on ERANGE (PWD-03). */
static char *
xgetcwd(void)
{
    size_t cap = PATH_MAX > 0 ? (size_t)PATH_MAX : 4096;

    for (;;) {
        char *buf = malloc(cap);
        if (buf == NULL)
            return NULL;
        if (getcwd(buf, cap) != NULL)
            return buf;
        free(buf);
        if (errno != ERANGE || cap > ((size_t)1 << 20))
            return NULL;
        cap *= 2;
    }
}

/* Is $PWD usable for logical mode: absolute, free of "."/".." components,
 * and naming the same directory as "."? */
static int
logical_ok(const char *pwd)
{
    struct stat a, b;
    const char *p;

    if (pwd == NULL || pwd[0] != '/')
        return 0;
    for (p = pwd; *p != '\0'; p++) {
        if (p[0] == '/' && p[1] == '.') {
            const char *q = p + 2;
            if (*q == '/' || *q == '\0')                      /* "/."  */
                return 0;
            if (q[0] == '.' && (q[1] == '/' || q[1] == '\0')) /* "/.." */
                return 0;
        }
    }
    if (stat(pwd, &a) != 0 || stat(".", &b) != 0)
        return 0;
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

int
main(int argc, char *argv[])
{
    int         logical = 1;   /* POSIX default is -L */
    char       *cwd = NULL;
    const char *out;
    int         i;

    if (argv[0] != NULL && argv[0][0] != '\0')
        prog = argv[0];

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0)
            break;
        if (strcmp(a, "-L") == 0)
            logical = 1;
        else if (strcmp(a, "-P") == 0)
            logical = 0;
        else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "%s: invalid option %s\nusage: pwd [-L|-P]\n", prog, a);
            return 2;
        } else {
            break;   /* operand — pwd takes none, but stop the option scan */
        }
    }

    if (logical) {
        const char *pwd = getenv("PWD");
        if (logical_ok(pwd)) {
            out = pwd;
        } else {
            cwd = xgetcwd();
            out = cwd;
        }
    } else {
        cwd = xgetcwd();
        out = cwd;
    }

    if (out == NULL) {                                        /* PWD-01/02 */
        fprintf(stderr, "%s: %s\n", prog, strerror(errno));
        return 1;
    }

    if (printf("%s\n", out) < 0 || fflush(stdout) != 0) {     /* PWD-05 */
        fprintf(stderr, "%s: write error: %s\n", prog, strerror(errno));
        free(cwd);
        return 1;
    }

    free(cwd);
    return 0;
}
