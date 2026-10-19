/*
 * rmdirwalk — path walks through a directory that is being removed.
 *
 * ext2 finishes deleting a directory removed while open at its last
 * close, clearing the node's filesystem pointer.  A path walk holding
 * that node (walks do not pin what they pass through) went on to call
 * ext2_finddir on it, which dereferenced the cleared pointer: a kernel
 * NULL dereference.  The operations on such a node now fail with ENOENT.
 *
 * One process repeatedly makes a directory, opens it, removes it and
 * closes it; another walks paths through it (stat, open, readlink,
 * mkdir inside it).  Both must finish and the kernel must survive.
 *
 * Usage: rmdirwalk [seconds]   (default 20).  Runs on the host too.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DIR_NAME "/tmp/rmdirwalk.d"

static void churn(void)
{
    for (;;) {
        if (mkdir(DIR_NAME, 0755) == 0) {
            int fd = open(DIR_NAME, O_RDONLY);
            rmdir(DIR_NAME);            /* removed while open ... */
            if (fd >= 0)
                close(fd);              /* ... finished at the close */
        }
    }
}

static void walk(void)
{
    struct stat st;
    char buf[64];

    for (;;) {
        stat(DIR_NAME "/x", &st);
        int fd = open(DIR_NAME "/x/y", O_RDONLY);
        if (fd >= 0)
            close(fd);
        readlink(DIR_NAME "/x", buf, sizeof(buf));
        if (mkdir(DIR_NAME "/x", 0755) == 0)
            rmdir(DIR_NAME "/x");
    }
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 20;
    pid_t a, b;
    int sa = 0, sb = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    rmdir(DIR_NAME "/x");
    rmdir(DIR_NAME);
    a = fork();
    if (a == 0)
        churn();
    b = fork();
    if (b == 0)
        walk();
    sleep(secs);
    kill(a, SIGKILL);
    kill(b, SIGKILL);
    waitpid(a, &sa, 0);
    waitpid(b, &sb, 0);
    rmdir(DIR_NAME "/x");
    rmdir(DIR_NAME);
    int ok = WIFSIGNALED(sa) && WTERMSIG(sa) == SIGKILL &&
             WIFSIGNALED(sb) && WTERMSIG(sb) == SIGKILL;
    printf("%s churn and walk ran %d s until stopped\n", ok ? "ok   " : "FAIL ",
           secs);
    if (!ok)
        printf("      status churn 0x%x walk 0x%x\n", sa, sb);
    printf("%s\n", ok ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return ok ? 0 : 1;
}
