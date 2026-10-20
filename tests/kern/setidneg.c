/*
 * setidneg — setuid and setgid refuse -1 with EINVAL (as do seteuid and
 * setegid, which Linux programs reach through setresuid instead).
 *
 * (uid_t)-1 means "leave unchanged" to setreuid and setresuid; the
 * single-id calls stored it, so root calling setuid(-1) ended up with ids
 * nothing could name or restore.  Run as root, in a child each time.
 *
 * Linux build (raw 32-bit-id syscalls); runs on the host too.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;

static void try(long nr, const char *name)
{
    pid_t c = fork();
    if (c == 0) {
        errno = 0;
        long r = syscall(nr, -1L);
        _exit(r == -1 && errno == EINVAL ? 0 : 1);
    }
    int st = 0;
    waitpid(c, &st, 0);
    int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    printf("%s %s(-1) is EINVAL\n", ok ? "ok   " : "FAIL ", name);
    if (!ok)
        fails++;
}

int main(void)
{
    try(SYS_setuid32, "setuid");
    try(SYS_setgid32, "setgid");
    printf("setidneg: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
