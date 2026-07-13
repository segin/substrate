/*
 * tee - copy standard input to standard output and to each FILE.
 *
 *   tee [-ai] [--] [FILE...]
 *
 * Reads stdin and writes it to stdout and to every FILE operand.  Short
 * writes and EINTR are handled on every descriptor; a FILE that cannot be
 * opened or written is diagnosed and makes the exit status nonzero, but
 * copying continues to the remaining destinations (POSIX).
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *prog = "tee";

/* Write the whole buffer, retrying EINTR and short writes (TEE-01/04). */
static int
full_write(int fd, const char *buf, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    int   append = 0;
    int   ignore_int = 0;
    int   i = 1;
    int   rc = 0;

    if (argv[0] && argv[0][0])
        prog = argv[0];

    /* Option parsing: -a, -i, combined (-ai), long forms, and "--". */
    for (; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0')
            break;                       /* operand (incl. "-") */
        if (strcmp(a, "--") == 0) { i++; break; }
        if (strcmp(a, "--append") == 0) { append = 1; continue; }
        if (strcmp(a, "--ignore-interrupts") == 0) { ignore_int = 1; continue; }
        if (strcmp(a, "--help") == 0) {
            printf("usage: %s [-ai] [--] [FILE...]\n", prog);
            return 0;
        }
        if (a[0] == '-' && a[1] != '-') {
            for (const char *p = a + 1; *p; p++) {
                switch (*p) {
                case 'a': append = 1; break;
                case 'i': ignore_int = 1; break;
                case 'p': break;         /* accepted, no diagnose-mode change */
                default:
                    fprintf(stderr, "%s: invalid option -- '%c'\n", prog, *p);
                    fprintf(stderr, "usage: %s [-ai] [--] [FILE...]\n", prog);
                    return 1;
                }
            }
            continue;
        }
        fprintf(stderr, "%s: unknown option %s\n", prog, a);
        return 1;
    }

    if (ignore_int)
        signal(SIGINT, SIG_IGN);           /* -i: keep copying past ^C (TEE-08) */

    int   nfiles = argc - i;
    int  *fds = NULL;
    if (nfiles > 0) {
        fds = malloc((size_t)nfiles * sizeof(*fds));   /* size to argc (TEE-03) */
        if (!fds) { perror(prog); return 1; }
    }

    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    for (int k = 0; k < nfiles; k++) {
        fds[k] = open(argv[i + k], flags, 0666);
        if (fds[k] < 0) {                 /* diagnose + nonzero, keep going (TEE-02) */
            fprintf(stderr, "%s: %s: %s\n", prog, argv[i + k], strerror(errno));
            rc = 1;
        }
    }

    char   buf[65536];
    ssize_t n;
    for (;;) {
        n = read(0, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR)           /* don't truncate input (TEE-04) */
                continue;
            fprintf(stderr, "%s: read error: %s\n", prog, strerror(errno));
            rc = 1;
            break;
        }
        if (n == 0)
            break;
        if (full_write(STDOUT_FILENO, buf, (size_t)n) != 0) {
            fprintf(stderr, "%s: stdout: %s\n", prog, strerror(errno));
            rc = 1;
        }
        for (int k = 0; k < nfiles; k++) {
            if (fds[k] < 0)
                continue;
            if (full_write(fds[k], buf, (size_t)n) != 0) {
                fprintf(stderr, "%s: %s: %s\n", prog, argv[i + k], strerror(errno));
                close(fds[k]);
                fds[k] = -1;              /* stop writing to a dead fd (TEE-05) */
                rc = 1;
            }
        }
    }

    for (int k = 0; k < nfiles; k++) {
        if (fds[k] >= 0 && close(fds[k]) != 0) {   /* check close (TEE-06) */
            fprintf(stderr, "%s: %s: %s\n", prog, argv[i + k], strerror(errno));
            rc = 1;
        }
    }
    free(fds);
    return rc;                            /* real status (TEE-07) */
}
