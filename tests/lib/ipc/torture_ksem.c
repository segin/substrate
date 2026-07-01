/*
 * torture_ksem.c — POSIX named / process-shared semaphore torture suite.
 *
 * Exercises the sem_open / sem_close / sem_unlink named-semaphore family and
 * process-shared sem_init(pshared) — the kernel-backed "ksem" objects
 * (sys/kern/posix_sem.c on substrate) — plus a regression pass over the
 * process-local futex sem_init path.
 *
 * Pure POSIX C.  Builds against the host libc by default (must PASS on Linux
 * first — a failure here that does NOT reproduce on host libc points at a
 * substrate kernel/libc bug); against substrate's libpthread + kernel ksem
 * subsystem when built with CROSS=/opt/substrate/bin/i386-unknown-substrate-.
 *
 * Coverage:
 *   - sem_open: O_CREAT create, get-existing attach, O_CREAT|O_EXCL -> EEXIST,
 *     missing-without-O_CREAT -> ENOENT, bad name (no leading '/') -> EINVAL.
 *   - sem_getvalue reflects the initial value and tracks post/wait.
 *   - sem_trywait -> EAGAIN at zero; sem_wait after sem_post succeeds.
 *   - sem_timedwait -> ETIMEDOUT on an elapsed absolute deadline; returns
 *     promptly when a post beats the deadline.
 *   - sem_unlink removes the name (subsequent plain open -> ENOENT) but an
 *     already-open descriptor keeps working (POSIX persistence).
 *   - Cross-process: parent opens value 0, child opens the SAME name and posts
 *     N times, parent waits N times and counts — proves the object is shared.
 *   - Process-shared unnamed sem_init in MAP_SHARED memory across fork.
 *   - Process-LOCAL unnamed sem_init regression (must be unchanged).
 *
 * Every test runs in a forked child under an alarm(2) watchdog so a hung
 * blocking test is reaped as HANG and the suite continues.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

#ifndef SEM_VALUE_MAX
#define SEM_VALUE_MAX 2147483647
#endif

#define TEST_TIMEOUT 8
#define KSEM_NAME    "/ksem_torture"

static int tests_run, tests_pass, tests_fail, tests_hang, tests_skip;
typedef int (*testfn)(void);

static void alrm_noop(int s){ (void)s; }

/* Each test runs in its own child so a blocking hang can't wedge the suite and
 * a leaked named object can't bleed into the next test. */
static void run_one(const char *name, testfn fn)
{
    fprintf(stdout, "[%2d] %-32s ", ++tests_run, name); fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { fprintf(stdout, "FORK-FAIL errno=%d\n", errno); tests_fail++; return; }
    if (pid == 0) {
        signal(SIGALRM, alrm_noop);
        alarm(TEST_TIMEOUT);
        _exit(fn());               /* 0 = PASS, 1 = FAIL, 2 = SKIP */
    }
    int st;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (WIFSIGNALED(st)) {
        fprintf(stdout, "HANG/SIG(%d)\n", WTERMSIG(st)); tests_hang++;
    } else if (WIFEXITED(st)) {
        int rc = WEXITSTATUS(st);
        if (rc == 0)      { fprintf(stdout, "PASS\n"); tests_pass++; }
        else if (rc == 2) { fprintf(stdout, "SKIP\n"); tests_skip++; }
        else              { fprintf(stdout, "FAIL\n"); tests_fail++; }
    } else {
        fprintf(stdout, "UNKNOWN\n"); tests_fail++;
    }
}
#define RUN(fn) run_one(#fn, fn)

