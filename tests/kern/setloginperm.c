/*
 * setloginperm — only root may set the session's login name.
 *
 * sys_setlogin stored any name for any caller, so an ordinary user could
 * rename the session getlogin(2) reports to every process in it.  As root,
 * sets "root"; a child that drops to uid 1000 must get EPERM, and the name
 * must still read back as "root".
 *
 * FreeBSD build (setlogin/getlogin); run as root.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    int fails = 0;

    if (setlogin("root") != 0) {
        printf("FAIL  root could not set the login name (errno %d)\n", errno);
        fails++;
    }
    pid_t c = fork();
    if (c == 0) {
        if (setuid(1000) != 0) _exit(2);
        errno = 0;
        int r = setlogin("mallory");
        _exit(r == -1 && errno == EPERM ? 0 : 1);
    }
    int st = 0;
    waitpid(c, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("FAIL  uid 1000 setlogin was not EPERM (status %d)\n", st);
        fails++;
    } else {
        printf("ok    uid 1000 setlogin is EPERM\n");
    }
    const char *n = getlogin();
    if (!n || strcmp(n, "root") != 0) {
        printf("FAIL  login name is now \"%s\"\n", n ? n : "(null)");
        fails++;
    } else {
        printf("ok    login name is still root\n");
    }
    printf("setloginperm: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
