/*
 * trapsig — a hardware fault whose signal is blocked or ignored must still
 * kill the process.
 *
 * A trap is synchronous: returning to userspace re-executes the faulting
 * instruction.  If the kernel gates delivery on the signal mask and simply
 * leaves a blocked SIGSEGV pending, the fault repeats forever and the
 * process spins unkillable.  That is exactly what an X client did when it
 * faulted inside a handler installed with sigfillset(&sa.sa_mask): the
 * console filled with identical traps (same eip, same esp) at full speed.
 *
 * Run each case in a child so the parent can report how it actually died.
 * Expected in both: WIFSIGNALED, WTERMSIG == SIGSEGV.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

static int check(const char *name, pid_t pid) {
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) {
        printf("FAIL %s: waitpid failed\n", name);
        return 1;
    }
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV) {
        printf("ok   %s: died with SIGSEGV\n", name);
        return 0;
    }
    printf("FAIL %s: expected SIGSEGV, got status 0x%x\n", name, st);
    return 1;
}

static void fault(void) {
    volatile int *p = (volatile int *)0;
    *p = 1;
    _exit(99);           /* not reached */
}

int main(void) {
    int bad = 0;
    pid_t pid;

    /* T1: SIGSEGV blocked via sigprocmask, then fault. */
    if ((pid = fork()) == 0) {
        sigset_t s;
        sigemptyset(&s);
        sigaddset(&s, SIGSEGV);
        sigprocmask(SIG_BLOCK, &s, NULL);
        fault();
        _exit(99);
    }
    bad += check("T1 fault with SIGSEGV blocked", pid);

    /* T2: SIGSEGV ignored, then fault. */
    if ((pid = fork()) == 0) {
        signal(SIGSEGV, SIG_IGN);
        fault();
        _exit(99);
    }
    bad += check("T2 fault with SIGSEGV ignored", pid);

    /* T3: the real-world shape -- fault *inside* a handler whose sa_mask is
     * sigfillset(), so SIGSEGV is masked for the duration of the handler. */
    if ((pid = fork()) == 0) {
        struct sigaction sa;
        sa.sa_handler = (void (*)(int))fault;
        sigfillset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGALRM, &sa, NULL);
        raise(SIGALRM);
        _exit(99);
    }
    bad += check("T3 fault inside sigfillset handler", pid);

    printf(bad ? "trapsig: FAILED\n" : "trapsig: ALL PASS\n");
    return bad ? 1 : 0;
}
