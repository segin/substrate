/*
 * ksemforeign.c — POSIX semaphore operations need an open reference or
 * permission.
 *
 * Kernel semaphore ids are small and sequential, and wait, trywait, post
 * and getvalue used to accept any live id from any process.  Now the
 * caller must hold the semaphore open (sem_open / sem_init, or inherited
 * across fork) or have the permission bits the operation needs.
 *
 *   A. root creates "/ksemforeign" with mode 0600 and value 1.  A process
 *      forked BEFORE the open drops to uid 1000 and is handed the id: its
 *      post, trywait, wait and getvalue all fail with EINVAL and the value
 *      stays 1.
 *   B. a child forked AFTER the open, also dropped to uid 1000, still
 *      posts and trywaits through the reference it inherited.
 *
 * Substrate-only (raw SYS_KSEM_* calls); must start as root.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#define NAME "/ksemforeign"
#define USER 1000

static int fails;

static void check(int cond, const char *what)
{
    printf("%s %s\n", cond ? "ok   " : "FAIL ", what);
    if (!cond)
        fails++;
}

static long getvalue(long id)
{
    int v = -1;
    long r = syscall(SYS_KSEM_GETVALUE, id, (long)&v);
    return r < 0 ? r : v;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (geteuid() != 0) {
        printf("ksemforeign: SKIP (needs root)\n");
        return 0;
    }
    syscall(SYS_KSEM_UNLINK, (long)NAME);

    int pfd[2];
    if (pipe(pfd) != 0) {
        printf("ksemforeign: FAIL (pipe)\n");
        return 1;
    }
    pid_t stranger = fork();
    if (stranger == 0) {
        long id;
        close(pfd[1]);
        if (setuid(USER) != 0)
            _exit(0x40);
        if (read(pfd[0], &id, sizeof(id)) != sizeof(id))
            _exit(0x41);
        int bad = 0;
        bad |= (syscall(SYS_KSEM_POST, id, 0L) != -EINVAL) << 0;
        bad |= (syscall(SYS_KSEM_TRYWAIT, id, 0L) != -EINVAL) << 1;
        bad |= (syscall(SYS_KSEM_WAIT, id, 0L) != -EINVAL) << 2;
        bad |= (getvalue(id) != -EINVAL) << 3;
        _exit(bad);
    }
    close(pfd[0]);

    long id = syscall(SYS_KSEM_OPEN, (long)NAME, (long)(O_CREAT | O_EXCL),
                      0600L, 1L);
    check(id >= 0, "root creates the semaphore, mode 0600, value 1");
    if (id < 0) {
        printf("ksemforeign: FAIL\n");
        return 1;
    }

    write(pfd[1], &id, sizeof(id));
    close(pfd[1]);
    int st = 0;
    waitpid(stranger, &st, 0);
    int bad = WIFEXITED(st) ? WEXITSTATUS(st) : 0xff;
    printf("      stranger result bits %#x\n", bad);
    check(bad == 0, "uid 1000 without the semaphore open: EINVAL for all four");
    check(getvalue(id) == 1, "value still 1");

    pid_t heir = fork();
    if (heir == 0) {
        if (setuid(USER) != 0)
            _exit(1);
        if (syscall(SYS_KSEM_POST, id) != 0)
            _exit(2);
        if (syscall(SYS_KSEM_TRYWAIT, id) != 0)
            _exit(3);
        if (getvalue(id) != 1)
            _exit(4);
        _exit(0);
    }
    st = 0;
    waitpid(heir, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
          "uid 1000 child forked after the open uses its inherited reference");

    check(syscall(SYS_KSEM_CLOSE, id) == 0, "close");
    check(syscall(SYS_KSEM_UNLINK, (long)NAME) == 0, "unlink");

    printf("ksemforeign: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
