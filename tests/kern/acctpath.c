/*
 * acctpath — acct(2) takes a whole path, and only a regular file.
 *
 * kern_acct looked its argument up as one name in the root directory, so
 * any path below / failed with ENOENT, and it took a device or anything
 * else that was not a directory.  Turns accounting on with a nested and
 * then a relative path, checks an exiting child's record lands in the
 * file, and that a directory and a device are refused with EACCES.
 *
 * Linux build (raw acct); run as root.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static long acct_(const char *p)
{
    return syscall(SYS_acct, p);
}

int main(void)
{
    struct stat st;
    int fd;

    mkdir("/tmp/acctd", 0755);
    fd = open("/tmp/acctd/log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) close(fd);

    check(acct_("/tmp/acctd/log") == 0, "accounting on to /tmp/acctd/log");
    pid_t c = fork();
    if (c == 0) _exit(0);
    waitpid(c, NULL, 0);
    acct_(NULL);
    check(stat("/tmp/acctd/log", &st) == 0 && st.st_size > 0,
          "an exiting child's record is in the file");

    check(chdir("/tmp/acctd") == 0 && acct_("log") == 0,
          "a relative path from the working directory");
    acct_(NULL);
    chdir("/");

    errno = 0;
    check(acct_("/tmp/acctd") == -1 && errno == EACCES, "a directory: EACCES");
    errno = 0;
    check(acct_("/dev/null") == -1 && errno == EACCES, "a device: EACCES");
    acct_(NULL);

    unlink("/tmp/acctd/log");
    rmdir("/tmp/acctd");
    printf("acctpath: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
