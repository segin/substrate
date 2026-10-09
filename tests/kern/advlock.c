/*
 * advlock — fcntl(2) record locks are on the file, and a process's own.
 *
 * Two processes that each open a file contend for it: the kernel used to
 * keep the locks on the open file description, so that only processes
 * sharing one descriptor (across fork) ever saw each other's, and two
 * programs that each opened /etc/.pwd.lock both "held" it.  F_SETLKW did
 * not wait, either; it failed with EAGAIN.
 *
 * The checks are POSIX's, and the program builds for the host as well,
 * where every line must also read ok:
 *
 *      cc -o advlock advlock.c && ./advlock
 *
 * A child is always another PROCESS; whether it uses the descriptor it
 * inherited or opens the file itself is what several checks turn on.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

static const char *path = "/tmp/advlock.tmp";
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

static int lockop(int fd, int cmd, short type, off_t start, off_t len) {
    struct flock fl;

    memset(&fl, 0, sizeof(fl));
    fl.l_type = type;
    fl.l_whence = SEEK_SET;
    fl.l_start = start;
    fl.l_len = len;
    return fcntl(fd, cmd, &fl) == 0 ? 0 : errno;
}

static long now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void nap_ms(long ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

    nanosleep(&ts, NULL);
}

/* The exit status of a child that ran `body`, which returns it. */
static int in_child(int (*body)(int), int arg) {
    int st = 0;
    pid_t pid = fork();

    if (pid == 0) _exit(body(arg));
    if (waitpid(pid, &st, 0) != pid || !WIFEXITED(st)) return -1;
    return WEXITSTATUS(st);
}

/* --- what the children do ------------------------------------------ */

/* Probe [0,10) for a write lock; 0 if one is found and it is the parent's. */
static int child_probe(int fd) {
    struct flock fl;

    if (fd < 0) fd = open(path, O_RDWR);
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_len = 10;
    if (fcntl(fd, F_GETLK, &fl) != 0) return 1;
    if (fl.l_type != F_WRLCK) return 2;
    if (fl.l_pid != getppid()) return 3;
    if (fl.l_start != 2 || fl.l_len != 6) return 4;
    return 0;
}

/* Try for a write lock on [4,5) without waiting; 0 if refused as it should be. */
static int child_refused(int fd) {
    int e;

    if (fd < 0) fd = open(path, O_RDWR);
    e = lockop(fd, F_SETLK, F_WRLCK, 4, 1);
    return (e == EAGAIN || e == EACCES) ? 0 : 1 + (e == 0);
}

/* A lock of `type` on [4,5), without waiting; 0 if it is given. */
static int child_takes(int type) {
    return lockop(open(path, O_RDWR), F_SETLK, (short)type, 4, 1) == 0 ? 0 : 1;
}

/* Wait for a write lock on [4,5); 0 if it came, and only after a wait. */
static int child_waits(int min_ms) {
    int fd = open(path, O_RDWR);
    long t0 = now_ms();

    if (lockop(fd, F_SETLKW, F_WRLCK, 4, 1) != 0) return 1;
    return (now_ms() - t0 >= min_ms) ? 0 : 2;
}

static void on_alarm(int sig) { (void)sig; }

/* Wait for a lock that will not come, with an alarm set; 0 on EINTR. */
static int child_interrupted(int unused) {
    struct sigaction sa;
    int fd = open(path, O_RDWR);

    (void)unused;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm;       /* no SA_RESTART */
    sigaction(SIGALRM, &sa, NULL);
    alarm(1);
    return lockop(fd, F_SETLKW, F_WRLCK, 4, 1) == EINTR ? 0 : 1;
}

/* Take [20,21) and leave by exit, the lock still held. */
static int child_exits_holding(int unused) {
    (void)unused;
    return lockop(open(path, O_RDWR), F_SETLK, F_WRLCK, 20, 1) == 0 ? 0 : 1;
}

