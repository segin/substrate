/*
 * which - locate a program file in the user's PATH
 *
 * Substrate which(1) implementation.  Single binary that supports the
 * full superset of BSD and GNU which options.  POSIX doesn't standardise
 * which (the standard answer is `command -v`), so there's no POSIX
 * flavour to honour; the BSD and GNU semantics agree on the common
 * case and only diverge in flags.
 *
 * Common operation: for each NAME on the command line, walk $PATH and
 * print the first executable found.  Exit 0 if all found, non-zero if
 * any wasn't.
 *
 * BSD flags:
 *   -a       Print all matches in PATH order, not just the first.
 *   -s       Silent.  Exit code only.  (No output.)
 *
 * GNU flags:
 *   -a, --all                  Same as BSD -a.
 *   --skip-dot                 Ignore PATH entries that start with `.`.
 *   --skip-tilde               Ignore PATH entries that start with `~`.
 *   --show-dot                 Print `./foo` rather than `$PWD/foo`.
 *   --show-tilde               Print `~/foo` rather than `$HOME/foo`.
 *   --tty-only                 Only do --show-dot / --show-tilde
 *                              transformations when stdout is a tty.
 *   -i, --read-alias           Read aliases from stdin (NOT IMPLEMENTED —
 *                              warned to stderr, ignored).
 *   --skip-alias               Suppress alias matching (no-op for us).
 *   --read-functions           Read shell functions from stdin (NOT
 *                              IMPLEMENTED).
 *   --skip-functions           Suppress function matching (no-op).
 *   -v, --version              Print version and exit.
 *   --help                     Print usage and exit.
 *
 * Exit status:
 *   0   every NAME was found and printed
 *   1   one or more NAMEs were not found
 *   2   invalid usage (bad option, no arguments)
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "getopt.h"
#include <sys/stat.h>

#define PROGRAM_VERSION "substrate-1.0"

static void usage(FILE *out)
{
    fprintf(out,
"Usage: which [options] NAME...\n"
"Print the full path of each executable NAME found in $PATH.\n"
"\n"
"Options:\n"
"  -a, --all              print every match in $PATH, not just the first\n"
"  -s                     silent (exit code only, no stdout output)\n"
"      --skip-dot         ignore $PATH entries starting with `.'\n"
"      --skip-tilde       ignore $PATH entries starting with `~'\n"
"      --show-dot         print `./foo' rather than `$PWD/foo'\n"
"      --show-tilde       print `~/foo' rather than `$HOME/foo'\n"
"      --tty-only         only apply --show-dot/--show-tilde when stdout is a TTY\n"
"  -i, --read-alias       read aliases from stdin (not implemented; ignored)\n"
"      --skip-alias       skip aliases (no-op)\n"
"      --read-functions   read functions from stdin (not implemented; ignored)\n"
"      --skip-functions   skip functions (no-op)\n"
"  -v, --version          print version and exit\n"
"      --help             print this help and exit\n"
"\n"
"Exit status: 0 if every NAME was found, 1 if any wasn't, 2 on usage error.\n"
    );
}

/* True if `path` is a regular (or symlink-resolved-to-regular) file
 * that is executable by the calling process. */
static bool is_executable(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
    return access(path, X_OK) == 0;
}

/* Apply the GNU --show-dot / --show-tilde transformations to a found
 * absolute path.  Caller passes the path it would normally print;
 * returned pointer is either the same path or a freshly malloc'd
 * shorter form.  Caller frees the result iff it differs from `path`
 * (i.e. iff a transformation actually fired).  Pass NULL `cwd`/`home`
 * to disable the corresponding transformation. */
static char *display_path(const char *path, const char *cwd, const char *home)
{
    if (cwd && *cwd) {
        size_t n = strlen(cwd);
        if (strncmp(path, cwd, n) == 0 && path[n] == '/') {
            size_t out_size = strlen(path + n) + 3;
            char *out = malloc(out_size);
            if (out) {
                snprintf(out, out_size, ".%s", path + n);
                return out;
            }
        }
    }
    if (home && *home) {
        size_t n = strlen(home);
        if (strncmp(path, home, n) == 0 && path[n] == '/') {
            size_t out_size = strlen(path + n) + 3;
            char *out = malloc(out_size);
            if (out) {
                snprintf(out, out_size, "~%s", path + n);
                return out;
            }
        }
    }
    return (char *)path;
}

/* Walk $PATH for `name`.  If `show_all` is true, prints every match;
 * otherwise prints the first.  Returns true iff at least one match was
 * printed (or, in --silent mode, located).  Honours the GNU skip-dot,
 * skip-tilde, show-dot, show-tilde flags. */
