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
 *      stop respawning, kill the interactive user login sessions,
 *      run /etc/rc stop, then signal every remaining process, give
 *      them a grace period, SIGKILL the stragglers, sync(), and halt.
 *
 * The terminal table is statically configured.  A proper
 * /etc/inittab parser is a later step — the current rootfs only
 * needs three gettys to come up.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

struct gettyline {
    const char *tty;        /* device path */
    pid_t       pid;        /* current getty pid (0 if not running) */
    time_t      last_spawn; /* wall-clock seconds of last spawn */
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
 * Walk the line table; respawn any whose getty pid is zero.  Rate-
 * limit using wall-clock seconds, not a loop counter — the main
 * loop only ticks when a child exits, so a fast-failing getty whose
 * loop-counter delta is small would stay un-respawned forever once
 * its sibling lines settled and waitpid started blocking.  Returns
 * 1 if any line is still rate-limited (caller should poll again
 * soon), 0 if every dead line was respawned.
 */
static int
respawn_dead_lines(time_t now)
{
    int rate_limited = 0;
    for (int i = 0; i < NLINES; i++) {
        if (g_lines[i].pid != 0) {
            continue;
        }
        if (g_lines[i].last_spawn != 0 &&
            now - g_lines[i].last_spawn < 1) {
            /* spawned this same second — wait at least one second. */
            rate_limited = 1;
            continue;
        }
        g_lines[i].last_spawn = now;
        spawn_getty(&g_lines[i]);
    }
    return rate_limited;
}

/*
 * Tear down the interactive login sessions init manages before the rc.d
 * services are stopped.  Each getty setsid()'d, so its pid is the session /
 * process-group id and the login + shell (+ anything they started, e.g. an X
 * server via startx) live in that session.  Hang it up, terminate it, give a
 * short grace, then SIGKILL whatever ignored the signals — a wedged session
 * (a cooked X server, a shell stuck in an uninterruptible read) must not be
 * left alive to fight the orderly `/etc/rc stop` that runs next.
 */
static void
terminate_login_sessions(void)
{
    int any = 0;

    for (int i = 0; i < NLINES; i++) {
        pid_t pid = g_lines[i].pid;
        if (pid == 0) {
            continue;
        }
        /* Signal the whole process group (negative pid) and the session
         * leader itself; the leader's death also makes the kernel SIGHUP
         * the controlling terminal's foreground group. */
        kill(-pid, SIGHUP);
        kill(-pid, SIGTERM);
        kill(pid, SIGHUP);
        kill(pid, SIGTERM);
        any = 1;
    }
    if (!any) {
        return;
    }

    fprintf(stderr, "init: terminating user login sessions\n");

    /* Up to ~3 s of grace, exiting early once every managed session leader
     * is gone. */
    for (int waited = 0; waited < 3; waited++) {
        int alive = 0;
        while (waitpid(-1, NULL, WNOHANG) > 0) { }
        for (int i = 0; i < NLINES; i++) {
            if (g_lines[i].pid != 0 && kill(g_lines[i].pid, 0) == 0) {
                alive = 1;
            }
        }
        if (!alive) {
            break;
        }
        sleep(1);
    }

    /* Force-kill any session that ignored the hangup so it can't hold the
     * console / a device busy while the stop scripts run. */
    for (int i = 0; i < NLINES; i++) {
        pid_t pid = g_lines[i].pid;
        if (pid != 0) {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            g_lines[i].pid = 0;
        }
    }
    while (waitpid(-1, NULL, WNOHANG) > 0) { }
}

/*
 * Best-effort shutdown.  We've already stopped respawning; now:
 *   - kill the interactive user login sessions
 *   - run /etc/rc stop to halt services in reverse start order
 *   - SIGHUP/SIGTERM everything still alive, give it a grace period
 *   - SIGKILL the stragglers
 *   - sync()
 *   - reboot()/halt (or idle forever if that somehow returns)
 */
static void
shutdown_sequence(void)
{
    fprintf(stderr, "init: shutdown requested\n");

    /* Kill user login sessions FIRST, before stopping services: a live
     * (possibly wedged) login or X session must not be left racing the
     * rc.d stop scripts for the console and devices. */
    terminate_login_sessions();

    /* Stop subsystems in reverse start order before tearing processes
     * down: run `/etc/rc stop`, which walks the /etc/rc.d scripts
     * backwards handing each an orderly `stop`.  Bounded wait so a
     * wedged stop script can't stall the power-down — the blanket
     * kill below mops up whatever the stop scripts left behind. */
    fprintf(stderr, "init: stopping services (/etc/rc stop)\n");
    {
        pid_t rc_pid = fork();
        if (rc_pid == 0) {
            execl("/bin/sh", "sh", "/etc/rc", "stop", (char *)NULL);
            _exit(127);
        }
        if (rc_pid > 0) {
            for (int waited = 0; waited < 15; waited++) {
                if (waitpid(rc_pid, NULL, WNOHANG) == rc_pid)
                    break;
                sleep(1);
            }
        }
    }

    fprintf(stderr, "init: signalling remaining processes\n");

    /* kill(-1, SIG) sends to every process the caller can signal —
     * as PID 1 that's effectively everything except init itself.
     * SIGHUP first so login shells / interactive readers see a hangup
     * and unwind any blocking read(); SIGTERM next for everything that
     * ignored the hangup. */
    kill(-1, SIGHUP);
    kill(-1, SIGTERM);

    /* Drain children with an early exit once all are reaped.  Up to
     * 5 seconds of grace before we escalate to SIGKILL.  This used
     * to be a fixed 20-second sleep that fired even when every child
     * had already exited, so `poweroff` took 20 s to take effect even
     * after the user closed their shell. */
    for (int passes = 0; passes < 5; passes++) {
        /* Drain everything ready right now. */
        while (waitpid(-1, NULL, WNOHANG) > 0) { }
        /* No remaining children?  We're done draining. */
        if (waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD) {
            break;
        }
        sleep(1);
    }

    kill(-1, SIGKILL);
    while (waitpid(-1, NULL, WNOHANG) > 0) { }

    /* Stamp wtmp with a shutdown record so `last` shows the
     * symmetric reboot/shutdown bracket. */
    {
        struct utmp ut;
        memset(&ut, 0, sizeof(ut));
        ut.ut_type = RUN_LVL;
        strlcpy(ut.ut_line, "~", sizeof(ut.ut_line));
        strlcpy(ut.ut_user, "shutdown", sizeof(ut.ut_user));
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ut.ut_tv.tv_sec  = (int32_t)tv.tv_sec;
        ut.ut_tv.tv_usec = (int32_t)tv.tv_usec;
        updwtmp(WTMP_FILE, &ut);
    }

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

/*
 * Record a logout.  When a getty/login/shell session on `tty` ends,
 * write a DEAD_PROCESS record: utmp then shows the line free and
 * wtmp carries the logout so `last` can pair it with the login.
 *
 * init is what does this — login(1) exec()s the user's shell and is
 * gone, so it cannot fork+wait to write the close itself (and an
 * intervening fork would move tty ownership to the wrong process).
 * init, as the session's reaper, is the natural place.
 */
static void
record_logout(const char *tty, pid_t pid)
{
    const char *base = strrchr(tty, '/');
    base = base ? base + 1 : tty;

    struct utmp ut;
    memset(&ut, 0, sizeof(ut));
    ut.ut_type = DEAD_PROCESS;
    ut.ut_pid  = pid;
    strlcpy(ut.ut_line, base, sizeof(ut.ut_line));
    /* ut_id must match login's USER_PROCESS record so pututline()
     * rewrites the same utmp slot rather than appending a new one. */
    size_t lnlen = strlen(base);
    const char *idsrc = lnlen > 4 ? base + lnlen - 4 : base;
    strncpy(ut.ut_id, idsrc, sizeof(ut.ut_id));
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ut.ut_tv.tv_sec  = (int32_t)tv.tv_sec;
    ut.ut_tv.tv_usec = (int32_t)tv.tv_usec;

    setutent();
    pututline(&ut);
    endutent();
    updwtmp(WTMP_FILE, &ut);
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

    /* Make sure the spool dirs exist before any login activity tries
     * to open them.  Idempotent — mkdir EEXIST is fine. */
    (void)mkdir("/var", 0755);
    (void)mkdir("/var/run", 0755);
    (void)mkdir("/var/log", 0755);

    /* Stamp wtmp with a BOOT_TIME record so `last reboot` works. */
    {
        struct utmp ut;
        memset(&ut, 0, sizeof(ut));
        ut.ut_type = BOOT_TIME;
        ut.ut_pid  = 0;
        strlcpy(ut.ut_line, "~", sizeof(ut.ut_line));
        strlcpy(ut.ut_user, "reboot", sizeof(ut.ut_user));
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ut.ut_tv.tv_sec  = (int32_t)tv.tv_sec;
        ut.ut_tv.tv_usec = (int32_t)tv.tv_usec;
        updwtmp(WTMP_FILE, &ut);
        /* Also reset utmp by truncating + writing a BOOT_TIME entry —
         * this clears stale USER_PROCESS rows from the previous boot. */
        int fd = open(UTMP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            (void)write(fd, &ut, sizeof(ut));
            close(fd);
        }
    }

    /*
     * Run BSD-style /etc/rc once at boot to bring up services.
     * Block here until rc.d is fully done — getty spawn must not
     * race the rc.d run.  waitpid() is wrapped in an EINTR-restart
     * loop because SIGCHLD fires for every short-lived helper that
     * /etc/rc forks (cat, kill -0, $(...) substitutions, ...), and
     * substrate's kernel surfaces those as -EINTR on a blocking
     * wait even with default SIGCHLD disposition.  Without the
     * restart, the first sub-helper to exit kicks init out of
     * waitpid early and getty spawns alongside an unfinished rc.d.
     */
    {
        pid_t rc_pid = fork();
        if (rc_pid == 0) {
            execl("/bin/sh", "sh", "/etc/rc", "start", (char *)NULL);
            _exit(127);
        } else if (rc_pid > 0) {
            int status;
            for (;;) {
                pid_t w = waitpid(rc_pid, &status, 0);
                if (w == rc_pid) break;
                if (w < 0 && errno == EINTR) continue;
                break;
            }
        }
    }

    /* Spawn initial gettys. */
    {
        time_t now = time(NULL);
        for (int i = 0; i < NLINES; i++) {
            spawn_getty(&g_lines[i]);
            g_lines[i].last_spawn = now;
        }
    }

    while (!g_want_shutdown) {
        /*
         * Reap exited children non-blocking, then sleep ~1s before
         * re-checking.  We can't block in waitpid(2) forever because
         * a fast-failing getty's last_spawn is "now", and the
         * 1-second rate-limit in respawn_dead_lines would otherwise
         * never resolve — there'd be no child exit to wake us, and
         * no advance of wall-clock visibility in our control flow.
         *
         * sleep(1) yields the rate-limit window and is interrupted
         * by SIGCHLD / SIGTERM, so child exits and shutdown signals
         * still feel snappy.
         */
        for (;;) {
            int   status;
            pid_t pid = waitpid(-1, &status, WNOHANG);
            if (pid <= 0) break;
            for (int i = 0; i < NLINES; i++) {
                if (g_lines[i].pid == pid) {
                    /* The session on this line ended — close the
                     * utmp/wtmp pair before respawning getty. */
                    record_logout(g_lines[i].tty, pid);
                    g_lines[i].pid = 0;
                    break;
                }
            }
        }
        if (g_want_shutdown) break;
        time_t now = time(NULL);
        (void)respawn_dead_lines(now);
        sleep(1);
    }

    shutdown_sequence();
    return 0;
}
