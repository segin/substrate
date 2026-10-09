/*
 * venixext.c - the system calls Venix/86 added to Version 7.
 *
 * Compiled by Venix's own cc under substrate, like venixtest.c beside it
 * (README.md), and to be run as the superuser from a directory it can
 * make a file in:
 *
 *     cc -o venixext venixext.c && ./venixext
 *
 * Semaphores (semset, semclear, semtest, semtset), shared data (sdata),
 * physical memory (phys), suspend, lock, region locking (locking) and
 * aiowait.  What each is expected to do is what Venix's kernel does; the
 * comment over them in sys/exec/perso/perso_xenix.c says where that was
 * read.
 *
 * sdata and phys point the ES register at something, and C here has no
 * way to say "through ES", so the test carries two routines as machine
 * code and calls them.  That works in a program linked the default way,
 * text and data in one segment.
 */
#include <stdio.h>
#include <signal.h>
#include <errno.h>

extern int errno;

int fails;

check(what, got, ok)
char *what;
long got;
int ok;
{
	printf("  %s  %s = %ld\n", ok ? "ok  " : "FAIL", what, got);
	if (!ok)
		fails++;
}

/* int espeek(off): the word at ES:off.
 *	push bp; mov bp,sp; mov bx,4(bp); es: mov ax,(bx); pop bp; ret */
char peekcode[] = {
	0x55, 0x8b, 0xec, 0x8b, 0x5e, 0x04, 0x26, 0x8b, 0x07, 0x5d, 0xc3
};
/* espoke(off, val): store val at ES:off.
 *	push bp; mov bp,sp; mov bx,4(bp); mov ax,6(bp); es: mov (bx),ax;
 *	pop bp; ret */
char pokecode[] = {
	0x55, 0x8b, 0xec, 0x8b, 0x5e, 0x04, 0x8b, 0x46, 0x06, 0x26, 0x89,
	0x07, 0x5d, 0xc3
};
int (*espeek)() = (int (*)())peekcode;
int (*espoke)() = (int (*)())pokecode;

int marker = 0x4d4b;

