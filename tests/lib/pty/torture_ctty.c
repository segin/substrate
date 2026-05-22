/*
 * torture_ctty.c — PTY session / controlling-terminal torture test.
 *
 * The existing torture_pty.c exercises the data path (open, read,
 * write, winsize, EOF).  It does NOT cover what an interactive
 * terminal emulator actually does once the window is up: the
 * parent/child handshake over pipes, setsid(), controlling-terminal
 * acquisition, /dev/tty routing, job-control process groups and the
 * job-control signals.  xterm hangs after drawing its UI and before
 * the shell's PTY produces output — somewhere in exactly that path.
 *
 * Each scenario runs inside a forked "runner" child guarded by an
 * alarm() timeout, so a deadlock is reported as HANG against the
 * precise scenario instead of wedging the whole test.
 *
 * Builds against host libc by default; cross-builds for substrate
 * with CROSS=PREFIX (see the Makefile).
 *
 * Scenarios:
 *   1. pipe_handshake   — xterm-shaped ~1KB struct exchange over two
 *                         pipes, both directions, no deadlock.
 *   2. setsid_basic     — setsid() makes the caller a session+pgrp leader.
 *   3. ctty_write       — setsid + open(slave) acquires the slave as
 *                         the controlling terminal; write to /dev/tty
 *                         reaches the master.
 *   4. ctty_read        — read of /dev/tty returns data the master sent.
 *   5. tcpgrp_roundtrip — tcsetpgrp / tcgetpgrp on the slave agree.
 *   6. login_tty_exec   — setsid + TIOCSCTTY + dup2(slave->0,1,2);
 *                         output on fd 1 reaches the master.
 *   7. job_ctrl_sigint  — master writes ^C; SIGINT reaches the slave's
 *                         foreground process group.
 *   8. winch_signal     — TIOCSWINSZ on the master raises SIGWINCH in
 *                         the slave's foreground process group.
 *   9. hangup_eof       — closing the master hangs the slave up: a
 *                         blocked slave read returns 0 (EOF).
 *  10. select_master    — select() on the master wakes when the slave
 *                         side writes.
 *  11. exec_on_pty      — fork + login_tty + exec a real program on the
 *                         slave; its stdout reaches the master.
 *  12. interactive_sh   — fork + login_tty + exec a login shell on the
 *                         slave; a command typed at the master is run
 *                         and its output read back.  This is exactly
 *                         what an xterm does once its window is up.
 *  13. master_nonblock — FIONBIO on the master: a read with no data
 *                         pending returns EAGAIN instead of blocking
 *                         (xterm runs its master fd non-blocking).
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

extern int   posix_openpt(int flags);
extern int   grantpt(int fd);
extern int   unlockpt(int fd);
extern char *ptsname(int fd);

static int tests_run = 0, tests_pass = 0, tests_fail = 0;
static int tests_skip = 0, tests_hung = 0;
static const int TOTAL = 13;

/* Scenario result codes (also used as child _exit codes). */
#define R_PASS 0
#define R_SKIP 1
#define R_FAIL 2

/* ------------------------------------------------------------------ */
/* Timeout harness: run a scenario in a child, alarm() the wait.       */

static volatile sig_atomic_t alarm_fired = 0;
static void on_alarm(int s) { (void)s; alarm_fired = 1; }

static void reap_strays(void)
{
	int st;
	while (waitpid(-1, &st, WNOHANG) > 0)
		;
}

