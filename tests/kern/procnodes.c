/*
 * procnodes — /proc nodes held open stay those of what was opened.
 *
 *   A. /proc/<pid>/status held open across the reaping of <pid>: the
 *      node block was freed under the open file, so after other pids'
 *      blocks reused the memory a read showed another process.  Now
 *      reads give EOF and nothing is freed under the holder.
 *   B. /proc/<A>/task/<A>/status held open while /proc/<B>/task/<B>/status
 *      is looked up: the four task nodes were statics shared by every
 *      process and rewritten on each lookup, so the open file went on to
 *      read B.  It must still read A.
 *   C. WaitChan (a kernel address) in a task status file is shown only to
 *      those who may inspect the process: an unprivileged reader of
 *      init's sees zero.
 *
 * Run as root.  Runs on the host too, where A, B and C also hold.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHURN 40

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

/* A child that waits to be killed. */
static pid_t sleeper(void)
{
    pid_t pid = fork();

    if (pid == 0) {
        for (;;)
            pause();
    }
    return pid;
}

/* The value of `field` ("Pid:", "WaitChan:") in status text, or -1. */
static long long field_of(const char *text, const char *field, int base)
{
    const char *s = strstr(text, field);

    if (!s)
        return -1;
    return strtoll(s + strlen(field), NULL, base);
}

/* A thread id of `pid` (its only one, for a single-threaded process); a
 * tid need not equal the pid. */
static int first_tid(pid_t pid)
{
    char path[64];
    struct dirent *de;
    DIR *d;
    int tid = -1;

    snprintf(path, sizeof(path), "/proc/%d/task", (int)pid);
    d = opendir(path);
    if (!d)
        return -1;
    while ((de = readdir(d)) != NULL)
        if (de->d_name[0] >= '0' && de->d_name[0] <= '9') {
            tid = atoi(de->d_name);
            break;
        }
    closedir(d);
    return tid;
}

static ssize_t read_all(int fd, char *buf, size_t sz)
{
    ssize_t n = pread(fd, buf, sz - 1, 0);

    buf[n > 0 ? n : 0] = '\0';
    return n;
}

static void test_reaped(void)
{
    char path[64], buf[2048];
    pid_t victim = sleeper(), kids[CHURN];
    int fd, i;
    ssize_t n;

    snprintf(path, sizeof(path), "/proc/%d/status", (int)victim);
    fd = open(path, O_RDONLY);
    check(fd >= 0, "A: open /proc/<child>/status");
    if (fd < 0)
        return;
    kill(victim, SIGKILL);
    waitpid(victim, NULL, 0);

    /* Other pids' node blocks, to take over freed memory. */
    for (i = 0; i < CHURN; i++) {
        struct stat st;

        kids[i] = sleeper();
        snprintf(path, sizeof(path), "/proc/%d/status", (int)kids[i]);
        stat(path, &st);
    }

    n = read_all(fd, buf, sizeof(buf));
    long long pid = field_of(buf, "Pid:", 10);
    check(n <= 0 || pid == victim,
          "A: the reaped pid's file shows no other process");
    if (n > 0 && pid != victim)
        printf("      read %zd bytes, Pid %lld (victim %d)\n", n, pid,
               (int)victim);
    close(fd);

    for (i = 0; i < CHURN; i++) {
        kill(kids[i], SIGKILL);
        waitpid(kids[i], NULL, 0);
    }
}

static void test_task_alias(void)
{
    char path[80], buf[2048];
    pid_t a = sleeper(), b = sleeper();
    struct stat st;
    int fd;

    int ta = first_tid(a), tb = first_tid(b);

    snprintf(path, sizeof(path), "/proc/%d/task/%d/status", (int)a, ta);
    fd = open(path, O_RDONLY);
    check(fd >= 0, "B: open A's task status");
    if (fd >= 0) {
        snprintf(path, sizeof(path), "/proc/%d/task/%d/status", (int)b, tb);
        check(stat(path, &st) == 0, "B: look up B's task status");
        read_all(fd, buf, sizeof(buf));
        /* "Pid:" in a task status file is the thread's id. */
        long long tid = field_of(buf, "Pid:", 10);
        check(tid == ta, "B: it still reads A after B is looked up");
        if (tid != ta)
            printf("      Pid %lld (A's tid %d, B's tid %d)\n", tid, ta, tb);
        close(fd);
    }
    kill(a, SIGKILL);
    kill(b, SIGKILL);
    waitpid(a, NULL, 0);
    waitpid(b, NULL, 0);
}

static void test_waitchan(void)
{
    int pfd[2], status;
    long long wc = -2;
    pid_t pid;

    int tid1 = first_tid(1);

    if (pipe(pfd) != 0)
        return;
    pid = fork();
    if (pid == 0) {
        char buf[2048], path[64];
        int fd;

        close(pfd[0]);
        if (setgid(65534) != 0 || setuid(65534) != 0)
            _exit(2);
        snprintf(path, sizeof(path), "/proc/1/task/%d/status", tid1);
        fd = open(path, O_RDONLY);
        if (fd < 0)
            _exit(3);
        read_all(fd, buf, sizeof(buf));
        wc = field_of(buf, "WaitChan:", 16);
        if (write(pfd[1], &wc, sizeof(wc)) != (ssize_t)sizeof(wc))
            _exit(4);
        _exit(0);
    }
    close(pfd[1]);
    if (read(pfd[0], &wc, sizeof(wc)) != (ssize_t)sizeof(wc))
        wc = -2;
    close(pfd[0]);
    waitpid(pid, &status, 0);
    check(wc == 0 || wc == -1,
          "C: an unprivileged reader sees no WaitChan address for init");
    if (wc != 0 && wc != -1)
        printf("      WaitChan %llx\n", wc);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_reaped();
    test_task_alias();
    test_waitchan();
    printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED");
    return fails ? 1 : 0;
}
