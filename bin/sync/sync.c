/*
 * sync - flush file system buffers.
 *
 *   sync [-d|--data] [-f|--file-system] [FILE...]
 *
 * With no operands, calls sync(2) to flush everything.  With operands,
 * each FILE is opened and synced individually:
 *   default     fsync(2)      - data + metadata for that file
 *   -d          fdatasync(2)  - data (and only essential metadata)
 *   -f          fsync(2)      - Substrate has no syncfs(2), so this is a
 *                               best-effort per-file fsync rather than a
 *                               whole-filesystem flush; still honours the
 *                               operand semantics GNU sync exposes.
 *
 * The previous stub ignored every flag and operand and issued a bare
 * global sync().
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
usage(FILE *f)
{
    fputs("Usage: sync [-d|--data] [-f|--file-system] [FILE...]\n", f);
}

int
main(int argc, char **argv)
{
    int data_only = 0;
    int per_fs = 0;
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-' || a[1] == '\0')
            break;                       /* operand, or bare "-" */
        if (strcmp(a, "--") == 0) { i++; break; }
        if (strcmp(a, "-d") == 0 || strcmp(a, "--data") == 0) {
            data_only = 1;
        } else if (strcmp(a, "-f") == 0 || strcmp(a, "--file-system") == 0) {
            per_fs = 1;
        } else if (strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(a, "--version") == 0) {
            printf("sync (Substrate) 1.0\n");
            return 0;
        } else {
            fprintf(stderr, "sync: invalid option '%s'\n", a);
            usage(stderr);
            return 1;
        }
    }

    /* No operands: whole-system flush. */
    if (i >= argc) {
        if (data_only || per_fs)
            fprintf(stderr,
                "sync: -d/-f require FILE operands; doing a global sync\n");
        sync();
        return 0;
    }

    int rc = 0;
    for (; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY | O_NONBLOCK);
        if (fd < 0)                       /* retry for write-only objects */
            fd = open(argv[i], O_WRONLY | O_NONBLOCK);
        if (fd < 0) {
            fprintf(stderr, "sync: %s: %s\n", argv[i], strerror(errno));
            rc = 1;
            continue;
        }

        int r = data_only ? fdatasync(fd) : fsync(fd);
        (void)per_fs;   /* no syncfs(2): -f degrades to a per-file fsync */
        if (r != 0) {
            fprintf(stderr, "sync: %s: %s\n", argv[i], strerror(errno));
            rc = 1;
        }
        close(fd);
    }
    return rc;
}
