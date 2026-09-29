/*
 * torture_path_errno.c -- path system calls report the POSIX errno.
 *
 *   nametoolong  a path longer than the kernel accepts, or with a component
 *                longer than 255 bytes, fails with ENAMETOOLONG (was
 *                EFAULT: path syscalls turned every copyinstr() failure
 *                into EFAULT); a bad pointer is still EFAULT.
 *
 * Runs as init; prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

static int failures;

static void expect(const char *what, int rc, int want)
{
    int got = rc == -1 ? errno : 0;

    if (rc != -1 || got != want) {
        printf("FAIL: %s: rc=%d errno=%d (%s), want %d (%s)\n", what, rc,
               got, strerror(got), want, strerror(want));
        failures++;
    }
}

static char longname[300];      /* one 299-byte component */
static char longpath[4200];     /* many short components, > 4096 bytes */
static const char *bad = (const char *)1;

static void test_nametoolong(void)
{
    struct stat st;
    char buf[64];

    memset(longname, 'n', sizeof(longname) - 1);
    for (size_t i = 0; i + 20 < sizeof(longpath); i += 20)
        memcpy(longpath + i, "/aaaaaaaaaaaaaaaaaaa", 20);

    expect("unlink(long name)", unlink(longname), ENAMETOOLONG);
    expect("unlink(long path)", unlink(longpath), ENAMETOOLONG);
    expect("open(long path)", open(longpath, O_RDONLY), ENAMETOOLONG);
    expect("stat(long path)", stat(longpath, &st), ENAMETOOLONG);
    expect("lstat(long path)", lstat(longpath, &st), ENAMETOOLONG);
    expect("mkdir(long path)", mkdir(longpath, 0755), ENAMETOOLONG);
    expect("rmdir(long path)", rmdir(longpath), ENAMETOOLONG);
    expect("chdir(long path)", chdir(longpath), ENAMETOOLONG);
    expect("symlink(t, long path)", symlink("t", longpath), ENAMETOOLONG);
    expect("readlink(long path)", (int)readlink(longpath, buf, sizeof(buf)),
           ENAMETOOLONG);
    expect("rename(long, x)", rename(longpath, "/tmp/x"), ENAMETOOLONG);
    expect("unlink(bad pointer)", unlink(bad), EFAULT);
    expect("stat(bad pointer)", stat(bad, &st), EFAULT);
}

int main(void)
{
    mkdir("/tmp", 01777);
    printf("nametoolong\n");
    test_nametoolong();
    printf("Result: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
