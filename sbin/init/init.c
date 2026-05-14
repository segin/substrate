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
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

struct gettyline {
    const char *tty;        /* device path */
    pid_t       pid;        /* current getty pid (0 if not running) */
    long        last_spawn; /* serial counter used for rate-limit */
};

/*
 * /dev/console is a raw passthrough façade in the kernel — it forwards
 * writes to the active console backend but has no termios / line
 * discipline, so a getty spawned on it sees ICANON / ECHO / ISIG
 * silently no-op'd: ^C generates the literal ETX glyph and Enter never
 * cooks input into lines.  Real ttys live at /dev/tty1..tty63 and go
 * through sys/drivers/console/tty.c which has the full line discipline.
 * Run the primary getty on tty1 and reserve /dev/console for kernel
 * diagnostics where its raw write semantics are actually desirable.
 */
static struct gettyline g_lines[] = {
    { "/dev/tty1",    0, 0 },
    { "/dev/tty2",    0, 0 },
    { "/dev/tty3",    0, 0 },
};
#define NLINES ((int)(sizeof(g_lines) / sizeof(g_lines[0])))

/*
 * Shutdown intent.  g_shutdown_cmd selects what reboot() does at the
 * tail of shutdown_sequence:
 *   RB_HALT_SYSTEM  on SIGTERM / SIGQUIT (the historical Unix default —
 *                  init does the cleanup, then halts)
 *   RB_AUTOBOOT    on SIGINT (Ctrl-Alt-Del convention)
 *   RB_POWER_OFF   on SIGUSR1 (sysvinit convention: kill -USR1 1)
 * sbin/{halt,reboot,poweroff} flip the matching signal at init; users
 * can also signal init directly with the kill(1) equivalents.
 */
static volatile sig_atomic_t g_want_shutdown = 0;
static volatile sig_atomic_t g_shutdown_cmd  = RB_HALT_SYSTEM;

static void
on_shutdown_signal(int sig)
{
    g_want_shutdown = 1;
    switch (sig) {
    case SIGINT:   g_shutdown_cmd = RB_AUTOBOOT;    break;
    case SIGUSR1:  g_shutdown_cmd = RB_POWER_OFF;   break;
    case SIGUSR2:  g_shutdown_cmd = RB_AUTOBOOT;    break;
    case SIGTERM:
    case SIGQUIT:
    default:       g_shutdown_cmd = RB_HALT_SYSTEM; break;
    }
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

    const char *action = "halt";
    if (g_shutdown_cmd == RB_AUTOBOOT)       action = "reboot";
    else if (g_shutdown_cmd == RB_POWER_OFF) action = "power-off";
    fprintf(stderr, "init: %s\n", action);

    /* reboot() doesn't return on success — the kernel resets the CPU
     * before the syscall does.  If it fails (EPERM is the only
     * realistic case, and only if someone has stripped CAP_ROOT from
     * init), fall through to the idle loop so the system at least
     * doesn't reboot uncontrollably. */
    (void)reboot((int)g_shutdown_cmd);
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

    /*
     * Release tty1 as a controlling terminal.  kinit_task() calls
     * console_attach_std_fds() which stamps init's session/pgrp onto
     * the active VT's tty (i.e. tty1) at boot.  That leaves getty
     * unable to acquire tty1 as its own ctty later — TIOCSCTTY's
     * substrate check refuses to take over a tty that's already owned
     * by another session.  TIOCNOTTY clears tty1->session and ->pgrp
     * via tty_hangup(), with SIGHUP routed back to init's pgrp; ignore
     * SIGHUP so it doesn't kill us first.
     */
    struct sigaction hup_ign = { 0 };
    hup_ign.sa_handler = SIG_IGN;
    sigaction(SIGHUP, &hup_ign, NULL);
    (void)ioctl(0, TIOCNOTTY, 0);

    struct sigaction sa = { 0 };
    sa.sa_handler = on_shutdown_signal;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);   /* halt / poweroff */
    sigaction(SIGUSR2, &sa, NULL);   /* reboot */

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
