/*
 * revoketest — revoke(2) as the BSD personalities see it.
 *
 * sys_revoke walked even a relative path from the root, checked no
 * credentials, and passed any file's private pointer to tty_revoke as a
 * terminal.  Now: a regular file is EINVAL, a relative name is the current
 * directory's, and someone neither root nor the owner gets EPERM.
 *
 * Built on a FreeBSD (or NetBSD) host and run as root under its
 * personality; on a real BSD it passes too.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

int main(void)
{
    const char *f = "/tmp/revoketest.f";
    int fd = open(f, O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) close(fd);

    errno = 0;
    check(revoke(f) == -1 && errno == EINVAL, "a regular file is EINVAL");

    /* Found from the cwd, the file is EINVAL; looked for from the root,
     * it would be ENOENT. */
    check(chdir("/tmp") == 0, "chdir /tmp");
    errno = 0;
    check(revoke("revoketest.f") == -1 && errno == EINVAL,
          "a relative name is found from the cwd");
    unlink(f);
    chdir("/");

    pid_t c = fork();
    if (c == 0) {
        if (setuid(1000) != 0) _exit(2);
        errno = 0;
        _exit(revoke("/dev/null") == -1 && errno == EPERM ? 0 : 1);
    }
    int st = 0;
    waitpid(c, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
          "a user who does not own the device gets EPERM");

    printf("revoketest: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
