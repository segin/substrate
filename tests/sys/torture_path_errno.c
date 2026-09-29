/*
 * torture_path_errno.c -- path system calls report the POSIX errno.
 *
 *   nametoolong  a path longer than the kernel accepts, or with a component
 *                longer than 255 bytes, fails with ENAMETOOLONG (was
 *                EFAULT: path syscalls turned every copyinstr() failure
 *                into EFAULT); a bad pointer is still EFAULT.
 *   eloop        a symbolic-link loop in the directory part of a path is
 *                ELOOP (was ENOENT for unlink, symlink, ...);
 *   notdir       a regular file in the directory part of a path is ENOTDIR
 *                (was ENOSYS for unlink when it was the immediate parent,
 *                ENOENT deeper down).
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

static void test_eloop(void)
{
    struct stat st;
    int fd;

    unlink("/tmp/pe_loop1");
    unlink("/tmp/pe_loop2");
    symlink("pe_loop2", "/tmp/pe_loop1");
    symlink("pe_loop1", "/tmp/pe_loop2");

    expect("unlink(loop/x)", unlink("/tmp/pe_loop1/x"), ELOOP);
    expect("unlinkat(loop/x)", unlinkat(AT_FDCWD, "/tmp/pe_loop1/x", 0), ELOOP);
    expect("symlink(t, loop/l)", symlink("t", "/tmp/pe_loop1/l"), ELOOP);
    expect("mkdir(loop/d)", mkdir("/tmp/pe_loop1/d", 0755), ELOOP);
    expect("rmdir(loop/d)", rmdir("/tmp/pe_loop1/d"), ELOOP);
    expect("rename(loop/a, x)", rename("/tmp/pe_loop1/a", "/tmp/pe_x"), ELOOP);
    expect("stat(loop/x)", stat("/tmp/pe_loop1/x", &st), ELOOP);
    fd = open("/tmp/pe_loop1/x", O_RDONLY);
    expect("open(loop/x)", fd, ELOOP);
    fd = open("/tmp/pe_loop1/x", O_CREAT | O_WRONLY, 0644);
    expect("open(loop/x, O_CREAT)", fd, ELOOP);
    expect("chmod(loop/x)", chmod("/tmp/pe_loop1/x", 0644), ELOOP);
}

static void test_notdir(void)
{
    struct stat st;
    int fd;

    fd = open("/tmp/pe_file", O_CREAT | O_WRONLY, 0644);
    close(fd);

    expect("unlink(file/x)", unlink("/tmp/pe_file/x"), ENOTDIR);
    expect("unlink(file/a/b)", unlink("/tmp/pe_file/a/b"), ENOTDIR);
    expect("unlinkat(file/x)", unlinkat(AT_FDCWD, "/tmp/pe_file/x", 0), ENOTDIR);
    expect("symlink(t, file/l)", symlink("t", "/tmp/pe_file/l"), ENOTDIR);
    expect("mkdir(file/d)", mkdir("/tmp/pe_file/d", 0755), ENOTDIR);
    expect("rmdir(file/d)", rmdir("/tmp/pe_file/d"), ENOTDIR);
    expect("rename(file/a, x)", rename("/tmp/pe_file/a", "/tmp/pe_x"), ENOTDIR);
    expect("stat(file/x)", stat("/tmp/pe_file/x", &st), ENOTDIR);
    fd = open("/tmp/pe_file/x", O_RDONLY);
    expect("open(file/x)", fd, ENOTDIR);
    fd = open("/tmp/pe_file/x", O_CREAT | O_WRONLY, 0644);
    expect("open(file/x, O_CREAT)", fd, ENOTDIR);
    unlink("/tmp/pe_file");
}

int main(void)
{
    mkdir("/tmp", 01777);
    printf("nametoolong\n");
    test_nametoolong();
    printf("eloop\n");
    test_eloop();
    printf("notdir\n");
    test_notdir();
    printf("Result: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