static bool search(const char *name,
                   const char *pathenv,
                   bool show_all,
                   bool silent,
                   bool skip_dot,
                   bool skip_tilde,
                   bool show_dot,
                   bool show_tilde,
                   bool tty_only)
{
    /* If NAME contains a slash, $PATH isn't searched — POSIX-style
     * `command` and BSD/GNU which agree on this.  Just check that
     * exact path. */
    if (strchr(name, '/')) {
        if (is_executable(name)) {
            if (!silent) puts(name);
            return true;
        }
        return false;
    }

    if (!pathenv || !*pathenv) return false;

    char *path_copy = strdup(pathenv);
    if (!path_copy) return false;

    bool found_any = false;
    bool tty = isatty(STDOUT_FILENO);
    const char *cwd = NULL, *home = NULL;
    char cwdbuf[4096];
    if (show_dot && (!tty_only || tty)) {
        cwd = getcwd(cwdbuf, sizeof(cwdbuf));
        if (cwd == NULL)          /* don't silently drop --show-dot (WHICH-01) */
            fprintf(stderr, "which: getcwd: %s\n", strerror(errno));
    }
    if (show_tilde && (!tty_only || tty)) {
        home = getenv("HOME");
    }

    /* Walk colon-separated PATH. */
    char *saveptr = NULL;
    for (char *dir = strtok_r(path_copy, ":", &saveptr);
         dir != NULL;
         dir = strtok_r(NULL, ":", &saveptr))
    {
        if (skip_dot && dir[0] == '.') continue;
        if (skip_tilde && dir[0] == '~') continue;

        /* An empty PATH component means the current directory (POSIX). */
        const char *effective = (*dir) ? dir : ".";

        size_t want = strlen(effective) + 1 + strlen(name) + 1;
        char *candidate = malloc(want);
        if (!candidate) continue;
        snprintf(candidate, want, "%s/%s", effective, name);

        if (is_executable(candidate)) {
            if (!silent) {
                char *display = display_path(candidate, cwd, home);
                puts(display);
                if (display != candidate) free(display);
            }
            found_any = true;
            if (!show_all) {
                free(candidate);
                break;
            }
        }
        free(candidate);
    }
    free(path_copy);
    return found_any;
}

/* Long option values for the no-short-form flags. */
enum {
    OPT_HELP = 256,
    OPT_SKIP_DOT,
    OPT_SKIP_TILDE,
    OPT_SHOW_DOT,
    OPT_SHOW_TILDE,
    OPT_TTY_ONLY,
    OPT_SKIP_ALIAS,
    OPT_READ_FUNCTIONS,
    OPT_SKIP_FUNCTIONS,
};

static const struct option longopts[] = {
    { "all",            no_argument,       NULL, 'a' },
    { "read-alias",     no_argument,       NULL, 'i' },
    { "skip-alias",     no_argument,       NULL, OPT_SKIP_ALIAS },
    { "read-functions", no_argument,       NULL, OPT_READ_FUNCTIONS },
    { "skip-functions", no_argument,       NULL, OPT_SKIP_FUNCTIONS },
    { "skip-dot",       no_argument,       NULL, OPT_SKIP_DOT },
    { "skip-tilde",     no_argument,       NULL, OPT_SKIP_TILDE },
    { "show-dot",       no_argument,       NULL, OPT_SHOW_DOT },
    { "show-tilde",     no_argument,       NULL, OPT_SHOW_TILDE },
    { "tty-only",       no_argument,       NULL, OPT_TTY_ONLY },
    { "version",        no_argument,       NULL, 'v' },
    { "help",           no_argument,       NULL, OPT_HELP },
    { NULL,             0,                 NULL, 0   },
};

int main(int argc, char **argv)
{
    bool show_all = false;
    bool silent = false;
    bool skip_dot = false;
    bool skip_tilde = false;
    bool show_dot = false;
    bool show_tilde = false;
    bool tty_only = false;

    int opt;
    while ((opt = getopt_long(argc, argv, "+aisv", longopts, NULL)) != -1) {
        switch (opt) {
        case 'a': show_all = true; break;
        case 's': silent = true; break;
        case 'i':
            /* --read-alias: would slurp stdin for `alias foo=...`
             * entries.  Substrate has no shell-alias integration
             * surface; warn once and move on. */
            fprintf(stderr,
                    "which: --read-alias not implemented; ignoring stdin aliases\n");
            break;
        case OPT_SKIP_ALIAS:      /* No-op: we don't read aliases anyway. */ break;
        case OPT_READ_FUNCTIONS:
            fprintf(stderr,
                    "which: --read-functions not implemented; ignoring\n");
            break;
        case OPT_SKIP_FUNCTIONS:  /* No-op. */                              break;
        case OPT_SKIP_DOT:        skip_dot = true;                          break;
        case OPT_SKIP_TILDE:      skip_tilde = true;                        break;
        case OPT_SHOW_DOT:        show_dot = true;                          break;
        case OPT_SHOW_TILDE:      show_tilde = true;                        break;
        case OPT_TTY_ONLY:        tty_only = true;                          break;
        case 'v':
            printf("which %s\n", PROGRAM_VERSION);
            return 0;
        case OPT_HELP:
            usage(stdout);
            return 0;
        default:
            usage(stderr);
            return 2;
        }
    }

    if (optind >= argc) {
        usage(stderr);
        return 2;
    }

    const char *pathenv = getenv("PATH");

    int missing = 0;
    for (int i = optind; i < argc; i++) {
        if (!search(argv[i], pathenv, show_all, silent,
                    skip_dot, skip_tilde, show_dot, show_tilde, tty_only))
        {
            if (!silent) {
                fprintf(stderr, "which: no %s in (%s)\n",
                        argv[i], pathenv ? pathenv : "");
            }
            missing++;
        }
    }
    return missing ? 1 : 0;
}
