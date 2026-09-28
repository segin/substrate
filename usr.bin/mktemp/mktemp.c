/*
 * mktemp - create a temporary file or directory with a unique name.
 *
 *   mktemp [-dqu] [-p dir] [-t] [template]
 *
 * The trailing run of X's in template (at least three) is replaced with
 * random letters and digits, and the file (mode 0600) or directory (mode
 * 0700) is created exclusively, retrying with new names on collision.
 * The resulting name is written to standard output.  With no template,
 * tmp.XXXXXXXXXX is created in $TMPDIR or /tmp.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#define DEFAULT_TEMPLATE "tmp.XXXXXXXXXX"
#define MIN_X 3
#define ATTEMPTS 1000

static bool quiet;

static void
usage(void)
{
    fprintf(stderr, "usage: mktemp [-dqu] [-p dir] [-t] [template]\n");
    exit(1);
}

static void
complain(const char *fmt, const char *arg, int errnum)
{
    if (quiet)
        return;
    fprintf(stderr, "mktemp: ");
    fprintf(stderr, fmt, arg);
    if (errnum != 0)
        fprintf(stderr, ": %s", strerror(errnum));
    fputc('\n', stderr);
}

/* $TMPDIR if it is set and non-empty, else /tmp. */
static const char *
tmpdir(void)
{
    const char *d = getenv("TMPDIR");

    return d != NULL && *d != '\0' ? d : "/tmp";
}

int
main(int argc, char *argv[])
{
    static const char chars[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    bool make_dir = false, dry_run = false, use_t = false, use_p = false;
    const char *pdir = NULL, *tmpl, *dir = NULL;
    char path[PATH_MAX];
    size_t len, nx;
    int c, i;

    while ((c = getopt(argc, argv, "dqp:tu")) != -1) {
        switch (c) {
        case 'd':
            make_dir = true;
            break;
        case 'q':
            quiet = true;
            break;
        case 'p':
            use_p = true;
            pdir = optarg;
            break;
        case 't':
            use_t = true;
            break;
        case 'u':
            dry_run = true;
            break;
        default:
            usage();
        }
    }
    argc -= optind;
    argv += optind;
    if (argc > 1)
        usage();

    if (argc == 0) {
        tmpl = DEFAULT_TEMPLATE;
        use_p = true;
    } else {
        tmpl = argv[0];
    }

    /*
     * Where a relative template goes: -t prefers $TMPDIR, then -p's
     * directory, then /tmp; -p (or no template) uses its directory, or
     * $TMPDIR/tmp when it is empty.  Otherwise the template is used as
     * given, relative to the current directory.
     */
    if (use_t) {
        const char *e = getenv("TMPDIR");

        if (e != NULL && *e != '\0')
            dir = e;
        else if (pdir != NULL && *pdir != '\0')
            dir = pdir;
        else
            dir = "/tmp";
    } else if (use_p) {
        dir = pdir != NULL && *pdir != '\0' ? pdir : tmpdir();
    }
    if (dir != NULL && strchr(tmpl, '/') != NULL) {
        complain("invalid template '%s': contains a directory separator", tmpl, 0);
        return 1;
    }

    if (dir != NULL) {
        size_t dlen = strlen(dir);
        const char *sep = dlen > 0 && dir[dlen - 1] == '/' ? "" : "/";

        if ((size_t)snprintf(path, sizeof(path), "%s%s%s", dir, sep, tmpl)
            >= sizeof(path)) {
            complain("template '%s' is too long", tmpl, 0);
            return 1;
        }
    } else {
        if (strlen(tmpl) >= sizeof(path)) {
            complain("template '%s' is too long", tmpl, 0);
            return 1;
        }
        memcpy(path, tmpl, strlen(tmpl) + 1);
    }

    len = strlen(path);
    for (nx = 0; nx < len && path[len - 1 - nx] == 'X'; nx++)
        ;
    if (nx < MIN_X) {
        complain("too few X's in template '%s'", tmpl, 0);
        return 1;
    }

    for (i = 0; i < ATTEMPTS; i++) {
        for (size_t k = len - nx; k < len; k++)
            path[k] = chars[arc4random_uniform(sizeof(chars) - 1)];

        if (dry_run) {
            struct stat st;

            if (lstat(path, &st) == 0)
                continue;
            break;
        }
        if (make_dir) {
            if (mkdir(path, 0700) == 0)
                break;
        } else {
            int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);

            if (fd >= 0) {
                close(fd);
                break;
            }
        }
        if (errno != EEXIST) {
            complain(make_dir ? "failed to create directory '%s'"
                         : "failed to create file '%s'", path, errno);
            return 1;
        }
    }
    if (i == ATTEMPTS) {
        complain("could not find an unused name for '%s'", tmpl, 0);
        return 1;
    }

    if (puts(path) == EOF || fflush(stdout) != 0) {
        int saved = errno;

        /* Nobody learns the name, so don't leave it behind. */
        if (dry_run)
            ;
        else if (make_dir)
            rmdir(path);
        else
            unlink(path);
        complain("write error%s", "", saved);
        return 1;
    }
    return 0;
}
