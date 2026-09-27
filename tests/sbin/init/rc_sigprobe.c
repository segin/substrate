/*
 * rc_sigprobe.c -- processes init starts get default signal handling.
 *
 * init ignores SIGHUP for itself, and the children it forked for /etc/rc,
 * getty and the shutdown scripts inherited that across exec: no daemon,
 * rc script or login session could be hung up.  Installed as an /etc/rc.d
 * script (rc runs it with an argument, "start" or "stop"), this probe
 * reports every signal it inherited as ignored or blocked.  /bin/sh passes
 * ignored signals on to what it runs, so what the probe sees is what init
 * handed /etc/rc.
 *
 * Boot the real init with this at /etc/rc.d/03-sigprobe; the probe prints
 * a "Result:" line on the console.
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    int bad = 0;
    sigset_t mask;

    if (argc > 1 && strcmp(argv[1], "start") != 0)
        return 0;
    printf("rc_sigprobe\n");
    for (int sig = 1; sig < 32; sig++) {
        struct sigaction sa;
        if (sig == SIGKILL || sig == SIGSTOP)
            continue;
        if (sigaction(sig, NULL, &sa) == 0 && sa.sa_handler == SIG_IGN) {
            printf("  FAIL signal %d is ignored\n", sig);
            bad++;
        }
    }
    if (sigprocmask(SIG_BLOCK, NULL, &mask) == 0) {
        for (int sig = 1; sig < 32; sig++) {
            if (sigismember(&mask, sig)) {
                printf("  FAIL signal %d is blocked\n", sig);
                bad++;
            }
        }
    }
    printf("Result: %s\n", bad ? "FAILED" : "PASSED");
    fflush(stdout);
    return 0;
}