/* An absolute CLOCK_REALTIME deadline `ms` milliseconds from now. */
static void abs_deadline(struct timespec *ts, long ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

/* ---- named semaphore tests ---- */

static int t_open_getvalue(void)
{
    sem_unlink(KSEM_NAME);
    sem_t *s = sem_open(KSEM_NAME, O_CREAT | O_RDWR, 0666, 5);
    if (s == SEM_FAILED) return 1;
    int v = -1;
    if (sem_getvalue(s, &v) != 0 || v != 5) { sem_close(s); sem_unlink(KSEM_NAME); return 1; }
    sem_close(s);
    sem_unlink(KSEM_NAME);
    return 0;
}

static int t_open_excl_eexist(void)
{
    sem_unlink(KSEM_NAME);
    sem_t *a = sem_open(KSEM_NAME, O_CREAT | O_RDWR, 0666, 1);
    if (a == SEM_FAILED) return 1;
    errno = 0;
    sem_t *b = sem_open(KSEM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666, 1);
    int ok = (b == SEM_FAILED && errno == EEXIST);
    if (b != SEM_FAILED) sem_close(b);
    sem_close(a);
    sem_unlink(KSEM_NAME);
    return ok ? 0 : 1;
}

static int t_open_noent(void)
{
    sem_unlink(KSEM_NAME);
    errno = 0;
    sem_t *s = sem_open(KSEM_NAME, O_RDWR);   /* no O_CREAT, does not exist */
    int ok = (s == SEM_FAILED && errno == ENOENT);
    if (s != SEM_FAILED) { sem_close(s); sem_unlink(KSEM_NAME); }
    return ok ? 0 : 1;
}

static int t_open_badname(void)
{
    /* A name with no leading '/' is implementation-defined: substrate (like
     * FreeBSD's ksem) is strict and returns EINVAL; glibc is lenient and
     * accepts it.  Accept either, so the suite stays green on both. */
    errno = 0;
    sem_t *s = sem_open("noslash", O_CREAT | O_RDWR, 0666, 1);
    if (s == SEM_FAILED)
        return (errno == EINVAL) ? 0 : 1;
    sem_close(s);
    sem_unlink("noslash");
    sem_unlink("/noslash");
    return 0;                         /* lenient host: accepted */
}

static int t_trywait_eagain(void)
{
    sem_unlink(KSEM_NAME);
    sem_t *s = sem_open(KSEM_NAME, O_CREAT | O_RDWR, 0666, 0);
    if (s == SEM_FAILED) return 1;
    errno = 0;
    int r = sem_trywait(s);
    int ok = (r == -1 && errno == EAGAIN);
    sem_close(s);
    sem_unlink(KSEM_NAME);
    return ok ? 0 : 1;
}

static int t_wait_after_post(void)
{
    sem_unlink(KSEM_NAME);
    sem_t *s = sem_open(KSEM_NAME, O_CREAT | O_RDWR, 0666, 0);
    if (s == SEM_FAILED) return 1;
    int ok = 1;
    if (sem_post(s) != 0) ok = 0;
    if (ok && sem_wait(s) != 0) ok = 0;
    int v = -1;
    if (ok && (sem_getvalue(s, &v) != 0 || v != 0)) ok = 0;
    sem_close(s);
    sem_unlink(KSEM_NAME);
    return ok ? 0 : 1;
}

static int t_timedwait_timeout(void)
{
    sem_unlink(KSEM_NAME);
    sem_t *s = sem_open(KSEM_NAME, O_CREAT | O_RDWR, 0666, 0);
    if (s == SEM_FAILED) return 1;
    struct timespec ts;
    abs_deadline(&ts, 200);
    errno = 0;
    int r = sem_timedwait(s, &ts);
    int ok = (r == -1 && errno == ETIMEDOUT);
    sem_close(s);
    sem_unlink(KSEM_NAME);
    return ok ? 0 : 1;
}

static int t_timedwait_success(void)
{
    sem_unlink(KSEM_NAME);
    sem_t *s = sem_open(KSEM_NAME, O_CREAT | O_RDWR, 0666, 0);
    if (s == SEM_FAILED) return 1;
    if (sem_post(s) != 0) { sem_close(s); sem_unlink(KSEM_NAME); return 1; }
    struct timespec ts;
    abs_deadline(&ts, 2000);
    int r = sem_timedwait(s, &ts);
    sem_close(s);
    sem_unlink(KSEM_NAME);
    return r == 0 ? 0 : 1;
}

static int t_unlink_persists_open(void)
{
    sem_unlink(KSEM_NAME);
    sem_t *s = sem_open(KSEM_NAME, O_CREAT | O_RDWR, 0666, 0);
    if (s == SEM_FAILED) return 1;
    /* Unlink the name; the object must survive because s is still open. */
    if (sem_unlink(KSEM_NAME) != 0) { sem_close(s); return 1; }
    /* A fresh open by name must now miss. */
    errno = 0;
    sem_t *miss = sem_open(KSEM_NAME, O_RDWR);
    if (miss != SEM_FAILED || errno != ENOENT) {
        if (miss != SEM_FAILED) sem_close(miss);
        sem_close(s);
        return 1;
    }
    /* The still-open descriptor keeps working. */
    int ok = (sem_post(s) == 0 && sem_wait(s) == 0);
    sem_close(s);
    return ok ? 0 : 1;
}

/* Parent opens value 0; child opens the SAME name and posts N times; parent
 * waits N times.  Every wait must succeed and drain to exactly 0 — proof that
 * both descriptors reference one shared kernel object. */
static int t_cross_process_roundtrip(void)
{
    const int N = 8;
    sem_unlink(KSEM_NAME);
    sem_t *p = sem_open(KSEM_NAME, O_CREAT | O_RDWR, 0666, 0);
    if (p == SEM_FAILED) return 1;

    pid_t pid = fork();
    if (pid < 0) { sem_close(p); sem_unlink(KSEM_NAME); return 1; }
    if (pid == 0) {
        sem_t *c = sem_open(KSEM_NAME, O_RDWR);
        if (c == SEM_FAILED) _exit(3);
        for (int i = 0; i < N; i++) {
            if (sem_post(c) != 0) _exit(4);
            usleep(1000);
        }
        sem_close(c);
        _exit(0);
    }

    int got = 0;
    for (int i = 0; i < N; i++) {
        if (sem_wait(p) == 0) got++;
    }
    int cst;
    while (waitpid(pid, &cst, 0) < 0 && errno == EINTR) {}
    int v = -1;
    sem_getvalue(p, &v);
    sem_close(p);
    sem_unlink(KSEM_NAME);
    int child_ok = WIFEXITED(cst) && WEXITSTATUS(cst) == 0;
    return (got == N && v == 0 && child_ok) ? 0 : 1;
}

/* ---- process-shared unnamed sem_init across fork ---- */

static int t_pshared_fork(void)
{
    sem_t *s = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (s == MAP_FAILED) return 1;
    if (sem_init(s, 1 /* pshared */, 0) != 0) { munmap(s, sizeof(sem_t)); return 1; }

    const int N = 5;
    pid_t pid = fork();
    if (pid < 0) { sem_destroy(s); munmap(s, sizeof(sem_t)); return 1; }
    if (pid == 0) {
        for (int i = 0; i < N; i++) { sem_post(s); usleep(1000); }
        _exit(0);
    }
    int got = 0;
    for (int i = 0; i < N; i++) if (sem_wait(s) == 0) got++;
    int cst;
    while (waitpid(pid, &cst, 0) < 0 && errno == EINTR) {}
    int v = -1;
    sem_getvalue(s, &v);
    sem_destroy(s);
    munmap(s, sizeof(sem_t));
    int child_ok = WIFEXITED(cst) && WEXITSTATUS(cst) == 0;
    return (got == N && v == 0 && child_ok) ? 0 : 1;
}

/* pshared as a mutex guarding a shared counter across fork. */
static int t_pshared_mutex(void)
{
    struct shared { sem_t mtx; long counter; };
    struct shared *sh = mmap(NULL, sizeof(*sh), PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sh == MAP_FAILED) return 1;
    sh->counter = 0;
    if (sem_init(&sh->mtx, 1, 1) != 0) { munmap(sh, sizeof(*sh)); return 1; }

    const int ITERS = 200;
    pid_t pid = fork();
    if (pid < 0) { sem_destroy(&sh->mtx); munmap(sh, sizeof(*sh)); return 1; }
    if (pid == 0) {
        for (int i = 0; i < ITERS; i++) {
            sem_wait(&sh->mtx);
            sh->counter++;
            sem_post(&sh->mtx);
        }
        _exit(0);
    }
    for (int i = 0; i < ITERS; i++) {
        sem_wait(&sh->mtx);
        sh->counter++;
        sem_post(&sh->mtx);
    }
    int cst;
    while (waitpid(pid, &cst, 0) < 0 && errno == EINTR) {}
    long final = sh->counter;
    sem_destroy(&sh->mtx);
    munmap(sh, sizeof(*sh));
    return (final == 2 * ITERS) ? 0 : 1;
}

/* ---- process-local unnamed sem_init regression ---- */

static int t_local_basic(void)
{
    sem_t s;
    if (sem_init(&s, 0, 2) != 0) return 1;
    int v = -1;
    if (sem_getvalue(&s, &v) != 0 || v != 2) return 1;
    if (sem_wait(&s) != 0) return 1;
    if (sem_wait(&s) != 0) return 1;
    errno = 0;
    if (sem_trywait(&s) != -1 || errno != EAGAIN) return 1;
    if (sem_post(&s) != 0) return 1;
    if (sem_trywait(&s) != 0) return 1;
    sem_destroy(&s);
    return 0;
}

static int t_local_fork_shared(void)
{
    /* A process-local sem in MAP_SHARED memory still coordinates across fork
     * on substrate only because sem_post/sem_wait touch the count word via the
     * futex in the shared page — this exercises the fast path under fork. */
    sem_t *s = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (s == MAP_FAILED) return 1;
    if (sem_init(s, 0, 0) != 0) { munmap(s, sizeof(sem_t)); return 1; }
    pid_t pid = fork();
    if (pid < 0) { munmap(s, sizeof(sem_t)); return 1; }
    if (pid == 0) { usleep(2000); sem_post(s); _exit(0); }
    int r = sem_wait(s);
    int cst;
    while (waitpid(pid, &cst, 0) < 0 && errno == EINTR) {}
    munmap(s, sizeof(sem_t));
    return r == 0 ? 0 : 1;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    fprintf(stdout, "torture_ksem: POSIX named / process-shared semaphores\n");
    fprintf(stdout, "----------------------------------------------------\n");

    RUN(t_open_getvalue);
    RUN(t_open_excl_eexist);
    RUN(t_open_noent);
    RUN(t_open_badname);
    RUN(t_trywait_eagain);
    RUN(t_wait_after_post);
    RUN(t_timedwait_timeout);
    RUN(t_timedwait_success);
    RUN(t_unlink_persists_open);
    RUN(t_cross_process_roundtrip);
    RUN(t_pshared_fork);
    RUN(t_pshared_mutex);
    RUN(t_local_basic);
    RUN(t_local_fork_shared);

    fprintf(stdout, "----------------------------------------------------\n");
    fprintf(stdout, "Result: %d/%d passed", tests_pass, tests_run);
    if (tests_skip) fprintf(stdout, ", %d skipped", tests_skip);
    if (tests_hang) fprintf(stdout, ", %d HUNG", tests_hang);
    if (tests_fail) fprintf(stdout, ", %d FAILED", tests_fail);
    fprintf(stdout, "\n");

    return (tests_fail || tests_hang) ? 1 : 0;
}
