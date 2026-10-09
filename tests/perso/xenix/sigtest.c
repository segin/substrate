/*
 * Signals under the Xenix/286 personality, from inside a Xenix process.
 * Compiled by Xenix's own cc, so written in the C of the time.  See
 * README.md.
 */
#include <stdio.h>
#include <signal.h>

int caught;
int last;

int handler(sig)
int sig;
{
    caught++;
    last = sig;
    signal(sig, handler);       /* the disposition was reset on the way in */
    return 0;
}

int bad;

check(what, ok)
char *what;
int ok;
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        bad = 1;
}

main()
{
    int i;
    int (*old)();
    char what[64];

    /* The old disposition comes back, and the two that are not functions
     * are told from one that is whatever the registers held. */
    old = signal(SIGINT, SIG_IGN);
    check("a signal starts out with the default action", old == SIG_DFL);
    kill(getpid(), SIGINT);
    check("an ignored signal is ignored", caught == 0);
    old = signal(SIGINT, handler);
    check("signal gives back SIG_IGN when that is what was set", old == SIG_IGN);

    /* The same signal, more than once: each is delivered, its number
     * reaches the function, and the interrupted code goes on with its
     * registers. */
    for (i = 1; i <= 4; i++) {
        register int keep;

        keep = i * 1000 + 7;
        kill(getpid(), SIGINT);
        sprintf(what, "delivery %d of the same signal", i);
        check(what, caught == i && last == SIGINT && keep == i * 1000 + 7);
    }

    /* Another signal has its own trampoline and so its own number. */
    signal(SIGTERM, handler);
    kill(getpid(), SIGTERM);
    check("a second signal arrives as itself", caught == 5 && last == SIGTERM);

    /* alarm(2) twice, which is what two calls of sleep(3) are. */
    signal(SIGALRM, handler);
    alarm(1);
    pause();
    check("the first alarm", last == SIGALRM && caught == 6);
    alarm(1);
    pause();
    check("the second alarm", last == SIGALRM && caught == 7);

    printf(bad ? "sigtest: FAIL\n" : "sigtest: PASS\n");
    return bad;
}
