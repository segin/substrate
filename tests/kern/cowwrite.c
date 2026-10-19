/*
 * cowwrite — the kernel's own stores into user memory respect read-only
 * and copy-on-write pages.
 *
 * Without CR0.WP an i386 kernel ignores a read-only PTE: a read(2) into a
 * buffer still shared with the parent after fork wrote the parent's page
 * too, and a read(2) into a PROT_READ private mapping of a file wrote the
 * page every process mapping that file sees.
 *
 * Builds for the host as well, where every line must also read ok:
 *
 *      cc -o cowwrite cowwrite.c && ./cowwrite
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define PAGE 4096

static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

/* A file of PAGE bytes of `c`, open for reading and writing; -1 if not. */
static int make_file(const char *path, int c) {
    char buf[PAGE];
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    memset(buf, c, sizeof(buf));
    if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
        close(fd);
        return -1;
    }
    lseek(fd, 0, SEEK_SET);
    return fd;
}

/* The child reads into a page it still shares with the parent. */
static void fork_cow(void) {
    static char page[PAGE] __attribute__((aligned(PAGE)));
    const char *path = "/tmp/cowwrite.fork";
    int fd = make_file(path, 'C');
    check(fd >= 0, "fork: make the file");
    if (fd < 0) return;

    memset(page, 'P', sizeof(page));        /* present and dirty */
    pid_t pid = fork();
    if (pid == 0) {
        ssize_t n = read(fd, page, PAGE);   /* the kernel's store, first */
        _exit(n == PAGE && page[0] == 'C' && page[PAGE - 1] == 'C' ? 0 : 1);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "fork: child read its copy");

    int intact = 1;
    for (int i = 0; i < PAGE; i++)
        if (page[i] != 'P') { intact = 0; break; }
    check(intact, "fork: parent's copy unchanged by the child's read");
    close(fd);
    unlink(path);
}

/* A read into a read-only private mapping of a file fails, and the file
 * and every other mapping of it are left alone. */
static void readonly_map(void) {
    const char *src = "/tmp/cowwrite.map";
    const char *in = "/tmp/cowwrite.in";
    int mfd = make_file(src, 'M');
    int ifd = make_file(in, 'X');
    check(mfd >= 0 && ifd >= 0, "map: make the files");
    if (mfd < 0 || ifd < 0) return;

    char *ro = mmap(NULL, PAGE, PROT_READ, MAP_PRIVATE, mfd, 0);
    char *other = mmap(NULL, PAGE, PROT_READ, MAP_SHARED, mfd, 0);
    check(ro != MAP_FAILED && other != MAP_FAILED, "map: map the file twice");
    if (ro == MAP_FAILED || other == MAP_FAILED) return;

    volatile char touch = ro[0];            /* make the page present */
    (void)touch;
    errno = 0;
    ssize_t n = read(ifd, ro, PAGE);
    check(n == -1 && errno == EFAULT, "map: read into PROT_READ fails EFAULT");
    check(ro[0] == 'M' && other[0] == 'M', "map: mappings unchanged");

    char buf[16];
    check(pread(mfd, buf, sizeof(buf), 0) == (ssize_t)sizeof(buf) &&
          buf[0] == 'M' && buf[15] == 'M', "map: file unchanged");
    munmap(ro, PAGE);
    munmap(other, PAGE);
    close(mfd);
    close(ifd);
    unlink(src);
    unlink(in);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    fork_cow();
    readonly_map();
    printf("%s\n", failed ? "cowwrite: FAIL" : "cowwrite: PASS");
    return failed;
}
