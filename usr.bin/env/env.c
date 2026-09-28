/*
 * env - set the environment for command invocation.
 *
 *   env [-i] [-u name]... [--] [name=value]... [utility [argument...]]
 *
 * With no utility, the resulting environment is written to standard
 * output, one name=value per line.  -i (or the obsolescent lone "-")
 * starts from an empty environment; -u removes a variable.  Exit status
 * follows POSIX: the utility's own status, 125 if env itself fails, 126
 * if the utility is found but cannot be run, 127 if it cannot be found.
 *
 * The new environment is built in a private array and installed as
 * environ just before exec, rather than edited with setenv/unsetenv,
 * so -i never hands libc an array it did not allocate.
 */

/* glibc declares environ in <unistd.h> only for _GNU_SOURCE (host builds). */
#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char **envv;
static size_t envc;

static void
usage(void)
{
    fprintf(stderr,
        "usage: env [-i] [-u name]... [name=value]... [utility [argument...]]\n");
    exit(125);
}

/* Length of the name part of "name=value" (the whole string if no '='). */
static size_t
name_len(const char *s)
{
    const char *eq = strchr(s, '=');

    return eq != NULL ? (size_t)(eq - s) : strlen(s);
}

/* Drop every entry whose name is the first len bytes of name. */
static void
env_remove(const char *name, size_t len)
{
    size_t i = 0, j = 0;

    for (; i < envc; i++)
        if (!(name_len(envv[i]) == len && strncmp(envv[i], name, len) == 0))
            envv[j++] = envv[i];
    envc = j;
    envv[envc] = NULL;
}

/* Replace or append "name=value" (s points into argv, which outlives us). */
static void
env_set(char *s)
{
    env_remove(s, name_len(s));
    envv[envc++] = s;
    envv[envc] = NULL;
}

int
main(int argc, char *argv[])
{
    size_t n = 0, k;
    int i = 1;

    while (environ != NULL && environ[n] != NULL)
        n++;
    /* Room for the inherited entries plus every argument as a new one. */
    envv = calloc(n + (size_t)argc + 1, sizeof(*envv));
    if (envv == NULL) {
        fprintf(stderr, "env: %s\n", strerror(errno));
        return 125;
    }
    for (k = 0; k < n; k++)
        envv[envc++] = environ[k];

    while (i < argc && argv[i][0] == '-') {
        const char *a = argv[i];

        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (strcmp(a, "-") == 0 || strcmp(a, "-i") == 0) {
            envc = 0;
            envv[0] = NULL;
            i++;
        } else if (strncmp(a, "-u", 2) == 0) {
            const char *name = a[2] != '\0' ? a + 2 : argv[i + 1];

            if (name == NULL || *name == '\0' || strchr(name, '=') != NULL) {
                fprintf(stderr, "env: invalid variable name for -u\n");
                usage();
            }
            env_remove(name, strlen(name));
            i += a[2] != '\0' ? 1 : 2;
        } else {
            fprintf(stderr, "env: invalid option '%s'\n", a);
            usage();
        }
    }

    for (; i < argc && strchr(argv[i], '=') != NULL; i++) {
        if (argv[i][0] == '=') {
            fprintf(stderr, "env: invalid assignment '%s'\n", argv[i]);
            return 125;
        }
        env_set(argv[i]);
    }

    if (i >= argc) {
        for (k = 0; k < envc; k++)
            if (puts(envv[k]) == EOF)
                return 125;
        return fflush(stdout) == 0 ? 0 : 125;
    }

    environ = envv;
    execvp(argv[i], &argv[i]);
    fprintf(stderr, "env: %s: %s\n", argv[i], strerror(errno));
    return errno == ENOENT ? 127 : 126;
}
