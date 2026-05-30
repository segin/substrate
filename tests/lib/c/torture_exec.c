/*
 * torture_exec - fork + execve stress test for bisecting the exec-pin race.
 *
 * Run as PID 1 (init=/root/torture_exec).  It hammers fork()+execve():
 * each iteration forks a child that re-execs this same binary with the
 * "child" argument, which exits immediately; the parent waits.
 *
 * Headless signal (no tty needed):
 *   - PASS: completes N iterations and calls reboot(RB_AUTOBOOT) -- under
 *     `qemu -no-reboot` the VM exits.  "qemu EXITED" == passed.
 *   - FAIL/HANG: a stuck exec never reboots -- "qemu ALIVE" == hung.
 *
 * Progress is also written, fsync'd, to /torture_exec.progress on the root
 * fs after every iteration so a post-mortem debugfs read shows how far it
 * got before a hang.
 *
 *   build: i386-unknown-substrate-gcc -O2 -o torture_exec torture_exec.c
 */
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/reboot.h>

static void record(int i) {
    int fd = open("/torture_exec.progress", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char buf[32];
        int n = snprintf(buf, sizeof buf, "iter=%d\n", i);
        if (n > 0)
            (void)write(fd, buf, (size_t)n);
        fsync(fd);
        close(fd);
    }
}

int main(int argc, char **argv) {
    extern char **environ;

    if (argc > 1 && strcmp(argv[1], "child") == 0) {
        _exit(0);
    }

    int N = (argc > 1) ? atoi(argv[1]) : 250;
    char *cargv[] = { argv[0], (char *)"child", NULL };

    for (int i = 0; i < N; i++) {
        record(i);
        pid_t p = fork();
        if (p == 0) {
            execve(argv[0], cargv, environ);
            _exit(127);                 /* exec failed */
        } else if (p > 0) {
            int st;
            while (waitpid(p, &st, 0) < 0)
                ;
            if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
                record(-i);             /* child died abnormally: mark it */
                /* keep going; the count + sign tells us where */
            }
        } else {
            record(-100000 - i);        /* fork failed */
            break;
        }
    }

    record(1000000 + N);                /* completed all N */
    reboot(RB_AUTOBOOT);                /* PASS signal: VM resets/exits */
    for (;;)
        pause();
}
