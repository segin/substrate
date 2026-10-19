/*
 * lseekchk — lseek refuses positions that are not positions.
 *
 * lseek accepted any result, so a seek before the start left a negative
 * f_offset (which filesystems then read as a huge unsigned offset), and a
 * seek past the largest off_t wrapped.  SEEK_END on a descriptor with no
 * file node also dereferenced NULL.
 *
 *   - lseek(fd, -1, SEEK_SET) and a SEEK_CUR to before 0: EINVAL, the
 *     offset is unchanged;
 *   - a SEEK_CUR past the largest off_t: EOVERFLOW (Linux says EINVAL;
 *     either is accepted), offset unchanged;
 *   - a 3 GiB position works and reads back (64-bit off_t);
 *   - SEEK_END gives the file's size.
 *
 * Runs on the host too.
 */
#define _FILE_OFFSET_BITS 64
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    char path[] = "/tmp/lseekchkXXXXXX";
    int fd = mkstemp(path);
    check(fd >= 0 && write(fd, "0123456789", 10) == 10, "file of 10 bytes");
    if (fd < 0) {
        printf("lseekchk: FAIL\n");
        return 1;
    }

    errno = 0;
    check(lseek(fd, -1, SEEK_SET) == -1 && errno == EINVAL,
          "SEEK_SET to -1 -> EINVAL");
    check(lseek(fd, 0, SEEK_CUR) == 10, "offset unchanged (10)");

    check(lseek(fd, 4, SEEK_SET) == 4, "SEEK_SET to 4");
    errno = 0;
    check(lseek(fd, -5, SEEK_CUR) == -1 && errno == EINVAL,
          "SEEK_CUR to before the start -> EINVAL");
    check(lseek(fd, 0, SEEK_CUR) == 4, "offset unchanged (4)");

    off_t big = (off_t)3 << 30;
    check(lseek(fd, big, SEEK_SET) == big && lseek(fd, 0, SEEK_CUR) == big,
          "a 3 GiB position");

    off_t max = (off_t)INT64_MAX;
    if (lseek(fd, max, SEEK_SET) == max) {
        errno = 0;
        off_t r = lseek(fd, 1, SEEK_CUR);
        check(r == -1 && (errno == EOVERFLOW || errno == EINVAL),
              "SEEK_CUR past the largest off_t -> EOVERFLOW");
        check(lseek(fd, 0, SEEK_CUR) == max, "offset unchanged (max)");
    } else {
        printf("      (largest off_t not seekable here; overflow case skipped)\n");
    }

    check(lseek(fd, 0, SEEK_END) == 10, "SEEK_END gives the size");

    close(fd);
    unlink(path);
    printf("lseekchk: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
