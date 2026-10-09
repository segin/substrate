/*
 * opencreat — open(O_CREAT) of a file that several processes create at once.
 *
 * Without O_EXCL the call means "open it, making it first if it is not
 * there", and it does not fail because someone else made it.  The kernel
 * looked the name up, found nothing, and created it as a second step; a
 * process that lost the race between the two was told EEXIST, and eight
 * useradds started together left half of themselves unable to open
 * /etc/.pwd.lock.
 *
 * Each round, CHILDREN processes wait on a pipe, are let go together, and
 * each open the same absent file.  Every one must get a descriptor.  With
 * O_EXCL, in the rounds after, exactly one must, and the rest EEXIST.
 *
 * Builds for the host as well, where it must also pass:
 *
 *      cc -o opencreat opencreat.c && ./opencreat
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define CHILDREN 8
#define ROUNDS   40

static const char *path = "/tmp/opencreat.tmp";

/* Children whose open failed (plain), or the number that succeeded (excl). */
static int round_of(int flags, int *other_errors) {
    int go[2], n = 0, i;
    pid_t pids[CHILDREN];
    char c = 'g';

    unlink(path);
    if (pipe(go) != 0) return -1;
    for (i = 0; i < CHILDREN; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            int fd;

            close(go[1]);
            if (read(go[0], &c, 1) < 0) _exit(3);   /* held until all exist */
            fd = open(path, O_WRONLY | O_CREAT | flags, 0600);
            if (fd >= 0) _exit(0);
            _exit(errno == EEXIST ? 1 : 2);
        }
    }
    close(go[0]);
    close(go[1]);                                   /* lets them all go */
    for (i = 0; i < CHILDREN; i++) {
        int st = 0;

        waitpid(pids[i], &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) >= 2) (*other_errors)++;
        else if (flags & O_EXCL) n += (WEXITSTATUS(st) == 0);
        else n += (WEXITSTATUS(st) != 0);
    }
    return n;
}

int main(void) {
    int r, refused = 0, wrong_winners = 0, other = 0;

    for (r = 0; r < ROUNDS; r++)
        refused += round_of(0, &other);
    printf("%s O_CREAT: %d opens of a file being created were refused\n",
           refused == 0 ? "ok   " : "FAIL ", refused);

    for (r = 0; r < ROUNDS; r++)
        wrong_winners += (round_of(O_EXCL, &other) != 1);
    printf("%s O_CREAT|O_EXCL: %d rounds without exactly one creator\n",
           wrong_winners == 0 ? "ok   " : "FAIL ", wrong_winners);

    printf("%s %d opens failed some other way\n", other == 0 ? "ok   " : "FAIL ", other);
    unlink(path);
    r = refused != 0 || wrong_winners != 0 || other != 0;
    printf("opencreat: %s\n", r ? "FAILED" : "PASS");
    return r;
}
