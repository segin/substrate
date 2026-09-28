/*
 * torture_fseek_pipe.c -- a failed fseek() leaves the error indicator clear.
 *
 * fseek()/fseeko() on an unseekable stream fail with ESPIPE.  They used to
 * set the stream's error indicator as well, so ferror() then reported a
 * write error that never happened.  flex dup2()s a pipe onto stdout and
 * syncs the FILE with fseek(stdout, 0, SEEK_CUR); the poisoned stream made
 * it skip closing stdout before wait()ing for the filter processes reading
 * that pipe, and every flex run hung.
 *
 *   espipe    fseek/fseeko on a pipe return -1 with errno ESPIPE;
 *   ferror    ...and ferror() stays 0 on both ends;
 *   io        the streams still carry data, and the reader sees EOF;
 *   negative  a negative SEEK_SET on a regular file fails and also
 *             leaves ferror() clear.
 *
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

int main(void)
{
	int p[2];
	FILE *r, *w, *f;
	char buf[32];

	if (pipe(p) != 0) {
		printf("pipe: %s\nResult: FAIL\n", strerror(errno));
		return 1;
	}
	r = fdopen(p[0], "r");
	w = fdopen(p[1], "w");
	check(r != NULL && w != NULL, "fdopen");
	if (r == NULL || w == NULL) {
		printf("Result: FAIL\n");
		return 1;
	}

	errno = 0;
	check(fseek(w, 0, SEEK_CUR) == -1, "fseek on pipe (write end) fails");
	check(errno == ESPIPE, "fseek on pipe sets ESPIPE");
	check(!ferror(w), "fseek on pipe leaves ferror clear (write end)");

	errno = 0;
	check(fseeko(r, 0, SEEK_CUR) == -1, "fseeko on pipe (read end) fails");
	check(errno == ESPIPE, "fseeko on pipe sets ESPIPE");
	check(!ferror(r), "fseeko on pipe leaves ferror clear (read end)");

	check(fputs("hello\n", w) >= 0, "fputs after failed seek");
	check(fflush(w) == 0, "fflush after failed seek");
	check(!ferror(w), "no write error after fflush");
	check(fclose(w) == 0, "fclose of write end succeeds");

	check(fgets(buf, sizeof buf, r) != NULL && strcmp(buf, "hello\n") == 0,
	      "data crosses the pipe");
	check(fgets(buf, sizeof buf, r) == NULL && feof(r) && !ferror(r),
	      "reader sees EOF, not an error");
	fclose(r);

	f = tmpfile();
	check(f != NULL, "tmpfile");
	if (f != NULL) {
		check(fseek(f, -1, SEEK_SET) == -1, "negative fseek fails");
		check(!ferror(f), "negative fseek leaves ferror clear");
		fclose(f);
	}

	printf("Result: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
	       failures, failures == 1 ? "" : "s");
	return failures != 0;
}
