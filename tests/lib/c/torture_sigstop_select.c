/*
 * torture_sigstop_select.c - regression for the job-control STOP/CONT
 * zombie-resurrection kernel wedge/panic (OPTS signals/sigaction/9-1).
 *
 * Run as init (PID 1) under a headless TCG boot.  Because init's parent is
 * the swapper (a different session), init's process group is permanently
 * orphaned, exactly like the Open POSIX Test Suite driver's group.  Each
 * worker forks a child that blocks forever in select(0,NULL,NULL,NULL,NULL)
 * and then drives it through SIGSTOP -> SIGCONT -> SIGSTOP -> SIGCONT ->
 * SIGKILL.  While one worker's child sits stopped, another worker reaping a
 * child re-orphans the group and the kernel delivers SIGHUP+SIGCONT to the
 * stopped members.  Under that churn a just-zombified thread could be flipped
 * back to THREAD_READY and re-scheduled: switch_to() then returned into
 * proc_exit()'s post-yield while(1) (a preempt-disabled spin that wedged the
 * CPU) or, once its kernel stack had been reaped, into a freed stack, faulting
 * in the kernel at a garbage EIP (EIP=0x282 / a thread_t address).
 *
 * Workers ignore SIGHUP so they survive and reap their own children (keeping
 * the group populated and churning); children take the default SIGHUP so the
 * orphan-hangup path exercises them.  Expect sustained "hb w0 iter=" output
 * with no kernel panic/hang.  Build static (i386) and boot with init=.
 */
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static volatile int child_stopped;

static void put(const char *s) { write(1, s, strlen(s)); }
static void putn(long n) {
    char b[24]; int i = 23; b[i--] = 0; int neg = n < 0;
    unsigned long u = neg ? (unsigned long)(-n) : (unsigned long)n;
    if (!u) { b[i--] = '0'; }
    while (u) { b[i--] = '0' + (u % 10); u /= 10; }
    if (neg) { b[i--] = '-'; }
    write(1, b + i + 1, strlen(b + i + 1));
}
static void handler(int sig, siginfo_t *info, void *ctx) {
    (void)sig; (void)ctx;
    if (info && info->si_code == CLD_STOPPED) child_stopped++;
}
static void msleep(int ms) {
    if (ms <= 0) return;
    struct timeval tv; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    select(0, NULL, NULL, NULL, &tv);
}

#define NWORKERS 8

static void worker(int id) {
    signal(SIGHUP, SIG_IGN);   /* survive the orphaned-group hangup */
    struct sigaction act; memset(&act, 0, sizeof act);
    act.sa_sigaction = handler; act.sa_flags = SA_SIGINFO;
    sigemptyset(&act.sa_mask); sigaction(SIGCHLD, &act, 0);
    unsigned long r = 0x1234567u ^ (unsigned)(id * 2654435761u);
    for (unsigned long iter = 0; ; iter++) {
        pid_t pid = fork();
        if (pid < 0) { msleep(5); continue; }
        if (pid == 0) {
            signal(SIGHUP, SIG_DFL);
            for (;;) select(0, NULL, NULL, NULL, NULL);
            _exit(0);
        }
        r = r * 1103515245u + 12345u; int d1 = 2 + (int)((r >> 16) % 14);
        r = r * 1103515245u + 12345u; int d2 = (int)((r >> 16) % 6);
        kill(pid, SIGSTOP); msleep(d1); kill(pid, SIGCONT);
        kill(pid, SIGSTOP); msleep(d2); kill(pid, SIGCONT);
        kill(pid, SIGKILL);
        int s; waitpid(pid, &s, 0);
        if (id == 0 && (iter % 200) == 0) {
            put("hb w0 iter="); putn((long)iter);
            put(" stops="); putn(child_stopped); put("\n");
        }
    }
}

int main(void) {
    put("torture_sigstop_select start nworkers="); putn(NWORKERS); put("\n");
    for (int i = 0; i < NWORKERS; i++) {
        pid_t w = fork();
        if (w == 0) { worker(i); _exit(0); }
    }
    for (;;) { int s; if (waitpid(-1, &s, 0) < 0) msleep(100); }
    return 0;
}
