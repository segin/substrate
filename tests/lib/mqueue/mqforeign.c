/*
 * mqforeign.c — POSIX message-queue descriptors belong to their process.
 *
 * mq descriptors used to be one system-wide table indexed straight by the
 * mqd_t value, so any process could read, send to, change or close
 * another process's descriptor just by guessing its number.  Now:
 *
 *   A. a process that never opened (or inherited) a descriptor gets EBADF
 *      from mq_getattr / mq_send / mq_receive / mq_setattr / mq_close on
 *      it, and its mq_close leaves the owner's descriptor working;
 *   B. fork() still hands the child the parent's descriptors under the
 *      same values, sharing the open description (O_NONBLOCK included);
 *   C. the child closing its inherited copy leaves the parent's open.
 *
 * Prints ok/FAIL lines and "mqforeign: PASS" or "mqforeign: FAIL".
 */
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define QNAME "/mqforeign"

static int fails;

static void check(int cond, const char *what)
{
    printf("%s %s\n", cond ? "ok   " : "FAIL ", what);
    if (!cond)
        fails++;
}

/* Run fn in a child and report whether it exited 0. */
static int in_child(int (*fn)(mqd_t), mqd_t q)
{
    pid_t pid = fork();
    if (pid == 0)
        _exit(fn(q));
    int st = 0;
    if (pid < 0 || waitpid(pid, &st, 0) != pid)
        return 0;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

/* B: the inherited descriptor works and shares the description. */
static int child_inherited(mqd_t q)
{
    struct mq_attr a;
    char buf[64];
    if (mq_getattr(q, &a) != 0 || a.mq_curmsgs != 1)
        return 1;
    if (!(a.mq_flags & O_NONBLOCK))
        return 2;            /* parent set O_NONBLOCK before fork */
    if (mq_receive(q, buf, sizeof(buf), NULL) != 5 || memcmp(buf, "hello", 5))
        return 3;
    if (mq_send(q, "back", 4, 0) != 0)
        return 4;
    return 0;
}

/* C: closing the inherited copy. */
static int child_close(mqd_t q)
{
    return mq_close(q) == 0 ? 0 : 1;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    mq_unlink(QNAME);

    /* A: a stranger forked BEFORE the open, handed the value by pipe. */
    int pfd[2];
    if (pipe(pfd) != 0) {
        printf("mqforeign: FAIL (pipe)\n");
        return 1;
    }
    pid_t stranger = fork();
    if (stranger == 0) {
        mqd_t v;
        close(pfd[1]);
        if (read(pfd[0], &v, sizeof(v)) != sizeof(v))
            _exit(9);
        struct mq_attr a, na = { 0 };
        char buf[64];
        int bad = 0;
        errno = 0;
        bad |= !(mq_getattr(v, &a) == -1 && errno == EBADF) << 0;
        errno = 0;
        bad |= !(mq_send(v, "x", 1, 0) == -1 && errno == EBADF) << 1;
        errno = 0;
        bad |= !(mq_receive(v, buf, sizeof(buf), NULL) == -1 &&
                 errno == EBADF) << 2;
        errno = 0;
        bad |= !(mq_setattr(v, &na, NULL) == -1 && errno == EBADF) << 3;
        errno = 0;
        bad |= !(mq_close(v) == -1 && errno == EBADF) << 4;
        _exit(bad);
    }
    close(pfd[0]);

    struct mq_attr attr = { .mq_maxmsg = 4, .mq_msgsize = 64 };
    mqd_t q = mq_open(QNAME, O_CREAT | O_RDWR | O_NONBLOCK, 0600, &attr);
    check(q != (mqd_t)-1, "mq_open");
    if (q == (mqd_t)-1) {
        printf("mqforeign: FAIL\n");
        return 1;
    }
    check(mq_send(q, "hello", 5, 1) == 0, "mq_send");

    write(pfd[1], &q, sizeof(q));
    close(pfd[1]);
    int st = 0;
    waitpid(stranger, &st, 0);
    int bad = WIFEXITED(st) ? WEXITSTATUS(st) : 0xff;
    printf("      stranger result bits %#x\n", bad);
    check(bad == 0, "a process without the descriptor gets EBADF for all five");

    struct mq_attr a;
    check(mq_getattr(q, &a) == 0 && a.mq_curmsgs == 1,
          "owner's descriptor unharmed, message still queued");

    check(in_child(child_inherited, q),
          "fork child uses the inherited descriptor (same value, shared flags)");
    check(in_child(child_close, q), "fork child closes its inherited copy");

    char buf[64];
    check(mq_receive(q, buf, sizeof(buf), NULL) == 4 && !memcmp(buf, "back", 4),
          "parent still reads through its own descriptor");
    check(mq_close(q) == 0, "parent mq_close");
    mq_unlink(QNAME);

    printf("mqforeign: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