static int run_one(const char *name, int (*fn)(void), int secs)
{
	fprintf(stdout, "[%2d/%2d] %-18s ", ++tests_run, TOTAL, name);
	fflush(stdout);

	pid_t pid = fork();
	if (pid < 0) {
		fprintf(stdout, "FAIL (fork: %s)\n", strerror(errno));
		tests_fail++;
		return R_FAIL;
	}
	if (pid == 0) {
		alarm(0);
		_exit(fn());
	}

	alarm_fired = 0;
	alarm((unsigned)secs);
	int st;
	pid_t w = waitpid(pid, &st, 0);
	alarm(0);

	if (w < 0 && alarm_fired) {
		kill(pid, SIGKILL);
		waitpid(pid, &st, 0);
		reap_strays();
		fprintf(stdout, "HANG (no result in %ds)\n", secs);
		tests_hung++;
		return R_FAIL;
	}
	reap_strays();

	if (WIFSIGNALED(st)) {
		fprintf(stdout, "FAIL (killed by signal %d)\n", WTERMSIG(st));
		tests_fail++;
		return R_FAIL;
	}
	int rc = WIFEXITED(st) ? WEXITSTATUS(st) : R_FAIL;
	if (rc == R_PASS)      { fprintf(stdout, "PASS\n"); tests_pass++; }
	else if (rc == R_SKIP) { fprintf(stdout, "SKIP\n"); tests_skip++; }
	else                   { fprintf(stdout, "FAIL\n"); tests_fail++; }
	return rc;
}

/* ------------------------------------------------------------------ */
/* Helpers.                                                            */

/* posix_openpt + grant + unlock; returns master fd and the slave
 * path (into `slave`, size >= 64).  -1 on failure. */
static int make_pty(char *slave, size_t slen)
{
	int m = posix_openpt(O_RDWR);
	if (m < 0)
		return -1;
	if (grantpt(m) != 0 || unlockpt(m) != 0) {
		close(m);
		return -1;
	}
	char *n = ptsname(m);
	if (!n) {
		close(m);
		return -1;
	}
	strncpy(slave, n, slen - 1);
	slave[slen - 1] = '\0';
	return m;
}

/* ------------------------------------------------------------------ */

/* 1. xterm's parent/child handshake: a ~1KB struct over two pipes. */
struct hs { int status; int error; int rows; int cols; char buf[1024]; };

static int test_pipe_handshake(void)
{
	int cp[2], pc[2];		/* child->parent, parent->child */
	if (pipe(cp) != 0 || pipe(pc) != 0)
		return R_FAIL;

	pid_t pid = fork();
	if (pid < 0)
		return R_FAIL;

	if (pid == 0) {
		struct hs m;
		close(cp[0]);
		close(pc[1]);
		memset(&m, 0, sizeof m);
		m.status = 42;
		strcpy(m.buf, "child-to-parent");
		if (write(cp[1], &m, sizeof m) != (ssize_t)sizeof m)
			_exit(R_FAIL);
		/* Wait for the parent's reply (xterm wait_for_map path). */
		if (read(pc[0], &m, sizeof m) != (ssize_t)sizeof m)
			_exit(R_FAIL);
		_exit(m.status == 99 ? R_PASS : R_FAIL);
	}

	struct hs m;
	close(cp[1]);
	close(pc[0]);
	if (read(cp[0], &m, sizeof m) != (ssize_t)sizeof m)
		return R_FAIL;
	if (m.status != 42 || strcmp(m.buf, "child-to-parent") != 0)
		return R_FAIL;
	memset(&m, 0, sizeof m);
	m.status = 99;
	if (write(pc[1], &m, sizeof m) != (ssize_t)sizeof m)
		return R_FAIL;
	close(cp[0]);
	close(pc[1]);

	int st;
	waitpid(pid, &st, 0);
	return (WIFEXITED(st) && WEXITSTATUS(st) == R_PASS) ? R_PASS : R_FAIL;
}

/* 2. setsid() makes the caller its own session and process group. */
static int test_setsid_basic(void)
{
	pid_t pid = fork();
	if (pid < 0)
		return R_FAIL;
	if (pid == 0) {
		pid_t sid = setsid();
		if (sid < 0)
			_exit(R_FAIL);
		_exit((getsid(0) == getpid() && getpgrp() == getpid())
		      ? R_PASS : R_FAIL);
	}
	int st;
	waitpid(pid, &st, 0);
	return (WIFEXITED(st) && WEXITSTATUS(st) == R_PASS) ? R_PASS : R_FAIL;
}

