/*
 * pdtest -- process descriptors: pdfork(2), pdgetpid(2), pdkill(2), and
 * what closing one does.
 *
 * A FreeBSD program, built on FreeBSD or by the cc of a FreeBSD tree under
 * substrate:
 *
 *      cc -O1 -Wall -Wextra -o pdtest pdtest.c
 *      ./pdtest             # every line "ok", then "pdtest: PASS"
 *
 * libcasper starts its helper with pdfork, so every base utility that
 * sandboxes itself depends on these.  Where FreeBSD hands a process whose
 * descriptor is closed to init, substrate leaves it its parent's to wait
 * for; the checks of a closed descriptor accept either.
 */
#include <sys/types.h>
#include <sys/wait.h>
/* A tree unpacked without all its headers has the calls in libc and not
 * this one to declare them. */
#if defined(__has_include) && __has_include(<sys/procdesc.h>)
#include <sys/procdesc.h>
#else
#define PD_DAEMON 0x00000001
pid_t pdfork(int *, int);
int pdgetpid(int, pid_t *);
int pdkill(int, int);
#endif
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

static void nap_ms(long ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

    nanosleep(&ts, NULL);
}

static void child_waits(void) {
    for (;;) pause();
}

/* Is `pid` gone within two seconds?  Collected by us, or not ours and not there. */
static int gone(pid_t pid, int *status) {
    int i;

    *status = 0;
    for (i = 0; i < 40; i++) {
        pid_t r = waitpid(pid, status, WNOHANG);

        if (r == pid) return 1;
        if (r == -1 && errno == ECHILD && kill(pid, 0) == -1 && errno == ESRCH)
            return 1;
        nap_ms(50);
    }
    return 0;
}

int main(void) {
    int fd = -1, st = 0;
    pid_t pid, got = -1;

    /* A descriptor is of the process pdfork made, and signals it. */
    pid = pdfork(&fd, 0);
    if (pid == 0) child_waits();
    check(pid > 0 && fd >= 0, "pdfork gives the parent a process and a descriptor");
    check(pdgetpid(fd, &got) == 0 && got == pid, "pdgetpid names that process");
    check(pdkill(fd, SIGTERM) == 0, "pdkill signals it");
    check(waitpid(pid, &st, 0) == pid && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM,
          "and it died of that signal");
    errno = 0;
    check(pdkill(fd, SIGTERM) == -1 && errno == ESRCH,
          "pdkill of a process that is gone: ESRCH");
    check(close(fd) == 0, "its descriptor closes");

    /* Closing the descriptor of a live process kills it. */
    pid = pdfork(&fd, 0);
    if (pid == 0) child_waits();
    check(kill(pid, 0) == 0, "a second child is running");
    close(fd);
    check(gone(pid, &st), "closing its descriptor ends it");

    /* Unless it was made a daemon. */
    pid = pdfork(&fd, PD_DAEMON);
    if (pid == 0) child_waits();
    close(fd);
    nap_ms(300);
    check(kill(pid, 0) == 0, "a PD_DAEMON child outlives its descriptor");
    kill(pid, SIGKILL);
    check(gone(pid, &st), "until it is killed");

    /* A descriptor that is not a process's. */
    errno = 0;
    check(pdgetpid(0, &got) == -1 && errno != 0, "pdgetpid of another kind of descriptor fails");

    /* The child has no descriptor for itself. */
    pid = pdfork(&fd, 0);
    if (pid == 0) {
        pid_t p;

        _exit(pdgetpid(fd, &p) == -1 ? 0 : 1);
    }
    check(waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0,
          "the child does not have the descriptor");
    close(fd);

    printf("pdtest: %s\n", failed ? "FAILED" : "PASS");
    return failed;
}
