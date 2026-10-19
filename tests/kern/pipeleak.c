/*
 * pipeleak — a failed pipe() gives back everything it took.
 *
 * When pipe() failed part way -- no second descriptor, no memory for a
 * file -- the pipe, its 4 KiB buffer and both endpoints stayed allocated
 * for good; and when the descriptors could not be copied out to the
 * caller, both stayed open, unknown to the program.
 *
 *   A. pipe() with a bad pointer: EFAULT, and no descriptors left open;
 *   B. with exactly one descriptor free, 3000 pipe() calls fail EMFILE
 *      and free memory does not drop (each used to leak over 4 KiB).
 *
 * Runs on the host too.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ROUNDS 3000

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static int open_fds(void)
{
    int n = 0;
    for (int fd = 0; fd < 1024; fd++)
        if (fcntl(fd, F_GETFD) != -1)
            n++;
    return n;
}

/* MemFree from /proc/meminfo, in kB, or -1. */
static long mem_free_kb(void)
{
    char buf[512];
    int fd = open("/proc/meminfo", O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return -1;
    buf[n] = '\0';
    char *p = strstr(buf, "MemFree:");
    long kb = -1;
    if (p)
        sscanf(p + 8, "%ld", &kb);
    return kb;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    int before = open_fds();
    errno = 0;
    int r = pipe((int *)8);
    check(r == -1 && errno == EFAULT, "pipe() with a bad pointer -> EFAULT");
    check(open_fds() == before, "and no descriptors left open");

    /* Fill the table, then free one: the second descriptor never comes. */
    int last = -1;
    for (;;) {
        int fd = dup(0);
        if (fd < 0)
            break;
        last = fd;
    }
    check(last >= 0, "descriptor table filled");
    close(last);

    long kb0 = mem_free_kb();
    int emfile = 0;
    for (int i = 0; i < ROUNDS; i++) {
        int p[2];
        if (pipe(p) == -1 && errno == EMFILE)
            emfile++;
        else if (p[0] >= 0) {
            close(p[0]);
            close(p[1]);
        }
    }
    long kb1 = mem_free_kb();
    check(emfile == ROUNDS, "every pipe() failed with EMFILE");
    printf("      MemFree %ld kB -> %ld kB\n", kb0, kb1);
    check(kb0 < 0 || kb1 >= kb0 - 2048, "no memory lost to the failed pipes");

    printf("pipeleak: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
