/*
 * torture_rlimit.c — getrlimit/setrlimit and the limits substrate enforces.
 *
 * Two halves.  The first is the interface itself: round-tripping a value,
 * the argument checks, the privilege rule for raising a hard limit, and the
 * POSIX inheritance rules.  The second actually spends the resource and
 * checks the kernel says no in the documented way -- a limit that reads back
 * correctly but never binds is exactly the state substrate was in before
 * (setrlimit accepted everything and enforced nothing).
 *
 * Runs as root on target, which is why the EPERM case re-tests after
 * dropping privilege rather than assuming it.
 *
 * Build: make CROSS=/opt/substrate/bin/i386-unknown-substrate-
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

static void ok(const char *what)   { printf("ok       %s\n", what); }
static void fail(const char *what) { printf("FAIL     %s\n", what); failures++; }
static void check(int cond, const char *what) { cond ? ok(what) : fail(what); }

static void failf(const char *what, long got, long want)
{
    printf("FAIL     %s (got %ld, wanted %ld)\n", what, got, want);
    failures++;
}

/* ---------------------------------------------------------------- */
/* The interface                                                     */
/* ---------------------------------------------------------------- */

static void test_roundtrip(void)
{
    struct rlimit rl, back;

    if (getrlimit(RLIMIT_FSIZE, &rl) != 0) { fail("getrlimit(RLIMIT_FSIZE)"); return; }
    ok("getrlimit returns");

    rl.rlim_cur = 8192;
    if (setrlimit(RLIMIT_FSIZE, &rl) != 0) { fail("setrlimit lowering soft"); return; }
    if (getrlimit(RLIMIT_FSIZE, &back) != 0) { fail("getrlimit after set"); return; }

    if (back.rlim_cur == 8192) ok("soft limit round-trips");
    else failf("soft limit round-trips", (long)back.rlim_cur, 8192);
}

static void test_einval(void)
{
    struct rlimit rl;

    /* A resource number the kernel does not know. */
    if (getrlimit(9999, &rl) == -1 && errno == EINVAL) ok("getrlimit bad resource -> EINVAL");
    else fail("getrlimit bad resource -> EINVAL");

    /* Soft above hard is nonsense and must be refused. */
    if (getrlimit(RLIMIT_FSIZE, &rl) != 0) { fail("getrlimit for EINVAL test"); return; }
    rl.rlim_max = 4096;
    rl.rlim_cur = 8192;
    if (setrlimit(RLIMIT_FSIZE, &rl) == -1 && errno == EINVAL) ok("soft > hard -> EINVAL");
    else fail("soft > hard -> EINVAL");
}

static void test_hard_limit_is_a_ratchet(void)
{
    struct rlimit rl, back;
    pid_t pid;
    int st;

    /* Lower the hard limit, then try to raise it again as an unprivileged
     * child.  Done in a child so the drop cannot affect the rest of the run. */
    if (getrlimit(RLIMIT_FSIZE, &rl) != 0) { fail("getrlimit for ratchet test"); return; }
    rl.rlim_cur = 4096;
    rl.rlim_max = 4096;
    if (setrlimit(RLIMIT_FSIZE, &rl) != 0) { fail("lower hard limit"); return; }
    ok("hard limit lowered");

    pid = fork();
    if (pid == 0) {
        struct rlimit up;

        if (setuid(65534) != 0) _exit(70);       /* nobody */
        if (getrlimit(RLIMIT_FSIZE, &up) != 0) _exit(71);
        up.rlim_max = 1 << 20;
        up.rlim_cur = 1 << 20;
        if (setrlimit(RLIMIT_FSIZE, &up) == 0) _exit(1);      /* should not work */
        _exit(errno == EPERM ? 0 : 2);
    }
    if (pid < 0) { fail("fork for ratchet test"); return; }
    waitpid(pid, &st, 0);

    if (WIFEXITED(st) && WEXITSTATUS(st) == 0)      ok("unprivileged cannot raise the hard limit");
    else if (WIFEXITED(st) && WEXITSTATUS(st) == 70) printf("skip     setuid unavailable\n");
    else fail("unprivileged cannot raise the hard limit");

    /* Root may raise it again; anyone else is stuck with the lower ceiling
     * for the life of the process, which is the whole point of the rule. */
    if (getrlimit(RLIMIT_FSIZE, &back) == 0) {
        back.rlim_cur = RLIM_INFINITY;
        back.rlim_max = RLIM_INFINITY;
        if (geteuid() == 0)
            check(setrlimit(RLIMIT_FSIZE, &back) == 0, "root may raise the hard limit");
        else if (setrlimit(RLIMIT_FSIZE, &back) == -1 && errno == EPERM)
            ok("non-root cannot raise the hard limit back");
        else
            fail("non-root cannot raise the hard limit back");
    }
}

