/*
 * torture_lseek64 - lseek() must return offsets past 2 GiB.
 *
 * The i386 syscall return is 32 bits of EAX plus 32 bits of EDX.  Two
 * separate bugs used to throw the high half away:
 *
 *   - the kernel forced EDX to 0 for every native syscall, and
 *   - libc's _syscall4 ran `cdq`, sign-extending EAX over EDX.
 *
 * Either one alone caps lseek at 2 GiB: an offset of 0x80000000 comes back
 * as 0xFFFFFFFF80000000, which libc reads as negative and reports as a
 * failure.  Nothing about the file needs to be that large for it to bite --
 * seeking past the end is legal and must return the requested offset -- so
 * this test needs no disk space at all.
 *
 * That is how e2fsck decided a 4 GiB partition was exactly 2 GiB: it sizes
 * a device by binary-searching lseek, and the search stopped at the wall.
 *
 * Builds on host or substrate; on the host it is a control that shows the
 * same expectations hold on a known-good platform.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void check(int fd, off_t want, const char *what)
{
	off_t got;

	errno = 0;
	got = lseek(fd, want, SEEK_SET);
	if (got == (off_t)-1) {
		printf("FAIL %-28s lseek(%" PRId64 ") -> -1 errno=%d (%s)\n",
		       what, (int64_t)want, errno, strerror(errno));
		failures++;
		return;
	}
	if (got != want) {
		printf("FAIL %-28s lseek(%" PRId64 ") -> %" PRId64 "\n",
		       what, (int64_t)want, (int64_t)got);
		failures++;
		return;
	}
	printf("ok   %-28s %" PRId64 "\n", what, (int64_t)got);
}

int main(void)
{
	const char *path = "/tmp/torture_lseek64.tmp";
	int fd;

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	unlink(path);

	/* Below the boundary: these always worked. */
	check(fd, 0, "zero");
	check(fd, 1234567, "small");
	check(fd, (off_t)0x7FFFFFFF, "INT32_MAX");

	/* The boundary itself and beyond: these were the broken cases. */
	check(fd, (off_t)0x80000000LL, "2GiB (INT32_MAX+1)");
	check(fd, (off_t)0xC0000000LL, "3GiB");
	check(fd, (off_t)0x100000000LL, "4GiB");
	check(fd, (off_t)0x123456789LL, "beyond 4GiB");

	/* SEEK_CUR from a >2GiB position must not wrap either. */
	if (lseek(fd, (off_t)0x100000000LL, SEEK_SET) != (off_t)0x100000000LL) {
		printf("FAIL %-28s could not position for SEEK_CUR\n", "seek_cur setup");
		failures++;
	} else {
		off_t got = lseek(fd, (off_t)0x10000000LL, SEEK_CUR);
		if (got != (off_t)0x110000000LL) {
			printf("FAIL %-28s -> %" PRId64 "\n", "SEEK_CUR past 4GiB",
			       (int64_t)got);
			failures++;
		} else {
			printf("ok   %-28s %" PRId64 "\n", "SEEK_CUR past 4GiB",
			       (int64_t)got);
		}
	}

	/* Errors must still be reported as errors, not as huge offsets --
	 * the reason the plain _syscall4 sign-extends in the first place. */
	errno = 0;
	if (lseek(-1, 0, SEEK_SET) != (off_t)-1 || errno != EBADF) {
		printf("FAIL %-28s bad fd did not give EBADF (errno=%d)\n",
		       "EBADF still reported", errno);
		failures++;
	} else {
		printf("ok   %-28s EBADF\n", "EBADF still reported");
	}

	close(fd);
	printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}
