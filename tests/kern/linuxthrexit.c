/*
 * linuxthrexit — Linux threads end one at a time.
 *
 * Built with the host's glibc (gcc -m32 -static -pthread), so it runs under
 * the Linux personality.  glibc ends every thread with exit(2) and the
 * whole process with exit_group(2).  Both ended the process, so a threaded
 * program died with its first thread; and the CLONE_CHILD_CLEARTID word
 * was set to 1, not cleared, so pthread_join would have waited forever.
 *
 *   A. four threads return values; main joins each and gets them back;
 *   B. a thread ends with pthread_exit while main is still working;
 *   C. main ends with exit_group's status (exit(3) -> status 3);
 *   D. main calls pthread_exit and the process lives until its last
 *      thread ends, then exits 0.
 *
 * Runs on the Linux host too, where it must also pass.
 */
#include <pthread.h>
#include <stdint.h>
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

static void *ret_value(void *arg)
{
    return (void *)((uintptr_t)arg * 10);
}

static volatile int b_done;

static void *exit_early(void *arg)
{
    (void)arg;
    b_done = 1;
    pthread_exit((void *)7);
    return NULL;
}

static void *outlive_main(void *arg)
{
    (void)arg;
    usleep(200 * 1000);
    /* Marker the parent looks for: printed after main has gone. */
    printf("ok    D: a thread ran on after main's pthread_exit\n");
    fflush(stdout);
    return NULL;
}

int main(void)
{
    pthread_t t[4], tb;
    void *rv;
    int i, all = 1, status = 0;
    pid_t pid;

    setvbuf(stdout, NULL, _IONBF, 0);

    /* A */
    for (i = 0; i < 4; i++)
        if (pthread_create(&t[i], NULL, ret_value, (void *)(uintptr_t)(i + 1)))
            all = 0;
    for (i = 0; i < 4; i++) {
        if (pthread_join(t[i], &rv) != 0 || (uintptr_t)rv != (uintptr_t)(i + 1) * 10)
            all = 0;
    }
    check(all, "A: four threads joined with their return values");

    /* B */
    if (pthread_create(&tb, NULL, exit_early, NULL) == 0 &&
        pthread_join(tb, &rv) == 0)
        check(b_done && (uintptr_t)rv == 7,
              "B: pthread_exit ended only its thread; main carried on");
    else
        check(0, "B: create/join");

    /* C: a child process, threaded, exits with a status. */
    pid = fork();
    if (pid == 0) {
        pthread_t tc;
        pthread_create(&tc, NULL, ret_value, (void *)1);
        pthread_join(tc, NULL);
        exit(3);
    }
    waitpid(pid, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 3,
          "C: exit(3) from a threaded process gives status 3");

    /* D: the main thread leaves first. */
    pid = fork();
    if (pid == 0) {
        pthread_t td;
        pthread_create(&td, NULL, outlive_main, NULL);
        pthread_exit(NULL);
    }
    waitpid(pid, &status, 0);
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "D: the process exits 0 when its last thread ends");

    printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails ? 1 : 0;
}