/* 3. After setsid + open(slave), /dev/tty must route to the slave:
 *    a write to /dev/tty has to surface on the master. */
static int test_ctty_write(void)
{
	char slave[64];
	int m = make_pty(slave, sizeof slave);
	if (m < 0)
		return R_FAIL;

	pid_t pid = fork();
	if (pid < 0) { close(m); return R_FAIL; }
	if (pid == 0) {
		close(m);
		setsid();
		int s = open(slave, O_RDWR);	/* no O_NOCTTY: claim ctty */
		if (s < 0)
			_exit(R_FAIL);
		int t = open("/dev/tty", O_RDWR);
		if (t < 0)
			_exit(R_FAIL);
		ssize_t w = write(t, "PING", 4);
		close(t);
		close(s);
		_exit(w == 4 ? R_PASS : R_FAIL);
	}

	char buf[16] = { 0 };
	ssize_t n = read(m, buf, 4);
	int st;
	waitpid(pid, &st, 0);
	close(m);
	return (n == 4 && memcmp(buf, "PING", 4) == 0) ? R_PASS : R_FAIL;
}

/* 4. The read direction: a read of /dev/tty returns what the master
 *    wrote into the slave. */
static int test_ctty_read(void)
{
	char slave[64];
	int m = make_pty(slave, sizeof slave);
	if (m < 0)
		return R_FAIL;

	int sync[2];
	if (pipe(sync) != 0) { close(m); return R_FAIL; }

	pid_t pid = fork();
	if (pid < 0) { close(m); return R_FAIL; }
	if (pid == 0) {
		close(m);
		setsid();
		int s = open(slave, O_RDWR);
		if (s < 0)
			_exit(R_FAIL);
		/* raw mode so the bytes come straight through */
		struct termios t;
		if (tcgetattr(s, &t) == 0) {
			t.c_lflag &= (unsigned)~(ICANON | ECHO);
			tcsetattr(s, TCSANOW, &t);
		}
		char go = 1;
		write(sync[1], &go, 1);		/* tell parent we're ready */
		int d = open("/dev/tty", O_RDWR);
		if (d < 0)
			_exit(R_FAIL);
		char buf[16] = { 0 };
		ssize_t n = read(d, buf, 4);
		close(d);
		close(s);
		_exit((n == 4 && memcmp(buf, "DATA", 4) == 0) ? R_PASS : R_FAIL);
	}

	char go;
	read(sync[0], &go, 1);
	usleep(100000);
	write(m, "DATA", 4);
	int st;
	waitpid(pid, &st, 0);
	close(m);
	close(sync[0]);
	close(sync[1]);
	return (WIFEXITED(st) && WEXITSTATUS(st) == R_PASS) ? R_PASS : R_FAIL;
}

/* 5. tcsetpgrp / tcgetpgrp round-trip on the slave. */
static int test_tcpgrp_roundtrip(void)
{
	char slave[64];
	int m = make_pty(slave, sizeof slave);
	if (m < 0)
		return R_FAIL;

	pid_t pid = fork();
	if (pid < 0) { close(m); return R_FAIL; }
	if (pid == 0) {
		close(m);
		setsid();
		int s = open(slave, O_RDWR);
		if (s < 0)
			_exit(R_FAIL);
		pid_t want = getpid();
		if (tcsetpgrp(s, want) != 0)
			_exit(R_FAIL);
		pid_t got = tcgetpgrp(s);
		close(s);
		_exit(got == want ? R_PASS : R_FAIL);
	}
	int st;
	waitpid(pid, &st, 0);
	close(m);
	return (WIFEXITED(st) && WEXITSTATUS(st) == R_PASS) ? R_PASS : R_FAIL;
}

/* 6. The login_tty pattern: setsid + TIOCSCTTY + dup2(slave->0,1,2).
 *    Output written to fd 1 must reach the master. */
