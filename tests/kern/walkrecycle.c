/*
 * walkrecycle — a path walk keeps the nodes it passes through.
 *
 * A walk carried bare fs_node_t pointers from one finddir to the next and
 * pinned nothing, and ext2 recycles any unpinned slot of its 256-node
 * cache for the next inode it is asked for.  With other processes looking
 * up enough files to cycle the cache, a walk could find a directory it
 * was standing in rebuilt as some other file: the lookup then failed or
 * named the wrong inode.
 *
 * Walkers stat a deep path and a file through it and check the inode;
 * churners stat 600 other files over and over to keep the cache cycling.
 * Any wrong answer, and any failure, is reported.
 *
 * Usage: walkrecycle [seconds] [dir]   (default 20, /tmp).  Runs on the
 * host too.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NCHURN_FILES 600
#define NWALKERS     3
#define NCHURNERS    3

static char base[200], deep[300], target[320], churn_dir[220];

static void churn(void)
{
    char p[300];
    struct stat st;

    for (;;) {
        for (int i = 0; i < NCHURN_FILES; i++) {
            snprintf(p, sizeof(p), "%s/f%d", churn_dir, i);
            stat(p, &st);
        }
    }
}

/* Exit status: 0 clean, 1 a wrong inode, 2 a failed lookup. */
static int walk(time_t end, ino_t dir_ino, ino_t file_ino)
{
    struct stat st;
    long n = 0, bad = 0, fail = 0;

    while (time(NULL) < end) {
        if (stat(deep, &st) != 0) fail++;
        else if (st.st_ino != dir_ino) bad++;
        if (stat(target, &st) != 0) fail++;
        else if (st.st_ino != file_ino) bad++;
        int fd = open(target, O_RDONLY);
        if (fd < 0) fail++;
        else {
            if (fstat(fd, &st) != 0 || st.st_ino != file_ino) bad++;
            close(fd);
        }
        n++;
    }
    printf("walker %d: %ld rounds, %ld wrong, %ld failed\n",
           (int)getpid(), n, bad, fail);
    return bad ? 1 : fail ? 2 : 0;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 20;
    const char *top = argc > 2 ? argv[2] : "/tmp";
    struct stat st;
    char p[300];

    snprintf(base, sizeof(base), "%s/walkrecycle.%d", top, (int)getpid());
    snprintf(deep, sizeof(deep), "%s/a/b/c/d/e/f/g/h", base);
    snprintf(target, sizeof(target), "%s/target", deep);
    snprintf(churn_dir, sizeof(churn_dir), "%s/churn", base);

    const char *parts[] = { "", "/a", "/a/b", "/a/b/c", "/a/b/c/d",
                            "/a/b/c/d/e", "/a/b/c/d/e/f", "/a/b/c/d/e/f/g",
                            "/a/b/c/d/e/f/g/h", "/churn" };
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        snprintf(p, sizeof(p), "%s%s", base, parts[i]);
        if (mkdir(p, 0755) != 0) {
            perror(p);
            return 3;
        }
    }
    int fd = open(target, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        perror(target);
        return 3;
    }
    close(fd);
    for (int i = 0; i < NCHURN_FILES; i++) {
        snprintf(p, sizeof(p), "%s/f%d", churn_dir, i);
        fd = open(p, O_WRONLY | O_CREAT, 0644);
        if (fd >= 0) close(fd);
    }

    ino_t dir_ino, file_ino;
    stat(deep, &st);
    dir_ino = st.st_ino;
    stat(target, &st);
    file_ino = st.st_ino;

    pid_t churners[NCHURNERS], walkers[NWALKERS];
    for (int i = 0; i < NCHURNERS; i++) {
        churners[i] = fork();
        if (churners[i] == 0) {
            churn();
            _exit(0);
        }
    }
    time_t end = time(NULL) + secs;
    for (int i = 0; i < NWALKERS; i++) {
        walkers[i] = fork();
        if (walkers[i] == 0)
            _exit(walk(end, dir_ino, file_ino));
    }

    int worst = 0;
    for (int i = 0; i < NWALKERS; i++) {
        int status;
        waitpid(walkers[i], &status, 0);
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : 4;
        if (code > worst) worst = code;
    }
    for (int i = 0; i < NCHURNERS; i++) {
        kill(churners[i], SIGKILL);
        waitpid(churners[i], NULL, 0);
    }

    for (int i = 0; i < NCHURN_FILES; i++) {
        snprintf(p, sizeof(p), "%s/f%d", churn_dir, i);
        unlink(p);
    }
    unlink(target);
    for (int i = (int)(sizeof(parts) / sizeof(parts[0])) - 1; i >= 0; i--) {
        snprintf(p, sizeof(p), "%s%s", base, parts[i]);
        rmdir(p);
    }

    printf("walkrecycle: %s\n", worst ? "FAIL" : "PASS");
    return worst;
}
