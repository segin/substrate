/*
 * truncperm — truncate(2) needs write permission on the file.
 *
 * sys_truncate looked the path up and truncated it, checking neither the
 * caller's permission nor whether it named a directory, so any process
 * could empty a root-owned file such as /etc/passwd.
 *
 * Run as root: a child that drops to uid 1000 tries to truncate a root
 * file of mode 0644 (EACCES, file intact), its own file (allowed) and a
 * directory (EISDIR).  On a Linux host run as root the same lines must
 * read ok.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

static off_t size_of(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? st.st_size : -1;
}

int main(void) {
    const char *rootf = "/tmp/truncperm.root";
    const char *dir = "/tmp/truncperm.dir";
    setvbuf(stdout, NULL, _IONBF, 0);
    check(getuid() == 0, "running as root");

    mkdir("/tmp", 01777);
    chmod("/tmp", 01777);
    int fd = open(rootf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    check(fd >= 0 && write(fd, "secret\n", 7) == 7, "make a root file, 0644");
    if (fd >= 0) close(fd);
    chmod(rootf, 0644);
    mkdir(dir, 0777);
    chmod(dir, 0777);

    pid_t pid = fork();
    if (pid == 0) {
        const char *mine = "/tmp/truncperm.mine";
        int r = 0;
        if (setuid(1000) != 0) _exit(100);
        errno = 0;
        if (truncate(rootf, 0) != -1 || errno != EACCES) r |= 1;
        int mfd = open(mine, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (mfd < 0 || write(mfd, "mine\n", 5) != 5) r |= 2;
        if (mfd >= 0) close(mfd);
        if (truncate(mine, 1) != 0 || size_of(mine) != 1) r |= 2;
        unlink(mine);
        errno = 0;
        if (truncate(dir, 0) != -1 || errno != EISDIR) r |= 4;
        _exit(r);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    int r = WIFEXITED(st) ? WEXITSTATUS(st) : 255;
    check(r != 100, "child dropped to uid 1000");
    check(!(r & 1), "uid 1000 truncating a root 0644 file: EACCES");
    check(size_of(rootf) == 7, "the root file is intact");
    check(!(r & 2), "uid 1000 truncating its own file: allowed");
    check(!(r & 4), "truncating a directory: EISDIR");

    unlink(rootf);
    rmdir(dir);
    printf("%s\n", failed ? "truncperm: FAIL" : "truncperm: PASS");
    return failed;
}
