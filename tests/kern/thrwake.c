/*
 * thrwake — thr_wake reaches only the caller's own threads.
 *
 * sys_thr_wake looked any tid up system-wide, so a process could set the
 * wake bit on -- and wake out of its sleep -- a thread of another process
 * or of the kernel.  A forked child's thread id is not one of ours: waking
 * it must fail (ESRCH), and waking our own must still work.
 *
 * Substrate only (raw SYS_THR_WAKE / SYS_THR_SELF).
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    int fails = 0;
    int pfd[2];

    if (pipe(pfd) != 0) return 2;
    pid_t c = fork();
    if (c == 0) {
        long tid = syscall(SYS_THR_SELF);
        write(pfd[1], &tid, sizeof(tid));
        pause();
        _exit(0);
    }
    long ctid = 0;
    read(pfd[0], &ctid, sizeof(ctid));

    long r = syscall(SYS_THR_WAKE, ctid);
    if (r == 0) {
        printf("FAIL  woke thread %ld of another process\n", ctid);
        fails++;
    } else {
        printf("ok    another process's thread is refused (%ld)\n", r);
    }
    r = syscall(SYS_THR_WAKE, 0L);
    if (r != 0) {
        printf("FAIL  could not wake itself (%ld)\n", r);
        fails++;
    } else {
        printf("ok    a thread of our own wakes\n");
    }
    kill(c, SIGKILL);
    waitpid(c, NULL, 0);
    printf("thrwake: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
