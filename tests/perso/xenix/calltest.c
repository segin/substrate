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
 *	cc -o calltest calltest.c && ./calltest
 *	cc -Ml -o calltest calltest.c && ./calltest
 *
 * Run with an argument it is its own child: see the exec check.
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

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
	char on_stack[64];
	struct stat st;
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
