/*
 * torture_dirent.c — rewinddir() must restart the stream from the first
 * entry, whatever readdir() has already buffered.
 *
 * Creates a directory holding NFILES entries (more than one getdents
 * buffer's worth), then for each of: one entry read, half read, all read,
 * calls rewinddir() and reads the whole directory again.  Every name must
 * come back exactly once.  Midnight Commander reads one entry and rewinds
 * before listing every directory, so a stale buffer showed each entry
 * twice.
 *
 * Portable: builds for the host and for substrate (Makefile.dirent), and
 * runs as init on substrate.  Prints a "Result:" line.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NFILES  200

static char base[64];
static int failures;

/* Read the rest of the stream, checking that every name appears once. */
static void check_all(DIR *d, const char *what) {
    static unsigned char seen[NFILES];
    int dot = 0, dotdot = 0, other = 0, total = 0;
    struct dirent *e;

    memset(seen, 0, sizeof(seen));
    errno = 0;
    while ((e = readdir(d)) != NULL) {
        total++;
        if (strcmp(e->d_name, ".") == 0)
            dot++;
        else if (strcmp(e->d_name, "..") == 0)
            dotdot++;
        else if (strncmp(e->d_name, "f", 1) == 0 &&
                 atoi(e->d_name + 1) >= 0 && atoi(e->d_name + 1) < NFILES)
            seen[atoi(e->d_name + 1)]++;
        else
            other++;
    }
    if (errno) {
        printf("  FAIL %s: readdir errno %d\n", what, errno);
        failures++;
        return;
    }
    int missing = 0, dup = 0;
    for (int i = 0; i < NFILES; i++) {
        if (seen[i] == 0)
            missing++;
        else if (seen[i] > 1)
            dup++;
    }
    if (dot != 1 || dotdot != 1 || other || missing || dup) {
        printf("  FAIL %s: %d entries; '.' x%d, '..' x%d, %d missing, "
               "%d repeated, %d unknown\n", what, total, dot, dotdot,
               missing, dup, other);
        failures++;
    } else {
        printf("  ok   %s: %d entries, each once\n", what, total);
    }
}

static void rewind_after(int nread, const char *what) {
    DIR *d = opendir(base);
    if (!d) {
        printf("  FAIL %s: opendir errno %d\n", what, errno);
        failures++;
        return;
    }
    for (int i = 0; i < nread && readdir(d); i++)
        ;
    rewinddir(d);
    check_all(d, what);
    closedir(d);
}

int main(void) {
    const char *tmp = access("/tmp", W_OK) == 0 ? "/tmp" : "";
    snprintf(base, sizeof(base), "%s/dirent.%d", tmp, (int)getpid());
    if (mkdir(base, 0755) < 0) {
        printf("torture_dirent: mkdir %s errno %d\nResult: FAILED\n", base, errno);
        return 1;
    }
    printf("torture_dirent: %s, %d files\n", base, NFILES);
    for (int i = 0; i < NFILES; i++) {
        char p[128];
        snprintf(p, sizeof(p), "%s/f%d", base, i);
        int fd = open(p, O_CREAT | O_WRONLY, 0644);
        if (fd < 0) {
            printf("  create %s errno %d\n", p, errno);
            failures++;
            break;
        }
        close(fd);
    }

    if (!failures) {
        DIR *d = opendir(base);
        if (d) {
            check_all(d, "plain read");
            closedir(d);
        }
        rewind_after(1, "rewind after 1");
        rewind_after(NFILES / 2, "rewind after half");
        rewind_after(NFILES + 2, "rewind after all");
    }

    for (int i = 0; i < NFILES; i++) {
        char p[128];
        snprintf(p, sizeof(p), "%s/f%d", base, i);
        unlink(p);
    }
    rmdir(base);
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    fflush(stdout);
    if (getpid() == 1)
        for (;;)
            pause();
    return failures ? 1 : 0;
}
