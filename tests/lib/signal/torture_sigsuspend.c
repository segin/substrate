/*
 * torture_sigsuspend.c — sigsuspend() and pause() return EINTR once a
 * handler has run, even when that handler was installed with SA_RESTART.
 *
 * POSIX never restarts sigsuspend(): its whole job is to report that a
 * signal was caught.  Restarting it after an SA_RESTART handler puts the
 * caller straight back to sleep, so a "block, test flag, sigsuspend" loop
 * never sees the flag its handler just set.  Midnight Commander waits for
 * its subshell exactly that way (SIGCHLD with SA_RESTART, waiting for the
 * subshell's SIGSTOP), and hung on start-up and on every directory change.
 *
 *   sigsuspend  an SA_RESTART SIGUSR1 handler wakes sigsuspend()
 *   pause       the same through pause()
 *   stop-cont   mc's handshake, ROUNDS times: the child answers on a pipe
 *               and stops itself; the parent's SIGCHLD handler (SA_RESTART)
 *               sees the stop via waitpid(WUNTRACED) while the parent waits
 *               in sigsuspend(), then the parent sends SIGCONT.
 *
 * A watchdog SIGALRM (installed without SA_RESTART) turns a hang into a
 * failure.  Builds for the host and for substrate (see Makefile); runs as
 * init on substrate.  Prints a "Result:" line.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROUNDS  500

static volatile sig_atomic_t got_usr1, got_alrm;
static volatile sig_atomic_t stopped, alive;
static volatile pid_t child;
static int failures;

static void on_usr1(int sig) { (void)sig; got_usr1 = 1; }
static void on_alrm(int sig) { (void)sig; got_alrm = 1; }

static void on_chld(int sig) {
    int st;
    (void)sig;
    if (waitpid(child, &st, WUNTRACED | WNOHANG) != child)
        return;
    if (WIFSTOPPED(st))
        stopped = 1;
    else
        alive = 0;
}

static void install(int sig, void (*fn)(int), int flags) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fn;
    sa.sa_flags = flags;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

/* Fork a child that sends SIGUSR1 to us after a short delay. */
static pid_t usr1_later(void) {
    pid_t parent = getpid();
    pid_t p = fork();
    if (p == 0) {
        usleep(200000);
        kill(parent, SIGUSR1);
        _exit(0);
    }
    return p;
}

static void case_wait(const char *name, int use_pause) {
    sigset_t block, old;
    got_usr1 = got_alrm = 0;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block, &old);
    pid_t p = usr1_later();
    alarm(3);
    int r, e;
    if (use_pause) {
        sigprocmask(SIG_SETMASK, &old, NULL);
        r = pause();
    } else {
        sigset_t wait_mask = old;
        sigdelset(&wait_mask, SIGUSR1);
        r = sigsuspend(&wait_mask);
    }
    e = errno;
    alarm(0);
    sigprocmask(SIG_SETMASK, &old, NULL);
    waitpid(p, NULL, 0);
    if (r != -1 || e != EINTR || !got_usr1 || got_alrm) {
        printf("  FAIL %s: returned %d errno %d; SIGUSR1 %s; %s\n", name, r, e,
               got_usr1 ? "caught" : "not caught",
               got_alrm ? "woken only by the watchdog" : "no watchdog");
        failures++;
    } else {
        printf("  ok   %s\n", name);
    }
}

static void case_stop_cont(void) {
    int cmd[2], ans[2];
    if (pipe(cmd) < 0 || pipe(ans) < 0) {
        printf("  FAIL stop-cont: pipe errno %d\n", errno);
        failures++;
        return;
    }
    install(SIGCHLD, on_chld, SA_RESTART);
    stopped = 0;
    alive = 1;
    got_alrm = 0;
    child = fork();
    if (child == 0) {
        char c;
        while (read(cmd[0], &c, 1) == 1) {
            if (write(ans[1], "/\n", 2) != 2)
                break;
            kill(getpid(), SIGSTOP);
        }
        _exit(0);
    }

    int round;
    for (round = 0; round < ROUNDS && !got_alrm && alive; round++) {
        char b[8];
        alarm(3);
        if (write(cmd[1], "x", 1) != 1)
            break;
        while (read(ans[0], b, sizeof(b)) < 0 && errno == EINTR && !got_alrm)
            ;
        sigset_t block, old;
        sigemptyset(&block);
        sigaddset(&block, SIGCHLD);
        sigprocmask(SIG_BLOCK, &block, &old);
        sigdelset(&old, SIGCHLD);
        while (alive && !stopped && !got_alrm)
            sigsuspend(&old);
        stopped = 0;
        kill(child, SIGCONT);
        sigprocmask(SIG_SETMASK, &old, NULL);
    }
    alarm(0);
    kill(child, SIGKILL);
    install(SIGCHLD, SIG_DFL, 0);
    waitpid(child, NULL, 0);
    close(cmd[0]); close(cmd[1]); close(ans[0]); close(ans[1]);
    if (round != ROUNDS || got_alrm) {
        printf("  FAIL stop-cont: hung in round %d of %d\n", round, ROUNDS);
        failures++;
    } else {
        printf("  ok   stop-cont: %d rounds\n", ROUNDS);
    }
}

int main(void) {
    printf("torture_sigsuspend: SA_RESTART handlers vs sigsuspend/pause\n");
    fflush(stdout);
    install(SIGUSR1, on_usr1, SA_RESTART);
    install(SIGALRM, on_alrm, 0);

    case_wait("sigsuspend", 0);
    case_wait("pause", 1);
    case_stop_cont();

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    fflush(stdout);
    if (getpid() == 1)
        for (;;)
            pause();
    return failures ? 1 : 0;
}
