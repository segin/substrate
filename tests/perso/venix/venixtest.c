/*
 * venixtest.c - the system calls of the Venix/86 personality.
 *
 * Compiled BY Venix, under substrate, with the C compiler of the system
 * itself -- there is no other for it -- so it is in the C of 1985.  See
 * README.md for how it is run.
 *
 *     cc -o venixtest venixtest.c && ./venixtest one two
 *
 * Prints a line per check and "venixtest: PASS" or "FAIL"; exits non-zero
 * if anything failed.  It is tests/perso/pcix/pcixtest.c with what is
 * Venix's put in: the calls arrive by another road (int 0xf1, arguments
 * in registers, the error number in CX) and are run by the same code, so
 * the same things want checking, and then brk, which here is the
 * kernel's, the file types of st_mode, floating point through the
 * emulator hook, and the six-byte sgttyb behind isatty.
 */
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

extern int errno;
extern long lseek();
extern long time();

int fails;
int caught;
int count;

check(what, got, ok)
char *what;
long got;
int ok;
{
	printf("  %s  %s = %ld\n", ok ? "ok  " : "FAIL", what, got);
	if (!ok)
		fails++;
}

catch(sig)
int sig;
{
	caught = sig;
	count++;
}

main(argc, argv, envp)
int argc;
char **argv, **envp;
{
	struct stat st;
	char buf[64];
	double x;
	int p0;
	extern char *sbrk();
	long t, off;
	int fd, pid, status, p[2], n, (*old)();
	register int r1, r2;

	setbuf(stdout, (char *)0);

	check("argc", (long)argc, argc == 3);
	check("argv[1]", (long)argv[1][0], strcmp(argv[1], "one") == 0);
	check("argv[2], and the vector ends", (long)argv[2][0],
	    strcmp(argv[2], "two") == 0 && argv[3] == 0);
	check("an environment", (long)(envp != 0), envp != 0);

	/* --- two results, the second in DX ---------------------------- */
	pid = getpid();
	check("getpid", (long)pid, pid > 0);
	check("getuid and geteuid agree", (long)getuid(),
	    getuid() == geteuid());
	check("getgid and getegid agree", (long)getgid(),
	    getgid() == getegid());
	t = time((long *)0);
	check("time, a long in DX:AX", t, t > 400000000L);

	/* --- files ----------------------------------------------------- */
	unlink("venixtest.tmp");
	fd = creat("venixtest.tmp", 0644);
	check("creat", (long)fd, fd >= 0);
	n = write(fd, "0123456789", 10);
	check("write", (long)n, n == 10);
	close(fd);
	n = stat("venixtest.tmp", &st);
	check("stat", (long)n, n == 0);
	check("  st_size", (long)st.st_size, st.st_size == 10L);
	check("  st_mode", (long)st.st_mode,
	    (st.st_mode & S_IFMT) == S_IFREG && (st.st_mode & 0777) == 0644);
	check("  st_nlink", (long)st.st_nlink, st.st_nlink == 1);
	fd = open("venixtest.tmp", 2);
	off = lseek(fd, -3L, 2);
	check("lseek from the end, a long both ways", off, off == 7L);
	n = read(fd, buf, 8);
	check("read to end of file", (long)n, n == 3 && buf[0] == '7');
	n = fstat(fd, &st);
	check("fstat", (long)st.st_size, n == 0 && st.st_size == 10L);
	close(fd);
	n = chmod("venixtest.tmp", 0600);
	stat("venixtest.tmp", &st);
	check("chmod", (long)(st.st_mode & 0777),
	    n == 0 && (st.st_mode & 0777) == 0600);
	n = link("venixtest.tmp", "venixtest.lnk");
	stat("venixtest.tmp", &st);
	check("link", (long)st.st_nlink, n == 0 && st.st_nlink == 2);
	unlink("venixtest.lnk");
	n = access("venixtest.tmp", 4);
	check("access", (long)n, n == 0);
	unlink("venixtest.tmp");
	errno = 0;
	n = open("venixtest.tmp", 0);
	check("open of nothing: carry, and ENOENT", (long)errno,
	    n == -1 && errno == ENOENT);
	errno = 0;
	n = close(99);
	check("close of nothing: EBADF", (long)errno,
	    n == -1 && errno == EBADF);

	/* A directory is read as a file of 16-byte entries. */
	p0 = open(".", 0);
	n = fstat(p0, &st);
	check("a directory's st_mode, as the Sixth Edition had it",
	    (long)st.st_mode, n == 0 && (st.st_mode & S_IFMT) == S_IFDIR);
	close(p0);
	fd = open(".", 0);
	n = read(fd, buf, 32);
	check("read of a directory", (long)n,
	    n == 32 && buf[2] == '.' && buf[3] == 0);
	p0 = fd;

	t = time((long *)0);
	n = brk(sbrk(0) + 1024);
	check("brk, above the bss", (long)n, n == 0);
	errno = 0;
	n = brk((char *)2);
	check("brk, into the program: ENOMEM", (long)errno,
	    n == -1 && errno == ENOMEM);
	x = 1.5;
	x = x * 3.0;
	check("floating point (the int 0xf4 hook)", (long)(x * 10.0),
	    (int)(x * 10.0) == 45);
	n = isatty(p0);
	check("isatty of a file is not", (long)n, n == 0);
	n = umask(022);
	fd = umask(n);
	check("umask returns the old one", (long)fd, fd == 022);

	/* --- processes -------------------------------------------------- */
	n = pipe(p);
	check("pipe: two descriptors", (long)p[1],
	    n == 0 && p[0] >= 0 && p[1] > p[0]);

	/* Registers the compiler keeps across a call must survive a trap,
	 * the parent's fork included. */
	r1 = 0x1234;
	r2 = 0x5678;
	pid = fork();
	if (pid == 0) {
		/* The child: the return two bytes before the parent's. */
		write(p[1], "kid", 4);
		_exit(r1 == 0x1234 && r2 == 0x5678 ? 7 : 8);
	}
	check("fork, in the parent", (long)pid, pid > 0);
	check("  with its registers", (long)r1, r1 == 0x1234 && r2 == 0x5678);
	status = -1;
	n = wait(&status);
	check("wait returns the child", (long)n, n == pid);
	check("  and its status through the pointer", (long)status,
	    status == (7 << 8));
	n = read(p[0], buf, 4);
	check("what the child wrote", (long)n, n == 4 && strcmp(buf, "kid") == 0);
	errno = 0;
	n = wait(&status);
	check("wait with no children: ECHILD", (long)errno,
	    n == -1 && errno == ECHILD);

	pid = fork();
	if (pid == 0) {
		execl("/bin/echo", "echo", "  ok    exece of /bin/echo", 0);
		_exit(9);
	}
	wait(&status);
	check("  and its exit", (long)status, status == 0);

	/* --- signals ----------------------------------------------------- */
	old = signal(SIGINT, catch);
	check("signal returns the old disposition", (long)(old == SIG_DFL),
	    old == SIG_DFL);
	r1 = 0x4321;
	kill(getpid(), SIGINT);
	check("the handler ran, with the signal number", (long)caught,
	    caught == SIGINT && count == 1);
	check("  and returned to where it was", (long)r1, r1 == 0x4321);
	old = signal(SIGINT, SIG_IGN);
	check("the disposition was reset on delivery", (long)(old == SIG_DFL),
	    old == SIG_DFL);
	kill(getpid(), SIGINT);
	check("an ignored signal is", (long)count, count == 1);

	signal(SIGALRM, catch);
	alarm(1);
	pause();
	check("alarm ends pause", (long)caught, caught == SIGALRM);
	/* The same signal a second time: nothing is left blocked by the
	 * first handler's having run. */
	caught = 0;
	signal(SIGALRM, catch);
	alarm(1);
	pause();
	check("and again", (long)caught, caught == SIGALRM);

	pid = fork();
	if (pid == 0) {
		signal(SIGTERM, SIG_DFL);
		pause();
		_exit(0);
	}
	sleep(1);
	kill(pid, SIGTERM);
	wait(&status);
	check("a killed child's status is the signal", (long)status,
	    status == SIGTERM);

	printf("venixtest: %s\n", fails ? "FAIL" : "PASS");
	exit(fails != 0);
}
