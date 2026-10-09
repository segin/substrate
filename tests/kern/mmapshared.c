/*
 * mmapshared — what a program stores through a shared, writable mapping
 * of a file is in the file.
 *
 * LLVM's linker writes its output this way and no other: it sizes the
 * file with ftruncate, maps it MAP_SHARED, builds the program in the
 * mapping, unmaps and renames, and never calls write.  The file it left
 * under substrate was the right length and all zeros.
 *
 * Each check stores through a mapping and reads the file back with
 * read(2) through another descriptor, after a different way of letting
 * go of the mapping.  Builds for the host as well, where every line must
 * also read ok:
 *
 *      cc -o mmapshared mmapshared.c && ./mmapshared
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define LEN 11172               /* the linker's, and not a whole page */

static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

/* What byte `i` of a file written with `seed` is. */
static unsigned char pat(int seed, int i) {
    return (unsigned char)(seed + i * 7 + (i >> 8));
}

/* A file of LEN bytes, mapped shared and writable; NULL if it cannot be. */
static unsigned char *map_new(const char *path, int *fd) {
    void *p;

    unlink(path);
    *fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (*fd < 0 || ftruncate(*fd, LEN) != 0) return NULL;
    p = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    return p == MAP_FAILED ? NULL : (unsigned char *)p;
}

static void fill(unsigned char *p, int seed) {
    int i;

    for (i = 0; i < LEN; i++) p[i] = pat(seed, i);
}

/* Is the file LEN bytes of the pattern, read with read(2)? */
static int file_is(const char *path, int seed) {
    static unsigned char buf[LEN + 16];
    int fd = open(path, O_RDONLY), n, i;

    if (fd < 0) return 0;
    n = (int)read(fd, buf, sizeof(buf));
    close(fd);
    if (n != LEN) return 0;
    for (i = 0; i < LEN; i++)
        if (buf[i] != pat(seed, i)) return 0;
    return 1;
}

int main(void) {
    static const char *a = "/tmp/mmapshared.a", *b = "/tmp/mmapshared.b";
    unsigned char *p;
    struct stat st;
    int fd, st2 = 0;
    pid_t pid;

    /* The linker's sequence: unmap, rename, close. */
    p = map_new(a, &fd);
    check(p != NULL, "a new file, sized and mapped shared");
    if (!p) return 1;
    fill(p, 1);
    check(munmap(p, LEN) == 0, "unmapped");
    unlink(b);
    check(rename(a, b) == 0, "renamed");
    close(fd);
    check(file_is(b, 1), "what was stored is in the file: munmap, rename, close");
    check(stat(b, &st) == 0 && st.st_size == LEN, "and the file is no longer than it was made");

    /* Seen through read(2) while still mapped, once msync has run. */
    p = map_new(a, &fd);
    fill(p, 2);
    check(msync(p, LEN, MS_SYNC) == 0, "msync");
    check(file_is(a, 2), "what was stored is in the file: msync, still mapped");
    munmap(p, LEN);
    close(fd);

    /* The descriptor closed first, the mapping outliving it. */
    p = map_new(a, &fd);
    close(fd);
    fill(p, 3);
    munmap(p, LEN);
    check(file_is(a, 3), "what was stored is in the file: close, then store, then munmap");

    /* Never unmapped: the process just exits. */
    pid = fork();
    if (pid == 0) {
        p = map_new(a, &fd);
        if (!p) _exit(1);
        fill(p, 4);
        _exit(0);
    }
    waitpid(pid, &st2, 0);
    check(WIFEXITED(st2) && WEXITSTATUS(st2) == 0 && file_is(a, 4),
          "what was stored is in the file: exit with it mapped");

    /* A store into part of a file that has contents leaves the rest. */
    fd = open(a, O_RDWR);
    p = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    check(p != MAP_FAILED && p[100] == pat(4, 100), "a file's contents are seen through a mapping of it");
    if (p != MAP_FAILED) {
        memset(p + 5000, 0xEE, 10);
        munmap(p, LEN);
    }
    close(fd);
    {
        static unsigned char buf[LEN];
        int n, ok;

        fd = open(a, O_RDONLY);
        n = (int)read(fd, buf, LEN);
        close(fd);
        ok = n == LEN && buf[4999] == pat(4, 4999) && buf[5000] == 0xEE &&
             buf[5009] == 0xEE && buf[5010] == pat(4, 5010) && buf[0] == pat(4, 0) &&
             buf[LEN - 1] == pat(4, LEN - 1);
        check(ok, "ten bytes stored in the middle, and the rest as it was");
    }

    /* A private mapping is the other thing: the file does not change. */
    fd = open(a, O_RDWR);
    p = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (p != MAP_FAILED) {
        memset(p, 0x11, LEN);
        munmap(p, LEN);
    }
    close(fd);
    {
        static unsigned char buf[16];

        fd = open(a, O_RDONLY);
        check(read(fd, buf, 16) == 16 && buf[0] == pat(4, 0) && buf[15] == pat(4, 15),
              "a private mapping's stores are not in the file");
        close(fd);
    }

    unlink(a);
    unlink(b);
    printf("mmapshared: %s\n", failed ? "FAILED" : "PASS");
    return failed;
}