static void test_inherited_by_fork(void)
{
    struct rlimit rl;
    pid_t pid;
    int st;

    if (getrlimit(RLIMIT_CORE, &rl) != 0) { fail("getrlimit(RLIMIT_CORE)"); return; }
    rl.rlim_cur = 12345;
    if (setrlimit(RLIMIT_CORE, &rl) != 0) { fail("set RLIMIT_CORE"); return; }

    pid = fork();
    if (pid == 0) {
        struct rlimit child;

        if (getrlimit(RLIMIT_CORE, &child) != 0) _exit(1);
        _exit(child.rlim_cur == 12345 ? 0 : 2);
    }
    if (pid < 0) { fail("fork for inheritance test"); return; }
    waitpid(pid, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "limits inherited across fork");
}

/* ---------------------------------------------------------------- */
/* Enforcement                                                       */
/* ---------------------------------------------------------------- */

static void test_nofile(void)
{
    pid_t pid;
    int st;

    pid = fork();
    if (pid == 0) {
        struct rlimit rl;
        int fd, opened = 0;

        rl.rlim_cur = 8;                 /* highest usable descriptor is 7 */
        rl.rlim_max = 8;
        if (setrlimit(RLIMIT_NOFILE, &rl) != 0) _exit(1);

        for (;;) {
            fd = open("/dev/null", O_RDONLY);
            if (fd < 0) break;
            if (fd >= 8) _exit(2);       /* handed out a descriptor past the limit */
            opened++;
            if (opened > 64) _exit(3);   /* limit never bound */
        }
        _exit(errno == EMFILE ? 0 : 4);
    }
    if (pid < 0) { fail("fork for NOFILE test"); return; }
    waitpid(pid, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "RLIMIT_NOFILE -> EMFILE");
}

static void test_fsize(void)
{
    pid_t pid;
    int st;

    pid = fork();
    if (pid == 0) {
        struct rlimit rl;
        char buf[512];
        int fd;
        ssize_t n, total = 0;

        signal(SIGXFSZ, SIG_IGN);        /* we want the errno, not death */
        rl.rlim_cur = 1024;
        rl.rlim_max = 1024;
        if (setrlimit(RLIMIT_FSIZE, &rl) != 0) _exit(1);

        fd = open("/tmp/rlimit-fsize.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) _exit(2);
        memset(buf, 'x', sizeof(buf));

        /* Writes up to the limit succeed (the last one short); past it the
         * write must fail with EFBIG rather than growing the file. */
        for (;;) {
            n = write(fd, buf, sizeof(buf));
            if (n < 0) break;
            if (n == 0) _exit(3);
            total += n;
            if (total > 4096) _exit(4);  /* limit never bound */
        }
        close(fd);
        unlink("/tmp/rlimit-fsize.tmp");
        if (errno != EFBIG) _exit(5);
        _exit(total == 1024 ? 0 : 6);
    }
    if (pid < 0) { fail("fork for FSIZE test"); return; }
    waitpid(pid, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
          "RLIMIT_FSIZE truncates at the limit then EFBIG");
}

static void test_nproc(void)
{
    pid_t pid;
    int st;

    pid = fork();
    if (pid == 0) {
        struct rlimit rl;
        int forks = 0;

        /* NPROC is exempt for root, so this only means anything unprivileged. */
        rl.rlim_cur = 2;
        rl.rlim_max = 2;
        if (setrlimit(RLIMIT_NPROC, &rl) != 0) _exit(1);
        if (setuid(65534) != 0) _exit(70);

        for (;;) {
            pid_t k = fork();

            if (k == 0) { pause(); _exit(0); }   /* child just waits to be reaped */
            if (k < 0) break;
            forks++;
            if (forks > 32) _exit(2);            /* limit never bound */
        }
        _exit(errno == EAGAIN ? 0 : 3);
    }
    if (pid < 0) { fail("fork for NPROC test"); return; }
    waitpid(pid, &st, 0);

    if (WIFEXITED(st) && WEXITSTATUS(st) == 70) printf("skip     NPROC (setuid unavailable)\n");
    else check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "RLIMIT_NPROC -> EAGAIN");
}

int main(void)
{
    printf("torture_rlimit: interface\n");
    test_roundtrip();
    test_einval();
    test_hard_limit_is_a_ratchet();
    test_inherited_by_fork();

    printf("torture_rlimit: enforcement\n");
    test_nofile();
    test_fsize();
    test_nproc();

    printf("torture_rlimit: %s (%d failure%s)\n",
           failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
