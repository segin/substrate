/*
 * torture_signal.c — verify that signals interrupt blocked syscalls.
 *
 * Reproducer for the original symptom: a process parked in read() on
 * a TTY (or pipe, or FIFO, or anywhere else with a kernel sleep) must
 * be killable with SIGKILL and interruptible by SIGINT — i.e., the
 * sleep must be cancellable via signal_interrupt_thread on the
 * sleeping thread, which requires THREAD_F_INTERRUPTIBLE to be set
 * before psignal scans.
 *
 * Each test forks a child, parks it in a blocking read, then sends a
 * signal from the parent and verifies the child terminated/woke
 * correctly via waitpid().
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MUST(cond, msg) do {                                          \
    if (!(cond)) {                                                    \
        fprintf(stderr, "  FAIL: %s (errno=%d: %s)\n",                \
                (msg), errno, strerror(errno));                       \
        return -1;                                                    \
    }                                                                 \
} while (0)

#define SKIP(reason) do {                                             \
    fprintf(stdout, "SKIP (%s) ", (reason));                          \
    return 1;                                                         \
} while (0)

#define TEST(name) static int test_##name(void)
#define RUN(name) do {                                                \
    fprintf(stdout, "[%2d/%2d] %-32s ", ++tests_run, TOTAL, #name);   \
    fflush(stdout);                                                   \
    int rc = test_##name();                                           \
    if (rc == 0)      { fprintf(stdout, "PASS\n"); tests_pass++; }    \
    else if (rc == 1) { fprintf(stdout, "\n");      tests_skip++; }   \
    else              { fprintf(stdout, "  -> FAILED\n"); tests_fail++; } \
} while (0)

static const int TOTAL = 4;

/* Short sleep helper.  100 ms is enough for the child to enter the
 * blocking syscall before the parent signals. */
static void short_sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ */

TEST(sigkill_terminates_pipe_read)
{
    int fds[2];
    MUST(pipe(fds) == 0, "pipe");

    pid_t pid = fork();
    MUST(pid >= 0, "fork");
    if (pid == 0) {
        /* Child: block forever in read.  The parent's SIGKILL should
         * tear us out.  If anything else returns from read first, we
         * exit with a sentinel that tells the parent we didn't get
         * killed the way we expected. */
        close(fds[1]);
        char buf;
        ssize_t n = read(fds[0], &buf, 1);
        if (n == 0)  _exit(20);   /* unexpected EOF */
        if (n > 0)   _exit(21);   /* unexpected data */
        _exit(22);                /* unexpected -1 */
    }
    close(fds[0]);
    short_sleep_ms(100);
    MUST(kill(pid, SIGKILL) == 0, "kill SIGKILL");

    int status = 0;
    pid_t r = waitpid(pid, &status, 0);
    close(fds[1]);
    MUST(r == pid, "waitpid returned wrong pid");
    MUST(WIFSIGNALED(status), "child not signalled");
    MUST(WTERMSIG(status) == SIGKILL, "child not killed by SIGKILL");
    return 0;
}

TEST(sigint_handler_returns_eintr)
{
    int fds[2];
    MUST(pipe(fds) == 0, "pipe");

    pid_t pid = fork();
    MUST(pid >= 0, "fork");
    if (pid == 0) {
        /* Child: install a SIGINT handler so read can return -EINTR.
         * Verify the handler ran AND read got EINTR. */
        static volatile int caught = 0;
        void on_sigint(int s) { (void)s; caught = 1; }
        struct sigaction sa = {0};
        sa.sa_handler = on_sigint;
        sigaction(SIGINT, &sa, NULL);

        close(fds[1]);
        char buf;
        errno = 0;
        ssize_t n = read(fds[0], &buf, 1);
        if (n != -1)  _exit(30);
        if (errno != EINTR) _exit(31);
        if (!caught) _exit(32);
        _exit(0);
    }
    close(fds[0]);
    short_sleep_ms(100);
    MUST(kill(pid, SIGINT) == 0, "kill SIGINT");

    int status = 0;
    pid_t r = waitpid(pid, &status, 0);
    close(fds[1]);
    MUST(r == pid, "waitpid returned wrong pid");
    MUST(WIFEXITED(status), "child not exited normally");
    /* The child encodes a failure mode in the exit code (30/31/32).
     * Anything non-zero means the test detected a kernel-side flaw. */
    MUST(WEXITSTATUS(status) == 0, "child exit code != 0 (signal-EINTR path broken)");
    return 0;
}

TEST(sigkill_terminates_fifo_read)
{
    char path[64];
    snprintf(path, sizeof(path), "/tmp/torture_signal.%d.fifo", (int)getpid());
    unlink(path);
    MUST(mkfifo(path, 0644) == 0, "mkfifo");

    pid_t pid = fork();
    MUST(pid >= 0, "fork");
    if (pid == 0) {
        /* Child: open the FIFO O_RDWR so the open doesn't block
         * (no peer required), then read — which blocks because the
         * pipe is empty.  Then we expect SIGKILL to land. */
        int fd = open(path, O_RDWR);
        if (fd < 0) _exit(40);
        char buf;
        ssize_t n = read(fd, &buf, 1);
        if (n == 0) _exit(41);
        if (n > 0)  _exit(42);
        _exit(43);
    }
    short_sleep_ms(100);
    MUST(kill(pid, SIGKILL) == 0, "kill SIGKILL");
    int status = 0;
    pid_t r = waitpid(pid, &status, 0);
    unlink(path);
    MUST(r == pid, "waitpid");
    MUST(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
         "child not killed by SIGKILL");
    return 0;
}

TEST(sigkill_terminates_nanosleep)
{
    pid_t pid = fork();
    MUST(pid >= 0, "fork");
    if (pid == 0) {
        struct timespec ts = { 60, 0 };   /* sleep 60 seconds */
        nanosleep(&ts, NULL);
        _exit(50);   /* should never reach */
    }
    short_sleep_ms(100);
    MUST(kill(pid, SIGKILL) == 0, "kill SIGKILL");
    int status = 0;
    pid_t r = waitpid(pid, &status, 0);
    MUST(r == pid, "waitpid");
    MUST(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
         "child not killed by SIGKILL");
    return 0;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    int tests_run = 0, tests_pass = 0, tests_fail = 0, tests_skip = 0;

    fprintf(stdout, "torture_signal: SIGKILL/SIGINT must interrupt blocked syscalls\n");
    fprintf(stdout, "----------------------------------------------------\n");

    RUN(sigkill_terminates_pipe_read);
    RUN(sigint_handler_returns_eintr);
    RUN(sigkill_terminates_fifo_read);
    RUN(sigkill_terminates_nanosleep);

    fprintf(stdout, "----------------------------------------------------\n");
    fprintf(stdout, "Result: %d/%d passed", tests_pass, tests_run);
    if (tests_skip) fprintf(stdout, ", %d skipped", tests_skip);
    if (tests_fail) fprintf(stdout, ", %d FAILED", tests_fail);
    fprintf(stdout, "\n");

    return tests_fail == 0 ? 0 : 1;
}
