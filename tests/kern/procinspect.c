/*
 * procinspect — the sys_proc_* calls show another user's process, or one
 * that changed credentials, only to root.
 *
 * sys_proc_threads, sys_proc_thr_list, sys_proc_cwd and sys_proc_exe had
 * no permission check at all, and the shared check the others used looked
 * only at the target's real uid, so a set-id process stayed open to the
 * user who ran it.  Run as root: one child becomes uid 1001, another
 * changes its ids to 1000 (which marks it set-id); a third, as uid 1000,
 * must be refused on both and still see itself.  Substrate only.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static pid_t sleeper(uid_t uid)
{
    pid_t p = fork();
    if (p == 0) {
        if (setuid(uid) != 0) _exit(1);
        for (;;) pause();
    }
    return p;
}

/* Each call refused with EPERM? */
static int refused(pid_t pid)
{
    char buf[256];
    tid_t tids[16];
    size_t n = 16;
    sys_thrinfo_t ti[16];
    int bad = 0;

    bad += sys_proc_cwd(pid, buf, sizeof(buf)) != -EPERM;
    bad += sys_proc_exe(pid, buf, sizeof(buf)) != -EPERM;
    bad += sys_proc_threads(pid, tids, &n) != -EPERM;
    bad += sys_proc_thr_list(pid, ti, sizeof(ti[0])) != -EPERM;
    return bad == 0;
}

int main(void)
{
    pid_t other = sleeper(1001);
    pid_t sugid = sleeper(1000);
    usleep(200000);

    pid_t c = fork();
    if (c == 0) {
        char buf[256];
        if (setuid(1000) != 0) _exit(2);
        int r = 0;
        if (!refused(other)) r |= 4;
        if (!refused(sugid)) r |= 8;
        if (sys_proc_cwd(getpid(), buf, sizeof(buf)) != 0) r |= 16;
        _exit(r);
    }
    int st = 0;
    waitpid(c, &st, 0);
    int r = WIFEXITED(st) ? WEXITSTATUS(st) : 255;
    check(r != 2 && r != 255, "child became uid 1000");
    check(!(r & 4), "another user's process is refused");
    check(!(r & 8), "a set-id process of the same user is refused");
    check(!(r & 16), "a process still sees itself");

    kill(other, SIGKILL);
    kill(sugid, SIGKILL);
    waitpid(other, NULL, 0);
    waitpid(sugid, NULL, 0);
    printf("procinspect: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
