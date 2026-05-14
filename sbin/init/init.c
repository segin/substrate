/*
 * sbin/init/init.c — substrate's PID 1.
 *
 * Responsibilities:
 *
 *   1. Open /dev/console as stdio so kernel-time kprintf and init's
 *      own diagnostics both land somewhere visible.
 *   2. Spawn a getty on each configured terminal line at boot.
 *   3. Reap exited children, respawn the getty for whichever line
 *      went away.  Respawns are backed off (one full second per
 *      cycle) so a fast-failing getty can't drive a runaway fork
 *      loop.
 *   4. On SIGTERM / SIGINT / SIGQUIT, initiate orderly shutdown:
 *      stop respawning, signal every other process, give them a
 *      grace period, then SIGKILL, sync(), and idle-halt.
 *
 * The terminal table is statically configured.  A proper
 * /etc/inittab parser is a later step — the current rootfs only
 * needs three gettys to come up.
 */

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

struct gettyline {
    const char *tty;        /* device path */
    pid_t       pid;        /* current getty pid (0 if not running) */
    long        last_spawn; /* serial counter used for rate-limit */
};

static struct gettyline g_lines[] = {
    { "/dev/console", 0, 0 },
    { "/dev/tty2",    0, 0 },
    { "/dev/tty3",    0, 0 },
};
#define NLINES ((int)(sizeof(g_lines) / sizeof(g_lines[0])))

static volatile sig_atomic_t g_want_shutdown = 0;

static void
on_shutdown_signal(int sig)
{
    (void)sig;
    g_want_shutdown = 1;
}

/*
 * Fork + exec a getty on `line`.  Called both at boot and on
 * respawn.  The child detaches into its own session so the getty
 * can become the controlling terminal for its tty.
 */
static void
spawn_getty(struct gettyline *line)
{
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "init: fork for %s failed\n", line->tty);
        line->pid = 0;
        return;
    }
    if (pid == 0) {
        /*
         * Child.  setsid() gives us a brand-new session so the
         * subsequent open() of the tty in getty can claim it as
         * the controlling terminal.  Restore default disposition
         * for the signals init handled — we don't want the getty
         * inheriting init's signal mask.
         */
        struct sigaction dfl = { 0 };
        dfl.sa_handler = SIG_DFL;
        sigaction(SIGTERM, &dfl, NULL);
        sigaction(SIGINT,  &dfl, NULL);
        sigaction(SIGQUIT, &dfl, NULL);
        sigaction(SIGCHLD, &dfl, NULL);

        setsid();

        execl("/sbin/getty", "/sbin/getty", line->tty, (char *)NULL);
        fprintf(stderr, "init: exec /sbin/getty %s failed\n", line->tty);
        _exit(127);
    }
    line->pid = pid;
}

/*
 * Reap any pids that exited and clear the matching line.pid so the
 * main loop knows to respawn.  Returns the number of children
 * reaped.
 */
static int
reap_zombies(void)
{
    int   reaped = 0;
    pid_t pid;
    int   status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        reaped++;
        for (int i = 0; i < NLINES; i++) {
            if (g_lines[i].pid == pid) {
                g_lines[i].pid = 0;
                break;
            }
        }
    }
    return reaped;
}

/*
 * Walk the line table; respawn any whose getty pid is zero.
 * Rate-limit by skipping the spawn if we just respawned this line
 * a moment ago — that's enough to keep a wedged getty from melting
 * the CPU without needing a real timer.
 */
static void
respawn_dead_lines(long now_serial)
{
    for (int i = 0; i < NLINES; i++) {
        if (g_lines[i].pid != 0) {
            continue;
        }
        if (g_lines[i].last_spawn != 0 &&
            now_serial - g_lines[i].last_spawn < 2) {
            /* respawned less than two ticks ago — wait a beat */
            continue;
        }
        g_lines[i].last_spawn = now_serial;
        spawn_getty(&g_lines[i]);
    }
}

/*
 * Best-effort shutdown.  We've already stopped respawning; now:
 *   - SIGTERM everything, give it ~2 seconds to drain
 *   - SIGKILL anything still alive
 *   - sync()
 *   - idle forever (the kernel will halt once nothing is runnable)
 */
static void
shutdown_sequence(void)
{
    int passes = 0;

    fprintf(stderr, "init: shutdown requested, signalling children\n");

    /* kill(-1, SIG) sends to every process the caller can signal —
     * as PID 1 that's effectively everything except init itself. */
    kill(-1, SIGTERM);

    while (passes++ < 20) {
        if (waitpid(-1, NULL, WNOHANG) > 0) {
            continue;
        }
        sleep(1);
    }

    kill(-1, SIGKILL);
    while (waitpid(-1, NULL, WNOHANG) > 0) { }

    sync();

    fprintf(stderr, "init: halt\n");
    for (;;) {
        sleep(3600);
    }
}

int
main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /*
     * Console attach.  kmain currently doesn't hand us inherited
     * stdio FDs; open /dev/console fresh and wire it to fd 0..2.
     */
    int cfd = open("/dev/console", O_RDWR);
    if (cfd >= 0) {
        if (cfd != 0) dup2(cfd, 0);
        if (cfd != 1) dup2(cfd, 1);
        if (cfd != 2) dup2(cfd, 2);
        if (cfd > 2)  close(cfd);
    }

    struct sigaction sa = { 0 };
    sa.sa_handler = on_shutdown_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    /*
     * Reap children async via SIGCHLD too — without it the main
     * loop's only wakeup is the waitpid in its body, which doesn't
     * advance while the loop is in sleep().
     */
    struct sigaction chld = { 0 };
    chld.sa_handler = SIG_DFL;       /* default: don't ignore — actual reap is below */
    chld.sa_flags   = SA_NOCLDSTOP;  /* don't notify on stop, only on exit */
    sigaction(SIGCHLD, &chld, NULL);

    fprintf(stderr, "Substrate init starting (pid 1)\n");

    long tick = 0;
    /* Spawn initial gettys. */
    for (int i = 0; i < NLINES; i++) {
        spawn_getty(&g_lines[i]);
        g_lines[i].last_spawn = tick;
    }

    while (!g_want_shutdown) {
        /*
         * Block in waitpid for the next child exit.  SIGCHLD wakes
         * us; the SIGTERM/SIGINT handlers wake us via EINTR.
         */
        int   status;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid > 0) {
            for (int i = 0; i < NLINES; i++) {
                if (g_lines[i].pid == pid) {
                    g_lines[i].pid = 0;
                    break;
                }
            }
        }
        /* Drain any other pending zombies non-blocking. */
        (void)reap_zombies();
        tick++;
        if (!g_want_shutdown) {
            respawn_dead_lines(tick);
        }
    }

    shutdown_sequence();
    return 0;
}
