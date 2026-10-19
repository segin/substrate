/*
 * sigretsel — what a signal handler leaves in its context must be safe to
 * return to.
 *
 * sigreturn put the saved selectors back with only RPL 3 forced, so a
 * handler that stored a selector past the end of the GDT made the kernel
 * fault on its own way back to user mode (a panic).  It also restored the
 * saved signal mask as is, so a context with SIGKILL and SIGSTOP in it made
 * the process immune to both; and under the Linux personality the mask was
 * left in native numbering, so SIGURG and the others numbered differently
 * came back as the wrong signals.
 *
 *   A. a handler that plants an invalid DS: the kernel stays up.  Substrate
 *      kills the process with SIGSEGV; Linux loads a null selector instead
 *      and lets it run on.  Either is accepted;
 *   B. a handler that adds SIGKILL, SIGSTOP and SIGURG to uc_sigmask:
 *      afterwards SIGURG is blocked and SIGKILL and SIGSTOP are not.
 *
 * Built as a static i386 Linux program (make sigretsel.linux); runs on the
 * host too, where Linux itself must give the same answers.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static void bad_ds(int sig, siginfo_t *si, void *ctx)
{
    (void)sig;
    (void)si;
    ucontext_t *uc = ctx;
    uc->uc_mcontext.gregs[REG_DS] = 0xFFF3;  /* GDT index 0x1ffe, RPL 3 */
}

static void add_mask(int sig, siginfo_t *si, void *ctx)
{
    (void)sig;
    (void)si;
    ucontext_t *uc = ctx;
    sigaddset(&uc->uc_sigmask, SIGKILL);
    sigaddset(&uc->uc_sigmask, SIGSTOP);
    sigaddset(&uc->uc_sigmask, SIGURG);
}

static void install(void (*fn)(int, siginfo_t *, void *))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fn;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &sa, NULL);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    pid_t pid = fork();
    if (pid == 0) {
        install(bad_ds);
        raise(SIGUSR1);
        _exit(0);                   /* the bad frame must not get here */
    }
    int st = 0;
    waitpid(pid, &st, 0);
    printf("      child %s\n",
           WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV ? "died of SIGSEGV" :
           WIFEXITED(st) && WEXITSTATUS(st) == 0 ? "ran on (null DS)" :
           "ended some other way");
    check((WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV) ||
          (WIFEXITED(st) && WEXITSTATUS(st) == 0),
          "handler planting an invalid DS: the process is stopped or runs on");

    pid = fork();
    if (pid == 0) {
        sigset_t cur;
        install(add_mask);
        raise(SIGUSR1);
        sigprocmask(SIG_BLOCK, NULL, &cur);
        int bad = 0;
        bad |= !sigismember(&cur, SIGURG) << 0;
        bad |= sigismember(&cur, SIGKILL) << 1;
        bad |= sigismember(&cur, SIGSTOP) << 2;
        _exit(bad);
    }
    st = 0;
    waitpid(pid, &st, 0);
    int bad = WIFEXITED(st) ? WEXITSTATUS(st) : 0xff;
    printf("      mask result bits %#x\n", bad);
    check(!(bad & 1), "SIGURG added in the handler is blocked afterwards");
    check(!(bad & 6), "SIGKILL and SIGSTOP added in the handler are not");

    printf("sigretsel: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