static int test_login_tty_exec(void)
{
	char slave[64];
	int m = make_pty(slave, sizeof slave);
	if (m < 0)
		return R_FAIL;

	pid_t pid = fork();
	if (pid < 0) { close(m); return R_FAIL; }
	if (pid == 0) {
		close(m);
		setsid();
		int s = open(slave, O_RDWR);
		if (s < 0)
			_exit(R_FAIL);
		ioctl(s, TIOCSCTTY, 0);
		struct termios t;
		if (tcgetattr(s, &t) == 0) {
			t.c_lflag &= (unsigned)~(ICANON | ECHO);
			tcsetattr(s, TCSANOW, &t);
		}
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (s > 2)
			close(s);
		ssize_t w = write(1, "EXEC-OK", 7);
		_exit(w == 7 ? R_PASS : R_FAIL);
	}

	char buf[16] = { 0 };
	ssize_t n = read(m, buf, 7);
	int st;
	waitpid(pid, &st, 0);
	close(m);
	return (n == 7 && memcmp(buf, "EXEC-OK", 7) == 0) ? R_PASS : R_FAIL;
}

/* 7. ^C on the master raises SIGINT in the slave's foreground pgrp. */
static volatile sig_atomic_t got_sigint = 0;
static void on_sigint(int s) { (void)s; got_sigint = 1; }

static int test_job_ctrl_sigint(void)
{
	char slave[64];
	int m = make_pty(slave, sizeof slave);
	if (m < 0)
		return R_FAIL;

	int sync[2];
	if (pipe(sync) != 0) { close(m); return R_FAIL; }

	pid_t pid = fork();
	if (pid < 0) { close(m); return R_FAIL; }
	if (pid == 0) {
		close(m);
		setsid();
		int s = open(slave, O_RDWR);
		if (s < 0)
			_exit(R_FAIL);
		ioctl(s, TIOCSCTTY, 0);
		struct termios t;
		if (tcgetattr(s, &t) == 0) {
			t.c_lflag |= ISIG;
			t.c_cc[VINTR] = 3;	/* ^C */
			tcsetattr(s, TCSANOW, &t);
		}
		signal(SIGINT, on_sigint);
		char go = 1;
		write(sync[1], &go, 1);
		char b;
		read(s, &b, 1);			/* blocks; ^C interrupts it */
		_exit(got_sigint ? R_PASS : R_FAIL);
	}

	char go;
	read(sync[0], &go, 1);
	usleep(150000);
	char ctrl_c = 3;
	write(m, &ctrl_c, 1);
	int st;
	waitpid(pid, &st, 0);
	close(m);
	close(sync[0]);
	close(sync[1]);
	return (WIFEXITED(st) && WEXITSTATUS(st) == R_PASS) ? R_PASS : R_FAIL;
}

/* 8. TIOCSWINSZ on the master raises SIGWINCH in the slave's pgrp. */
static volatile sig_atomic_t got_winch = 0;
static void on_winch(int s) { (void)s; got_winch = 1; }

static int test_winch_signal(void)
{
	char slave[64];
	int m = make_pty(slave, sizeof slave);
	if (m < 0)
		return R_FAIL;

	int sync[2];
	if (pipe(sync) != 0) { close(m); return R_FAIL; }

	pid_t pid = fork();
	if (pid < 0) { close(m); return R_FAIL; }
	if (pid == 0) {
		close(m);
		setsid();
		int s = open(slave, O_RDWR);
		if (s < 0)
			_exit(R_FAIL);
		ioctl(s, TIOCSCTTY, 0);
		signal(SIGWINCH, on_winch);
		char go = 1;
		write(sync[1], &go, 1);
		for (int i = 0; i < 50 && !got_winch; i++)
			usleep(50000);
		_exit(got_winch ? R_PASS : R_FAIL);
	}

	char go;
	read(sync[0], &go, 1);
	usleep(150000);
	struct winsize ws = { 40, 100, 0, 0 };
	ioctl(m, TIOCSWINSZ, &ws);
	int st;
	waitpid(pid, &st, 0);
	close(m);
	close(sync[0]);
	close(sync[1]);
	return (WIFEXITED(st) && WEXITSTATUS(st) == R_PASS) ? R_PASS : R_FAIL;
}

