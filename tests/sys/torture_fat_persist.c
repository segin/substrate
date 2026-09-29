/*
 * torture_fat_persist.c -- files written on FAT survive umount and mount.
 *
 * Files created on a FAT file system were gone after it was unmounted
 * and mounted again.  Needs a FAT image on an extra disk whose device
 * path is in /fat-persist-dev (the harness knows which name it got).
 *
 *   create    a new file in the root directory, 3 clusters' worth;
 *   subdir    a new directory with a file in it;
 *   grow      an existing file appended to;
 *   remount   umount + mount: every name, size and byte is back;
 *   unlink    a removed file stays removed across the remount.
 *
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#define MNT "/mnt/fat"

static int failures;
static char dev[64];

static void fail(const char *what)
{
    printf("FAIL: %s (errno %d)\n", what, errno);
    failures++;
}

static int run(const char *cmd)
{
    int rc = system(cmd);

    if (rc != 0)
        printf("FAIL: '%s' exited %d\n", cmd, rc);
    return rc;
}

static void fill(char *buf, size_t n, int seed)
{
    for (size_t i = 0; i < n; i++)
        buf[i] = (char)('a' + (i * 7 + (size_t)seed) % 26);
}

static int write_file(const char *path, int flags, const char *buf, size_t n)
{
    int fd = open(path, flags, 0644);

    if (fd < 0 || write(fd, buf, n) != (ssize_t)n) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    return close(fd);
}

/* 0 if path holds exactly the n bytes in want. */
static int same_contents(const char *path, const char *want, size_t n)
{
    static char got[65536];
    struct stat st;
    int fd;
    ssize_t r;

    if (stat(path, &st) != 0 || (size_t)st.st_size != n)
        return -1;
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    r = read(fd, got, sizeof(got));
    close(fd);
    return (r == (ssize_t)n && memcmp(got, want, n) == 0) ? 0 : -1;
}

int main(void)
{
    static char a[12000], b[5000], c1[3000], c2[2000], cc[5000];
    char mnt[128];
    FILE *f = fopen("/fat-persist-dev", "r");

    if (f == NULL || fgets(dev, sizeof(dev), f) == NULL) {
        printf("FAIL: no /fat-persist-dev\nResult: FAIL (1 failure)\n");
        return 1;
    }
    fclose(f);
    dev[strcspn(dev, "\n")] = '\0';
    snprintf(mnt, sizeof(mnt), "mount %s " MNT " fat", dev);

    mkdir("/mnt", 0755);
    mkdir(MNT, 0755);
    if (run(mnt) != 0)
        goto out;

    fill(a, sizeof(a), 1);
    fill(b, sizeof(b), 2);
    fill(c1, sizeof(c1), 3);
    fill(c2, sizeof(c2), 4);
    memcpy(cc, c1, sizeof(c1));
    memcpy(cc + sizeof(c1), c2, sizeof(c2));

    if (write_file(MNT "/NEWFILE.TXT", O_CREAT | O_WRONLY | O_TRUNC, a, sizeof(a)))
        fail("create NEWFILE.TXT");
    if (mkdir(MNT "/newdir", 0755) != 0)
        fail("mkdir newdir");
    if (write_file(MNT "/newdir/inner.dat", O_CREAT | O_WRONLY | O_TRUNC, b, sizeof(b)))
        fail("create newdir/inner.dat");
    if (write_file(MNT "/grow.txt", O_CREAT | O_WRONLY | O_TRUNC, c1, sizeof(c1)))
        fail("create grow.txt");
    if (write_file(MNT "/grow.txt", O_WRONLY | O_APPEND, c2, sizeof(c2)))
        fail("append grow.txt");
    if (write_file(MNT "/gone.txt", O_CREAT | O_WRONLY | O_TRUNC, b, 100))
        fail("create gone.txt");
    if (unlink(MNT "/gone.txt") != 0)
        fail("unlink gone.txt");

    if (same_contents(MNT "/NEWFILE.TXT", a, sizeof(a)))
        fail("NEWFILE.TXT before remount");

    if (run("umount " MNT) != 0 || run(mnt) != 0)
        goto out;

    if (same_contents(MNT "/NEWFILE.TXT", a, sizeof(a)))
        fail("NEWFILE.TXT after remount");
    if (same_contents(MNT "/newdir/inner.dat", b, sizeof(b)))
        fail("newdir/inner.dat after remount");
    if (same_contents(MNT "/grow.txt", cc, sizeof(cc)))
        fail("grow.txt after remount");
    if (access(MNT "/gone.txt", F_OK) == 0)
        fail("gone.txt reappeared after remount");

    run("umount " MNT);
out:
    printf("Result: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
