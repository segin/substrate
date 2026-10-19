/*
 * cowkeep — a page made private by a copy-on-write fault stays private.
 *
 * The COW fault handler made the private copy in the page tables only and
 * never put it in the mapping's vm_object, which still held the page as it
 * was before the write.  Whatever later rebuilt the mapping from the
 * object brought the old contents back.
 *
 *   A. MAP_PRIVATE file mapping, written, then fork: the child sees the
 *      parent's writes, not the file;
 *   B. anonymous memory written after a fork (a COW copy), then a second
 *      fork: the grandchild sees the newest contents;
 *   C. both survive in the parent itself after the forks.
 *
 * Runs on the host too.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

/* Fork; the child exits 0 if `p` starts with `want`. */
static int child_sees(const char *p, const char *want)
{
    pid_t pid = fork();
    if (pid == 0)
        _exit(strncmp(p, want, strlen(want)) == 0 ? 0 : 1);
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* A: a private file mapping. */
    char path[] = "/tmp/cowkeepXXXXXX";
    int fd = mkstemp(path);
    char page[4096];
    memset(page, 'F', sizeof(page));
    memcpy(page, "FILE", 4);
    check(fd >= 0 && write(fd, page, sizeof(page)) == (ssize_t)sizeof(page),
          "file written");
    char *fm = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    check(fm != MAP_FAILED, "mmap MAP_PRIVATE");
    if (fm == MAP_FAILED) {
        printf("cowkeep: FAIL\n");
        return 1;
    }
    check(fm[0] == 'F', "mapping shows the file");
    memcpy(fm, "MINE", 4);                       /* COW: private copy */
    check(child_sees(fm, "MINE"), "child sees the parent's private write");

    /* B: anonymous memory, COW'd by a fork, written, forked again. */
    char *am = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy(am, "ONE", 4);
    check(child_sees(am, "ONE"), "child sees the first contents");
    memcpy(am, "TWO", 4);                        /* COW after the fork */
    check(child_sees(am, "TWO"), "a later child sees the newer contents");
    pid_t pid = fork();
    if (pid == 0)
        _exit(child_sees(am, "TWO") ? 0 : 1);    /* grandchild */
    int st = 0;
    waitpid(pid, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "grandchild sees them too");

    /* C */
    check(memcmp(fm, "MINE", 4) == 0 && memcmp(am, "TWO", 3) == 0,
          "parent still has both");

    unlink(path);
    printf("cowkeep: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
