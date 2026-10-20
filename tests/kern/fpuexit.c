/*
 * fpuexit — FPU state stays each process's own while others that hold it
 * die.
 *
 * A process torn down while it owns the CPU's FPU registers drops that
 * ownership (fpu_forget_process).  Workers here keep values in x87 and
 * SSE registers across a busy loop and check them; meanwhile children
 * that are using the FPU are killed or exit.  Every worker must see its
 * own numbers throughout.
 *
 * Linux build; runs on the host too.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static int work(double seed)
{
    volatile double a = seed, b = seed * 2.0;
    for (long i = 0; i < 3000000; i++) {
        a = a * 1.0000001 + 0.5;
        b = b * 0.9999999 + 0.25;
    }
    /* Recompute: the same arithmetic must give the same answer. */
    volatile double c = seed, d = seed * 2.0;
    for (long i = 0; i < 3000000; i++) {
        c = c * 1.0000001 + 0.5;
        d = d * 0.9999999 + 0.25;
    }
    return a == c && b == d;
}

int main(void)
{
    int fails = 0;
    pid_t workers[4];

    for (int w = 0; w < 4; w++) {
        workers[w] = fork();
        if (workers[w] == 0)
            _exit(work(1.0 + w) ? 0 : 1);
    }
    for (int round = 0; round < 30; round++) {
        pid_t c = fork();
        if (c == 0) {
            volatile double x = round;
            for (;;) x = x * 1.5 + 1.0;
        }
        usleep(20000);
        kill(c, SIGKILL);
        waitpid(c, NULL, 0);
        pid_t e = fork();
        if (e == 0) {
            volatile double y = round;
            for (int i = 0; i < 100000; i++) y = y / 3.0 + 7.0;
            _exit(0);
        }
        waitpid(e, NULL, 0);
    }
    for (int w = 0; w < 4; w++) {
        int st = 0;
        waitpid(workers[w], &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
            fails++;
    }
    printf("fpuexit: %d of 4 workers saw their FPU values change: %s\n", fails,
           fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
