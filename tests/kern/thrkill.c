/*
 * thrkill — thr_kill2 follows kill(2)'s permission rule.
 *
 * thr_kill2(pid, tid, sig) posted the signal to the thread directly,
 * without the permission check kill(2) makes and past psignal's
 * protection of pid 1, so any process could SIGKILL root's processes or
 * init.
 *
 * Run as root: a child that drops to uid 1000 tries thr_kill2 on init
 * and on a root process (both EPERM), and on a process of its own uid
 * (allowed).  Substrate only (thr_kill2 is the native thread call).
 */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/thr.h>
#include <sys/wait.h>

static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

/* 0, or the -errno thr_kill2 failed with (tid -1: every thread). */
static int thr_kill2_all(pid_t pid, int sig) {
    return (int)syscall(SYS_THR_KILL2, (long)pid, -1L, (long)sig);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    check(getuid() == 0, "running as root");

    pid_t root_victim = fork();
    if (root_victim == 0) {
        for (;;) pause();
    }

    pid_t child = fork();
    if (child == 0) {
        if (setuid(1000) != 0) _exit(10);
        pid_t peer = fork();                /* uid 1000 as well */
        if (peer == 0) {
            for (;;) pause();
        }
        int r = 0;
        if (thr_kill2_all(1, SIGKILL) != -EPERM) r |= 1;
        if (thr_kill2_all(root_victim, SIGKILL) != -EPERM) r |= 2;
        if (thr_kill2_all(root_victim, 0) != -EPERM) r |= 4;
        if (thr_kill2_all(peer, SIGKILL) != 0) r |= 8;
        int st = 0;
        waitpid(peer, &st, 0);
        if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGKILL) r |= 16;
        _exit(r);
    }

    int st = 0;
    waitpid(child, &st, 0);
    int r = WIFEXITED(st) ? WEXITSTATUS(st) : 255;
    check(r != 10, "child dropped to uid 1000");
    check(!(r & 1), "uid 1000 -> init: EPERM");
    check(!(r & 2), "uid 1000 -> root process: EPERM");
    check(!(r & 4), "uid 1000 -> root process, signal 0: EPERM");
    check(!(r & 8) && !(r & 16), "uid 1000 -> uid 1000: delivered");

    check(kill(root_victim, 0) == 0, "root process still alive");
    kill(root_victim, SIGKILL);
    waitpid(root_victim, NULL, 0);

    printf("%s\n", failed ? "thrkill: FAIL" : "thrkill: PASS");
    return failed;
}
