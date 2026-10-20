/*
 * ptsrace — looking up /dev/pts/N while ptys come and go.
 *
 * A pty pair's slave node was freed (with the pair) when both ends
 * closed, and a path walk that had just found /dev/pts/N in devfs held it
 * with no reference: a stat racing the close read freed memory.  The node
 * now outlives its pair while a lookup holds it.
 *
 * One process opens and closes ptys as fast as it can; three stat and
 * lstat /dev/pts/0..7 and read the directory.  They must finish and the
 * kernel must survive.
 *
 * Usage: ptsrace [seconds]   (default 20).  Runs on the host too.
 */
#define _XOPEN_SOURCE 600
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void churn(void)
{
    for (;;) {
        int m = posix_openpt(O_RDWR | O_NOCTTY);
        if (m < 0) continue;
        if (grantpt(m) == 0 && unlockpt(m) == 0) {
            char *n = ptsname(m);
            int s = n ? open(n, O_RDWR | O_NOCTTY) : -1;
            if (s >= 0) close(s);
        }
        close(m);
    }
}

static void look(time_t end)
{
    char p[32];
    struct stat st;
    long n = 0;

    while (time(NULL) < end) {
        for (int i = 0; i < 8; i++) {
            snprintf(p, sizeof(p), "/dev/pts/%d", i);
            stat(p, &st);
            lstat(p, &st);
        }
        DIR *d = opendir("/dev/pts");
        if (d) {
            while (readdir(d))
                ;
            closedir(d);
        }
        n++;
    }
    printf("looker %d: %ld rounds\n", (int)getpid(), n);
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 20;
    time_t end = time(NULL) + secs;
    pid_t c = fork();

    if (c == 0) {
        churn();
        _exit(0);
    }
    pid_t l[3];
    for (int i = 0; i < 3; i++) {
        l[i] = fork();
        if (l[i] == 0) {
            look(end);
            _exit(0);
        }
    }
    int bad = 0;
    for (int i = 0; i < 3; i++) {
        int status;
        waitpid(l[i], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) bad = 1;
    }
    kill(c, SIGKILL);
    waitpid(c, NULL, 0);
    printf("ptsrace: %s\n", bad ? "FAIL" : "PASS");
    return bad;
}
