/*
 * utimeperm -- who may set a file's times (utimensat(2), utimes(2)).
 *
 * POSIX: setting a time to a value of the caller's choosing takes the
 * file's owner or privilege (EPERM); setting both to now (times NULL, or
 * UTIME_NOW) the owner, privilege or write permission (EACCES).
 * kern_utimensat() checked nothing, so any process could set any file's
 * times.
 *
 * Run as root: a child dropped to uid 1000 tries a root file of mode 0644
 * (no write permission: EPERM for a time, EACCES for now), one of mode
 * 0666 (now allowed, a time still EPERM) and its own file (both allowed).
 * On a Linux host run as root the same lines must read ok.
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

static time_t mtime_of(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? st.st_mtime : (time_t)-1;
}

static int make(const char *path, mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return -1;
    close(fd);
    return chmod(path, mode);
}

int main(void) {
    const char *ro = "/tmp/utimeperm.ro";
    const char *rw = "/tmp/utimeperm.rw";
    struct timespec past[2] = { { 1000000, 0 }, { 1000000, 0 } };

    setvbuf(stdout, NULL, _IONBF, 0);
    check(getuid() == 0, "running as root");
    mkdir("/tmp", 01777);
    chmod("/tmp", 01777);
    check(make(ro, 0644) == 0 && make(rw, 0666) == 0, "make root files 0644 and 0666");
    utimensat(AT_FDCWD, ro, past, 0);
    utimensat(AT_FDCWD, rw, past, 0);

    pid_t pid = fork();
    if (pid == 0) {
        const char *mine = "/tmp/utimeperm.mine";
        int r = 0;
        if (setuid(1000) != 0) _exit(100);
        errno = 0;
        if (utimensat(AT_FDCWD, ro, past, 0) != -1 || errno != EPERM) r |= 1;
        errno = 0;
        if (utimensat(AT_FDCWD, ro, NULL, 0) != -1 || errno != EACCES) r |= 2;
        if (utimensat(AT_FDCWD, rw, NULL, 0) != 0) r |= 4;
        errno = 0;
        if (utimensat(AT_FDCWD, rw, past, 0) != -1 || errno != EPERM) r |= 8;
        if (make(mine, 0600) != 0 || utimensat(AT_FDCWD, mine, past, 0) != 0 ||
            utimensat(AT_FDCWD, mine, NULL, 0) != 0) r |= 16;
        unlink(mine);
        _exit(r);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    int r = WIFEXITED(st) ? WEXITSTATUS(st) : 255;
    check(r != 100, "child dropped to uid 1000");
    check(!(r & 1), "uid 1000 setting a root 0644 file's times: EPERM");
    check(!(r & 2), "uid 1000 setting a root 0644 file's times to now: EACCES");
    check(mtime_of(ro) == 1000000, "the root 0644 file's times are intact");
    check(!(r & 4), "uid 1000 setting a root 0666 file's times to now: allowed");
    check(!(r & 8), "uid 1000 setting a root 0666 file's times: EPERM");
    check(!(r & 16), "uid 1000 setting its own file's times: allowed");

    unlink(ro);
    unlink(rw);
    printf("%s\n", failed ? "utimeperm: FAIL" : "utimeperm: PASS");
    return failed;
}