int main(void) {
    int fd, fd2, p[2], st, e;
    pid_t pid;
    char c;

    unlink(path);
    fd = open(path, O_RDWR | O_CREAT, 0644);
    check(fd >= 0 && write(fd, "0123456789abcdefghijklmnopqrstuvwxyz", 36) == 36,
          "a file to lock");

    check(lockop(fd, F_SETLK, F_WRLCK, 2, 6) == 0, "a write lock on bytes 2 to 7");
    check(lockop(fd, F_GETLK, F_WRLCK, 0, 10) == 0, "its owner is not in its own way");

    check(in_child(child_probe, fd) == 0,
          "a child sees it as another's, through the descriptor it inherited");
    check(in_child(child_probe, -1) == 0,
          "and through a descriptor it opened itself");
    check(in_child(child_refused, fd) == 0, "it is refused the bytes, by the first");
    check(in_child(child_refused, -1) == 0, "and by the second");

    /* F_SETLKW: the child waits until the lock is taken off. */
    pid = fork();
    if (pid == 0) _exit(child_waits(200));
    nap_ms(400);
    check(lockop(fd, F_SETLK, F_UNLCK, 2, 6) == 0, "the lock is taken off");
    st = -1;
    waitpid(pid, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
          "F_SETLKW waited for that, and then had the lock");

    /* Read locks are shared, and keep a writer out. */
    check(lockop(fd, F_SETLK, F_RDLCK, 4, 1) == 0, "a read lock");
    check(in_child(child_takes, F_RDLCK) == 0, "another process reads beside it");
    check(in_child(child_takes, F_WRLCK) != 0, "and none writes");

    /* Unlocking the middle of a lock leaves both ends. */
    check(lockop(fd, F_SETLK, F_WRLCK, 0, 10) == 0, "a write lock on bytes 0 to 9");
    check(lockop(fd, F_SETLK, F_UNLCK, 4, 1) == 0, "byte 4 of it unlocked");
    check(in_child(child_takes, F_WRLCK) == 0, "which another process may now have");
    check(in_child(child_takes, F_WRLCK) == 0, "and a third, the second having exited");
    lockop(fd, F_SETLK, F_UNLCK, 0, 0);

    /* A wait that a signal ends. */
    check(lockop(fd, F_SETLK, F_WRLCK, 4, 1) == 0, "a lock that will not be given up");
    check(in_child(child_interrupted, 0) == 0, "a signal ends the wait for it: EINTR");
    lockop(fd, F_SETLK, F_UNLCK, 0, 0);

    /* Closing ANY descriptor for the file gives up the process's locks. */
    check(lockop(fd, F_SETLK, F_WRLCK, 4, 1) == 0, "a lock set through one descriptor");
    fd2 = open(path, O_RDONLY);
    close(fd2);
    check(in_child(child_takes, F_WRLCK) == 0,
          "is gone when the process closes another descriptor for the file");

    /* A process's locks go when it does. */
    check(in_child(child_exits_holding, 0) == 0, "a child locks byte 20 and exits");
    check(lockop(fd, F_SETLK, F_WRLCK, 20, 1) == 0, "and the byte is free");
    lockop(fd, F_SETLK, F_UNLCK, 0, 0);

    /* Deadlock: the parent holds byte 30 and the child byte 31; the child
     * waits for 30, and the parent then asks to wait for 31. */
    check(lockop(fd, F_SETLK, F_WRLCK, 30, 1) == 0, "the parent holds byte 30");
    if (pipe(p) != 0) return 1;
    pid = fork();
    if (pid == 0) {
        int cfd = open(path, O_RDWR);

        close(p[0]);
        if (lockop(cfd, F_SETLK, F_WRLCK, 31, 1) != 0) _exit(1);
        if (write(p[1], "x", 1) != 1) _exit(2);
        _exit(lockop(cfd, F_SETLKW, F_WRLCK, 30, 1) == 0 ? 0 : 3);
    }
    close(p[1]);
    check(read(p[0], &c, 1) == 1, "the child holds byte 31, and waits for 30");
    nap_ms(300);
    e = lockop(fd, F_SETLKW, F_WRLCK, 31, 1);
    check(e == EDEADLK, "the parent's wait for 31 is refused: EDEADLK");
    lockop(fd, F_SETLK, F_UNLCK, 30, 1);
    st = -1;
    waitpid(pid, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "and the child has 30 when it is let go");

    close(fd);
    unlink(path);
    printf("advlock: %s\n", failed ? "FAILED" : "PASS");
    return failed;
}