main()
{
	char buf[700];
	int fd, pid, status, p[2], n, a, b, c, i;

	setbuf(stdout, (char *)0);

	/* --- lock -------------------------------------------------------- */
	n = lock(1);
	check("lock(1), as the superuser", (long)n, n == 0);
	n = lock(0);
	check("lock(0)", (long)n, n == 0);

	/* --- semaphores -------------------------------------------------- */
	semclear(3);
	semclear(-1);
	n = semtest(3);
	check("semtest of a clear semaphore", (long)n, n == 0);
	n = semtset(3, 0);
	check("semtset takes it", (long)n, n == 0);
	n = semtest(3);
	check("semtest now", (long)n, n == 1);
	n = semtset(3, 0);
	check("semtset again: 1, it was set", (long)n, n == 1);
	n = semtest(4);
	check("its neighbour is another semaphore", (long)n, n == 0);
	n = semtset(-1, 0);
	check("a system-wide one (-1)", (long)n, n == 0);
	n = semtest(-1);
	check("  is set", (long)n, n == 1);
	semclear(-1);
	n = semclear(3);
	check("semclear", (long)n, n == 0 && semtest(3) == 0);
	errno = 0;
	n = semtest(16);
	check("semaphore 16: EINVAL", (long)errno, n == -1 && errno == EINVAL);

	/* semset waits for a semaphore another process holds. */
	pipe(p);
	semset(5, 0);
	pid = fork();
	if (pid == 0) {
		semset(5, 0);
		write(p[1], "c", 1);
		semclear(5);
		_exit(0);
	}
	sleep(1);
	write(p[1], "p", 1);
	semclear(5);
	wait(&status);
	n = read(p[0], buf, 2);
	check("semset waited for semclear", (long)n,
	    n == 2 && buf[0] == 'p' && buf[1] == 'c');

	/* --- shared data, and suspend ------------------------------------ */
	errno = 0;
	n = sdata(0, 0, 0);
	check("sdata(0) with none attached: EINVAL", (long)errno,
	    n == -1 && errno == EINVAL);
	n = sdata(1, 0, 1);
	check("sdata(1): 512 bytes of shared data", (long)n, n == 0);
	(*espoke)(0, 0);
	(*espoke)(510, 0x1234);
	check("  written and read through ES", (long)(*espeek)(510),
	    (*espeek)(510) == 0x1234);

	pid = fork();
	if (pid == 0) {
		for (;;)
			(*espoke)(0, (*espeek)(0) + 1);
	}
	sleep(1);
	n = suspend(pid, 1);
	a = (*espeek)(0);
	check("the child's writes are seen by the parent", (long)a, a != 0);
	sleep(1);
	b = (*espeek)(0);
	check("suspend(pid, 1) stopped it", (long)n, n == 0 && a == b);
	n = suspend(pid, 0);
	sleep(1);
	c = (*espeek)(0);
	check("suspend(pid, 0) let it go on", (long)n, n == 0 && c != b);
	kill(pid, SIGKILL);
	wait(&status);
	errno = 0;
	n = suspend(pid, 1);
	check("suspend of nobody: ESRCH", (long)errno,
	    n == -1 && errno == ESRCH);

	errno = 0;
	n = sdata(1, 0, 1);
	check("a second sdata(1): EINVAL", (long)errno,
	    n == -1 && errno == EINVAL);
	n = sdata(2, 0, 0);
	check("sdata(2) detaches", (long)n, n == 0);
	check("  and ES is the data segment again",
	    (long)(*espeek)(&marker), (*espeek)(&marker) == 0x4d4b);

	/* A file as shared data: what is stored there is in the file. */
	for (i = 0; i < sizeof buf; i++)
		buf[i] = 'a' + i % 26;
	unlink("venixext.tmp");
	fd = creat("venixext.tmp", 0644);
	write(fd, buf, sizeof buf);
	close(fd);
	n = sdata("venixext.tmp", 0, 0);
	check("sdata(path)", (long)n, n == 0);
	a = (*espeek)(0);
	check("  the file's first word", (long)a, a == ('a' | 'b' << 8));
	(*espoke)(2, 'X' | 'Y' << 8);
	n = sdata(0, 0, 1);
	a = (*espeek)(0);
	check("sdata(0, r, 1): ES 512 bytes in", (long)a,
	    n == 0 && (a & 0377) == buf[512]);
	sdata(2, 0, 0);
	fd = open("venixext.tmp", 0);
	read(fd, buf, 4);
	close(fd);
	check("the store went to the file", (long)buf[2],
	    buf[2] == 'X' && buf[3] == 'Y');

	/* --- locking ----------------------------------------------------- */
	fd = open("venixext.tmp", 2);
	n = locking(fd, 1, 0L);
	check("locking(fd, 1, 0): the whole file", (long)n, n == 0);
	pid = fork();
	if (pid == 0) {
		int g = open("venixext.tmp", 2);

		errno = 0;
		n = locking(g, 1, 0L);
		_exit(n == -1 && errno == EACCES ? 7 : 8);
	}
	wait(&status);
	check("another process is refused with EACCES", (long)status,
	    status == (7 << 8));
	n = locking(fd, 0, 0L);
	check("locking(fd, 0, 0) unlocks", (long)n, n == 0);
	pid = fork();
	if (pid == 0) {
		int g = open("venixext.tmp", 2);

		_exit(locking(g, 1, 0L) == 0 ? 7 : 8);
	}
	wait(&status);
	check("and then it may have it", (long)status, status == (7 << 8));

	/* Mode 2 waits for the lock instead of failing. */
	pipe(p);
	locking(fd, 1, 0L);
	pid = fork();
	if (pid == 0) {
		int g = open("venixext.tmp", 2);

		n = locking(g, 2, 0L);
		write(p[1], n == 0 ? "c" : "x", 1);
		_exit(0);
	}
	sleep(1);
	write(p[1], "p", 1);
	locking(fd, 0, 0L);
	wait(&status);
	n = read(p[0], buf, 2);
	check("locking(fd, 2, 0) waited for the unlock", (long)n,
	    n == 2 && buf[0] == 'p' && buf[1] == 'c');

	/* A lock is on a range of the file from where the descriptor is. */
	lseek(fd, 100L, 0);
	locking(fd, 1, 50L);
	pid = fork();
	if (pid == 0) {
		int g = open("venixext.tmp", 2);

		lseek(g, 200L, 0);
		a = locking(g, 1, 10L);
		lseek(g, 140L, 0);
		b = locking(g, 1, 20L);
		_exit(a == 0 && b == -1 ? 7 : 8);
	}
	wait(&status);
	check("bytes outside the range can be had, inside not", (long)status,
	    status == (7 << 8));
	lseek(fd, 100L, 0);
	locking(fd, 0, 50L);

	/* A process's locks on a file go when it closes the file. */
	{
		int h = open("venixext.tmp", 2);

		locking(h, 1, 0L);
		close(h);
	}
	pid = fork();
	if (pid == 0) {
		int g = open("venixext.tmp", 2);

		_exit(locking(g, 1, 0L) == 0 ? 7 : 8);
	}
	wait(&status);
	check("a lock is gone when its file is closed", (long)status,
	    status == (7 << 8));

	n = aiowait(fd, 0);
	check("aiowait: nothing outstanding", (long)n, n == 0);
	close(fd);
	unlink("venixext.tmp");

	/* --- phys --------------------------------------------------------- */
	errno = 0;
	n = phys(0, 0, 0x10);
	check("phys below the adapters: EPERM", (long)errno,
	    n == -1 && errno == EPERM);
	n = phys(0, 0, 0xb8000L / 512);
	check("phys at the display, B800:0", (long)n, n == 0);
	if (n == 0) {
		a = (*espeek)(0);
		(*espoke)(0, a);
		check("  read and written back", (long)a, 1);
	}

	printf("venixext: %s\n", fails ? "FAIL" : "PASS");
	exit(fails != 0);
}
