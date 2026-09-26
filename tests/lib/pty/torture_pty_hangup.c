/*
 * torture_pty_hangup.c — closing a pty master hangs up the slave's session.
 *
 * POSIX 11.1.10: when a terminal is disconnected, SIGHUP goes to its
 * controlling process (the session leader), with SIGCONT to any stopped
 * member.  Closing a pty master only marked the slave hung up: a shell left
 * stopped on the slave -- Midnight Commander's zsh subshell after mc quit
 * -- stayed stopped forever, reparented to init.
 *
 *   stopped-leader   the session leader stops itself; closing the master
 *                    kills it with SIGHUP.
 *   job-in-front     the session leader has put another process group in
 *                    the foreground and waits; closing the master kills
 *                    the leader with SIGHUP too, not just the job.
 *
 * Portable: builds for the host (Linux passes) and substrate (Makefile);
 * runs as init on substrate.  Prints a "Result:" line.
 */
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static int failures;

/* Wait up to `secs` for pid to terminate; its status, or -1. */
static int wait_dead(pid_t pid, int secs) {
    for (int i = 0; i < secs * 10; i++) {
        int st;
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid)
            return st;
        usleep(100000);
    }
    return -1;
}

/* Fork a session leader on a fresh pty; `job` puts a second process
 * group in the foreground.  Returns the master fd; *leader is the child. */
static int spawn(int job, pid_t *leader) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) < 0 || unlockpt(m) < 0)
        return -1;
    const char *sname = ptsname(m);
    int sync[2];
    if (pipe(sync) < 0)
        return -1;
    pid_t pid = fork();
    if (pid == 0) {
        close(m);
        close(sync[0]);
        setsid();
        int s = open(sname, O_RDWR);
        if (s < 0) _exit(10);
        ioctl(s, TIOCSCTTY, 0);
        if (job) {
            pid_t j = fork();
            if (j == 0) {
                setpgid(0, 0);
                for (;;) pause();           /* dies of the SIGHUP */
            }
            setpgid(j, j);
            signal(SIGTTOU, SIG_IGN);
            tcsetpgrp(s, j);
        }
        if (write(sync[1], "r", 1) != 1) _exit(11);
        if (job)
            for (;;) pause();
        raise(SIGSTOP);                     /* a shell left stopped */
        for (;;) pause();
    }
    close(sync[1]);
    char c;
    if (read(sync[0], &c, 1) != 1) {
        close(sync[0]);
        return -1;
    }
    close(sync[0]);
    *leader = pid;
    return m;
}

static void run_case(const char *name, int job) {
    pid_t leader;
    int m = spawn(job, &leader);
    if (m < 0) {
        printf("  FAIL %s: pty setup errno %d\n", name, errno);
        failures++;
        return;
    }
    usleep(300000);                         /* let it stop / settle */
    close(m);
    int st = wait_dead(leader, 5);
    if (st == -1) {
        printf("  FAIL %s: the session leader survived the hangup\n", name);
        kill(leader, SIGKILL);
        waitpid(leader, NULL, 0);
        failures++;
    } else if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGHUP) {
        printf("  FAIL %s: leader ended with status 0x%x, want SIGHUP\n",
               name, st);
        failures++;
    } else {
        printf("  ok   %s\n", name);
    }
}

int main(void) {
    printf("torture_pty_hangup: pty master close vs the slave's session\n");
    fflush(stdout);
    run_case("stopped-leader", 0);
    run_case("job-in-front", 1);
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    fflush(stdout);
    if (getpid() == 1)
        for (;;)
            pause();
    return failures ? 1 : 0;
}
