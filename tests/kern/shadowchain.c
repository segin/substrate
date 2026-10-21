/*
 * shadowchain -- freeing a long copy-on-write shadow chain does not run
 * the kernel stack off its end.
 *
 * Each fork after which the parent writes its memory again puts a new
 * shadow object on top of the old one, and a process that forks over and
 * over -- a shell running a test loop -- builds a backing chain hundreds
 * deep.  vm_object_deallocate freed the chain by recursing once per link,
 * which ran the 16 KiB kernel stack off its base into the page below (on
 * x86_64 a few hundred links were enough to overwrite block-cache headers
 * there -- the cache's queues were found pointing into a live kernel stack,
 * and reading /proc/meminfo panicked; at 500 links both architectures
 * fault).  A child forks 2 rounds of 500 live grandchildren, writing a
 * page after each fork, then exits; the test then checks the system still
 * forks and reads files.  The old kernel faults or corrupts memory instead.
 * Portable POSIX.
 */
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROUNDS 2
#define BATCH  500      /* chain depth ~ BATCH; the old recursion died near 200 */

/*
 * The children stay alive while the parent writes after each fork, so each
 * write lands on a page still shared with a child and adds a shadow level;
 * ROUNDS rounds of BATCH children stack them up, as soak_reap's batches did.
 */
static int chain_builder(void)
{
    static char page[4096];
    static pid_t kids[BATCH];

    for (int r = 0; r < ROUNDS; r++) {
        for (int i = 0; i < BATCH; i++) {
            pid_t p = fork();
            if (p < 0)
                return 2;
            if (p == 0) {
                pause();
                _exit(0);
            }
            kids[i] = p;
            memset(page, i & 0xff, sizeof page);  /* COW-break while shared */
        }
        for (int i = 0; i < BATCH; i++)
            kill(kids[i], SIGKILL);
        for (int i = 0; i < BATCH; i++)
            waitpid(kids[i], NULL, 0);
    }
    return page[0] == (char)((BATCH - 1) & 0xff) ? 0 : 3;
}

int main(void)
{
    char buf[256];

    pid_t b = fork();
    if (b == 0)
        _exit(chain_builder());   /* its exit frees the chain */
    int st = 0;
    if (waitpid(b, &st, 0) != b || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("shadowchain: chain builder failed (status 0x%x): FAIL\n", st);
        return 1;
    }

    /* The kernel is still sane: fork, and read a file a few times. */
    for (int i = 0; i < 20; i++) {
        pid_t p = fork();
        if (p == 0)
            _exit(0);
        waitpid(p, NULL, 0);
        int fd = open("/proc/meminfo", O_RDONLY);
        if (fd < 0 || read(fd, buf, sizeof buf) <= 0) {
            printf("shadowchain: reading /proc/meminfo failed: FAIL\n");
            return 1;
        }
        close(fd);
    }
    printf("shadowchain: PASS\n");
    return 0;
}
