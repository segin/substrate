/*
 * calltest.c -- the system calls that take pointers, from a Xenix/286
 * program of either data model.
 *
 * Compiled by Xenix's own cc, under substrate, so it is in the C of the
 * time.  Built with no flags it is a small-model program, whose system
 * calls have their arguments in registers and every pointer an offset;
 * built with -Ml it is a large-model one, whose calls have them in a
 * block on the stack with every pointer far.  Both must say the same.
 *
 *	cc -o calltest calltest.c -lx && ./calltest
 *	cc -Mm -o calltest calltest.c -lx && ./calltest
 *	cc -Ml -o calltest calltest.c -lx && ./calltest
 *
 * -lx is the library of Xenix's own calls, where locking(S) is.
 *
 * Run with an argument it is its own child: see the exec check.
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/locking.h>

extern int errno;
extern char *malloc();
extern long lseek();
extern long time();

static int failed;

static check(ok, what)
int ok;
char *what;
{
	printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
	if (!ok)
		failed = 1;
}

/* A buffer that is not in the stack's segment, in the large model. */
static char far_buf[64];

main(argc, argv, envp)
int argc;
char **argv, **envp;
{
	static char path[] = "/tmp/calltest.tmp";
	static char moved[] = "/tmp/calltest.two";
	static char locked[] = "/tmp/calltest.lck";
	char on_stack[64];
	struct stat st;
	struct flock lk;	/* on the stack */
	char *heap, *args[4];
	long now, then;
	int fd, p[2], pid, status, i, n;

	if (argc > 1) {
		/* The child of the exec check: its arguments, and that the
		 * environment came too. */
		if (argc != 3 || strcmp(argv[1], "child") != 0 ||
		    strcmp(argv[2], "second argument") != 0)
			exit(3);
		for (i = 0; envp[i] != 0; i++)
			if (strncmp(envp[i], "PATH=", 5) == 0)
				exit(7);
		exit(4);
	}

	printf("pointers are %d bytes\n", (int)sizeof(char *));

	unlink(path);
	unlink(moved);
	fd = creat(path, 0644);
	check(fd >= 0, "creat");
	check(write(fd, "written from data\n", 18) == 18, "write from the data segment");
	strcpy(on_stack, "and from the stack\n");
	check(write(fd, on_stack, 19) == 19, "write from the stack");
	check(close(fd) == 0, "close");

	check(stat(path, &st) == 0 && st.st_size == 37L, "stat gives the size");
	check((st.st_mode & 0777) == 0644, "stat gives the mode");
	check(chmod(path, 0600) == 0 && stat(path, &st) == 0 &&
	      (st.st_mode & 0777) == 0600, "chmod");
	check(access(path, 4) == 0, "access");
	check(link(path, moved) == 0, "link");
	check(stat(moved, &st) == 0 && st.st_nlink == 2, "stat of the link");

	fd = open(moved, 0);
	check(fd >= 0, "open");
	check(fstat(fd, &st) == 0 && st.st_size == 37L, "fstat");
	n = read(fd, far_buf, 18);
	check(n == 18 && strncmp(far_buf, "written from data\n", 18) == 0,
	      "read into the data segment");
	n = read(fd, on_stack, 63);
	check(n == 19 && strncmp(on_stack, "and from the stack\n", 19) == 0,
	      "read into the stack");
	check(lseek(fd, 13L, 0) == 13L, "lseek");
	check(read(fd, on_stack, 4) == 4 && strncmp(on_stack, "data", 4) == 0,
	      "read after lseek");
	close(fd);

	heap = malloc(3000);
	check(heap != 0, "malloc");
	if (heap != 0) {
		fd = open(path, 0);
		n = read(fd, heap, 3000);
		check(n == 37 && strncmp(heap + 18, "and from the stack\n", 19) == 0,
		      "read into the heap");
		close(fd);
	}

	check(unlink(path) == 0 && unlink(moved) == 0, "unlink");
	check(stat(path, &st) != 0, "stat of what is gone fails");
	check(chdir("/tmp") == 0, "chdir");

	then = time((long *)0);
	check(time(&now) >= then && now >= then && now > 500000000L, "time");

	/* A record lock: its structure is reached through fcntl's third
	 * argument, which is declared an int and is a pointer here. */
	fd = open(locked, O_RDWR | O_CREAT, 0644);
	write(fd, "0123456789abcdef", 16);
	lk.l_type = F_WRLCK;
	lk.l_whence = 0;
	lk.l_start = 4L;
	lk.l_len = 8L;
	check(fcntl(fd, F_SETLK, &lk) == 0, "fcntl sets a write lock");
	pid = fork();
	if (pid == 0) {
		static struct flock probe;	/* in the data segment */
		int cfd = open(locked, O_RDWR);	/* its own, not the parent's */

		probe.l_type = F_WRLCK;
		probe.l_whence = 0;
		probe.l_start = 0L;
		probe.l_len = 0L;
		if (fcntl(cfd, F_GETLK, &probe) != 0)
			exit(10);
		if (probe.l_type != F_WRLCK || probe.l_start != 4L ||
		    probe.l_len != 8L || probe.l_pid != getppid())
			exit(11);
		probe.l_type = F_WRLCK;
		probe.l_start = 6L;
		probe.l_len = 1L;
		if (fcntl(cfd, F_SETLK, &probe) == 0)
			exit(12);
		probe.l_type = F_RDLCK;
		probe.l_start = 0L;
		probe.l_len = 4L;
		if (fcntl(cfd, F_SETLK, &probe) != 0)
			exit(13);
		exit(8);
	}
	status = 0;
	check(wait(&status) == pid && status == (8 << 8),
	      "another process finds the lock, and is refused it");
	if (status != (8 << 8))
		printf("      the child said %d\n", status >> 8);
	lk.l_type = F_UNLCK;
	check(fcntl(fd, F_SETLK, &lk) == 0, "fcntl takes the lock off");
	lk.l_type = F_WRLCK;
	lk.l_start = 0L;
	lk.l_len = 0L;
	check(fcntl(fd, F_GETLK, &lk) == 0 && lk.l_type == F_UNLCK,
	      "and then there is none");

	/* locking(S), Xenix's own call for it: the same locks. */
	lseek(fd, 4L, 0);
	check(locking(fd, LK_NBLCK, 8L) == 0, "locking(LK_NBLCK): bytes 4 to 11");
	pid = fork();
	if (pid == 0) {
		static struct flock probe;
		int cfd = open(locked, O_RDWR);

		lseek(cfd, 6L, 0);
		errno = 0;
		if (locking(cfd, LK_NBLCK, 2L) == 0)
			exit(10);
		if (errno != EACCES)
			exit(11);
		lseek(cfd, 0L, 0);
		if (locking(cfd, LK_NBLCK, 4L) != 0)
			exit(12);
		probe.l_type = F_WRLCK;
		probe.l_whence = 0;
		probe.l_start = 4L;
		probe.l_len = 8L;
		if (fcntl(cfd, F_GETLK, &probe) != 0 || probe.l_type != F_WRLCK)
			exit(13);
		exit(8);
	}
	status = 0;
	check(wait(&status) == pid && status == (8 << 8),
	      "another process is refused them with EACCES, and fcntl sees the lock");
	if (status != (8 << 8))
		printf("      the child said %d\n", status >> 8);

	/* LK_LOCK waits for what LK_NBLCK would be refused. */
	pipe(p);
	pid = fork();
	if (pid == 0) {
		int cfd = open(locked, O_RDWR);

		lseek(cfd, 4L, 0);
		n = locking(cfd, LK_LOCK, 8L);
		write(p[1], n == 0 ? "c" : "x", 1);
		exit(0);
	}
	sleep(1);
	write(p[1], "p", 1);
	lseek(fd, 4L, 0);
	check(locking(fd, LK_UNLCK, 8L) == 0, "locking(LK_UNLCK)");
	wait(&status);
	n = read(p[0], on_stack, 2);
	check(n == 2 && on_stack[0] == 'p' && on_stack[1] == 'c',
	      "locking(LK_LOCK) waited for the unlock");
	close(p[0]);
	close(p[1]);

	/* Read locks are shared, and keep a writer out. */
	lseek(fd, 0L, 0);
	check(locking(fd, LK_NBRLCK, 0L) == 0, "locking(LK_NBRLCK): the whole file");
	pid = fork();
	if (pid == 0) {
		int cfd = open(locked, O_RDWR);

		if (locking(cfd, LK_NBRLCK, 0L) != 0)
			exit(10);
		exit(locking(cfd, LK_NBLCK, 0L) == 0 ? 11 : 8);
	}
	status = 0;
	check(wait(&status) == pid && status == (8 << 8),
	      "another process reads beside it and may not write");
	close(fd);
	unlink(locked);

	check(pipe(p) == 0, "pipe");
	pid = fork();
	if (pid == 0) {
		close(p[0]);
		write(p[1], "from the child", 14);
		exit(5);
	}
	close(p[1]);
	n = read(p[0], on_stack, 63);
	check(n == 14 && strncmp(on_stack, "from the child", 14) == 0,
	      "a child writes down a pipe");
	close(p[0]);
	status = 0;
	check(wait(&status) == pid && status == (5 << 8), "wait gives its status");

	pid = fork();
	if (pid == 0) {
		static char *env[] = { "HOME=/", 0 };

		args[0] = argv[0];
		args[1] = "child";
		args[2] = "second argument";
		args[3] = 0;
		execve(argv[0], args, env);
		exit(6);
	}
	status = 0;
	check(wait(&status) == pid && status == (4 << 8),
	      "execve passes arguments and environment");

	printf("calltest: %s\n", failed ? "FAILED" : "PASS");
	exit(failed);
}