/* 9. Closing the master hangs the slave up: a blocked slave read
 *    returns 0 (EOF). */
static int test_hangup_eof(void)
{
	char slave[64];
	int m = make_pty(slave, sizeof slave);
	if (m < 0)
		return R_FAIL;

	int sync[2];
	if (pipe(sync) != 0) { close(m); return R_FAIL; }

	pid_t pid = fork();
	if (pid < 0) { close(m); return R_FAIL; }
	if (pid == 0) {
		close(m);
		setsid();
		signal(SIGHUP, SIG_IGN);	/* survive to observe EOF */
		int s = open(slave, O_RDWR);
		if (s < 0)
			_exit(R_FAIL);
		char go = 1;
		write(sync[1], &go, 1);
		char b;
		ssize_t n = read(s, &b, 1);	/* master close -> EOF */
		close(s);
		_exit(n == 0 ? R_PASS : R_FAIL);
	}

	char go;
	read(sync[0], &go, 1);
	usleep(150000);
	close(m);
	int st;
	waitpid(pid, &st, 0);
	close(sync[0]);
	close(sync[1]);
	return (WIFEXITED(st) && WEXITSTATUS(st) == R_PASS) ? R_PASS : R_FAIL;
}

/* 10. select() on the master wakes when the slave writes. */
static int test_select_master(void)
{
	char slave[64];
	int m = make_pty(slave, sizeof slave);
	if (m < 0)
		return R_FAIL;

	pid_t pid = fork();
	if (pid < 0) { close(m); return R_FAIL; }
	if (pid == 0) {
		close(m);
		int s = open(slave, O_RDWR);
		if (s < 0)
			_exit(R_FAIL);
		struct termios t;
		if (tcgetattr(s, &t) == 0) {
			t.c_lflag &= (unsigned)~(ICANON | ECHO);
			tcsetattr(s, TCSANOW, &t);
		}
		usleep(150000);
		write(s, "WAKE", 4);
		close(s);
		_exit(R_PASS);
	}

	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(m, &rfds);
	struct timeval tv = { 5, 0 };
	int r = select(m + 1, &rfds, NULL, NULL, &tv);
	int ok = 0;
	if (r > 0 && FD_ISSET(m, &rfds)) {
		char buf[16] = { 0 };
		ssize_t n = read(m, buf, 4);
		ok = (n == 4 && memcmp(buf, "WAKE", 4) == 0);
	}
	int st;
	waitpid(pid, &st, 0);
	close(m);
	return ok ? R_PASS : R_FAIL;
}

/* Read from fd until `needle` is seen.  Returns 1 if found, 0 if the
 * stream ends first.  A genuine hang is caught by the alarm timeout. */
static int read_until(int fd, const char *needle)
{
	char acc[2048];
	size_t len = 0;
	for (int chunk = 0; chunk < 64 && len < sizeof acc - 1; chunk++) {
		ssize_t n = read(fd, acc + len, sizeof acc - 1 - len);
		if (n <= 0)
			break;
		len += (size_t)n;
		acc[len] = '\0';
		if (strstr(acc, needle))
			return 1;
	}
	return 0;
}

/* 11. Exec a real program on the slave; its stdout reaches the master. */
static int test_exec_on_pty(void)
{
	char slave[64];
	int m = make_pty(slave, sizeof slave);
	if (m < 0)
		return R_FAIL;

	pid_t pid = fork();
	if (pid < 0) { close(m); return R_FAIL; }
	if (pid == 0) {
		close(m);
		setsid();
		int s = open(slave, O_RDWR);
		if (s < 0)
			_exit(R_FAIL);
		ioctl(s, TIOCSCTTY, 0);
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (s > 2)
			close(s);
		execl("/bin/echo", "echo", "TORTURE_OK", (char *)NULL);
		_exit(R_FAIL);			/* exec failed */
	}

	int found = read_until(m, "TORTURE_OK");
	int st;
	waitpid(pid, &st, 0);
	close(m);
	return found ? R_PASS : R_FAIL;
}

