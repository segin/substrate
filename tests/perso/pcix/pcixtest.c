/*
 * pcixtest.c - the system calls of the PC/IX personality.
 *
 * Compiled BY PC/IX, under substrate, with the C compiler on its own
 * distribution media -- there is no other compiler for the system -- so it
 * is in the C of 1984: no prototypes, no void, no unsigned long.  See
 * README.md for how it is run.
 *
 *     cc -o pcixtest pcixtest.c && ./pcixtest
 *
 * Prints a line per check and "pcixtest: PASS" or "FAIL"; exits non-zero
 * if anything failed.  What is checked is what /usr/include/sys.s on the
 * media says a call does, and where a call differs from its Xenix
 * counterpart -- which runs it -- that difference in particular: the
 * second result in DX (getpid, getuid, pipe, time), fork's two return
 * addresses, wait's status through a pointer, and a signal handler entered
 * and left the way the C library's stub expects.
 */
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>

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
	struct utsname un;
	char buf[64];
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
	check("getppid, from the same trap", (long)getppid(),
	    getppid() > 0 && getppid() != pid);
	check("getuid and geteuid agree", (long)getuid(),
	    getuid() == geteuid());
	check("getgid and getegid agree", (long)getgid(),
	    getgid() == getegid());
	t = time((long *)0);
	check("time, a long in DX:AX", t, t > 400000000L);

	/* --- files ----------------------------------------------------- */
	unlink("pcixtest.tmp");
	fd = creat("pcixtest.tmp", 0644);
	check("creat", (long)fd, fd >= 0);
	n = write(fd, "0123456789", 10);
	check("write", (long)n, n == 10);
	close(fd);
	n = stat("pcixtest.tmp", &st);
	check("stat", (long)n, n == 0);
	check("  st_size", (long)st.st_size, st.st_size == 10L);
	check("  st_mode", (long)st.st_mode,
	    (st.st_mode & S_IFMT) == S_IFREG && (st.st_mode & 0777) == 0644);
	check("  st_nlink", (long)st.st_nlink, st.st_nlink == 1);
	fd = open("pcixtest.tmp", 2);
	off = lseek(fd, -3L, 2);
	check("lseek from the end, a long both ways", off, off == 7L);
	n = read(fd, buf, 8);
	check("read to end of file", (long)n, n == 3 && buf[0] == '7');
	n = fstat(fd, &st);
	check("fstat", (long)st.st_size, n == 0 && st.st_size == 10L);
	close(fd);
	n = chmod("pcixtest.tmp", 0600);
	stat("pcixtest.tmp", &st);
	check("chmod", (long)(st.st_mode & 0777),
	    n == 0 && (st.st_mode & 0777) == 0600);
	n = link("pcixtest.tmp", "pcixtest.lnk");
	stat("pcixtest.tmp", &st);
	check("link", (long)st.st_nlink, n == 0 && st.st_nlink == 2);
	unlink("pcixtest.lnk");
	n = access("pcixtest.tmp", 4);
	check("access", (long)n, n == 0);
	unlink("pcixtest.tmp");
	errno = 0;
	n = open("pcixtest.tmp", 0);
	check("open of nothing: carry, and ENOENT", (long)errno,
	    n == -1 && errno == ENOENT);
	errno = 0;
	n = close(99);
	check("close of nothing: EBADF", (long)errno,
	    n == -1 && errno == EBADF);

	/* A directory is read as a file of 16-byte entries. */
	fd = open(".", 0);
	n = read(fd, buf, 32);
	check("read of a directory", (long)n,
	    n == 32 && buf[2] == '.' && buf[3] == 0);
	close(fd);

	n = uname(&un);
	check("uname", (long)n, n == 0 && strcmp(un.sysname, "PC/IX") == 0);
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

	printf("pcixtest: %s\n", fails ? "FAIL" : "PASS");
	exit(fails != 0);
}