/* 12. Exec a login shell on the slave and run a command through it —
 *     exactly what xterm does once its window is mapped.  The needle
 *     "RES=42" can only appear if the shell actually evaluated the
 *     line (it is not present in the echoed input). */
static int test_interactive_sh(void)
{
	char slave[64];
	int m = make_pty(slave, sizeof slave);
	if (m < 0)
		return R_FAIL;

	pid_t pid = fork();
	if (pid < 0) { close(m); return R_FAIL; }
	if (pid == 0) {
		close(m);
		setsid();
		int s = open(slave, O_RDWR);
		if (s < 0)
			_exit(R_FAIL);
		ioctl(s, TIOCSCTTY, 0);
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (s > 2)
			close(s);
		execl("/bin/sh", "-sh", (char *)NULL);	/* login shell */
		_exit(R_FAIL);
	}

	usleep(800000);				/* let the shell come up */
	const char *cmd = "echo RES=$((6*7))\n";
	write(m, cmd, strlen(cmd));
	int found = read_until(m, "RES=42");

	write(m, "exit\n", 5);
	int st;
	for (int i = 0; i < 20; i++) {
		if (waitpid(pid, &st, WNOHANG) == pid)
			break;
		usleep(100000);
	}
	kill(pid, SIGKILL);
	waitpid(pid, &st, WNOHANG);
	close(m);
	return found ? R_PASS : R_FAIL;
}

/* 13. A non-blocking master must return EAGAIN on an empty read, not
 *     block.  xterm sets its pty master non-blocking with FIONBIO. */
static int test_master_nonblock(void)
{
	char slave[64];
	int m = make_pty(slave, sizeof slave);
	if (m < 0)
		return R_FAIL;

	int on = 1;
	if (ioctl(m, FIONBIO, &on) != 0) {
		close(m);
		return R_FAIL;
	}
	/* Nothing has been written to the slave — this read must not
	 * block.  On a PTY that ignores O_NONBLOCK it hangs here and
	 * the scenario is reported as HANG by the alarm. */
	char buf[8];
	errno = 0;
	ssize_t n = read(m, buf, sizeof buf);
	int ok = (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
	close(m);
	return ok ? R_PASS : R_FAIL;
}

/* ------------------------------------------------------------------ */

int main(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_alarm;		/* no SA_RESTART: waitpid -> EINTR */
	sigaction(SIGALRM, &sa, NULL);

	fprintf(stdout, "torture_ctty: PTY session / controlling-terminal "
	    "torture test\n");
	fflush(stdout);

	run_one("pipe_handshake",   test_pipe_handshake,   8);
	run_one("setsid_basic",     test_setsid_basic,     8);
	run_one("ctty_write",       test_ctty_write,       8);
	run_one("ctty_read",        test_ctty_read,        8);
	run_one("tcpgrp_roundtrip", test_tcpgrp_roundtrip, 8);
	run_one("login_tty_exec",   test_login_tty_exec,   8);
	run_one("job_ctrl_sigint",  test_job_ctrl_sigint,  8);
	run_one("winch_signal",     test_winch_signal,     8);
	run_one("hangup_eof",       test_hangup_eof,       8);
	run_one("select_master",    test_select_master,    8);
	run_one("exec_on_pty",      test_exec_on_pty,      12);
	run_one("interactive_sh",   test_interactive_sh,   18);
	run_one("master_nonblock",  test_master_nonblock,  8);

	fprintf(stdout, "\n%d run, %d passed, %d failed, %d hung, %d skipped\n",
	    tests_run, tests_pass, tests_fail, tests_hung, tests_skip);
	fprintf(stdout, "Result: %s\n",
	    (tests_fail == 0 && tests_hung == 0) ? "PASSED" : "FAILED");
	fflush(stdout);
	return (tests_fail == 0 && tests_hung == 0) ? 0 : 1;
}
