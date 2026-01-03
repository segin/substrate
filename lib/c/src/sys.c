#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>

extern int64_t _syscall0(int);
extern int64_t _syscall1(int, int);
extern int64_t _syscall2(int, int, int);
extern int64_t _syscall3(int, int, int, int);
extern int64_t _syscall4(int, int, int, int, int);
extern int64_t _syscall5(int, int, int, int, int, int);

int errno = 0;
char **environ = NULL;

void _exit(int status) {
    _syscall1(SYS_EXIT, status);
    while(1);
}

int fork(void) {
    return (int)_syscall0(SYS_FORK);
}

ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)_syscall3(SYS_READ, fd, (int)buf, (int)count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)_syscall3(SYS_WRITE, fd, (int)buf, (int)count);
}

int close(int fd) {
    return (int)_syscall1(SYS_CLOSE, fd);
}

int open(const char *pathname, int flags, ...) {
    int mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return (int)_syscall3(SYS_OPEN, (int)pathname, flags, mode);
}

int unlink(const char *pathname) {
    return (int)_syscall1(SYS_UNLINK, (int)pathname);
}

int link(const char *oldpath, const char *newpath) {
    return (int)_syscall2(SYS_LINK, (int)oldpath, (int)newpath);
}

int execve(const char *filename, char *const argv[], char *const envp[]) {
    return (int)_syscall3(SYS_EXECVE, (int)filename, (int)argv, (int)envp);
}

int execv(const char *filename, char *const argv[]) {
    return execve(filename, argv, environ);
}

int execvp(const char *file, char *const argv[]) {
    // Stub: no PATH search yet
    return execv(file, argv);
}

int execl(const char *path, const char *arg, ...) {
    va_list ap;
    int argc = 0;
    va_start(ap, arg);
    while (va_arg(ap, char *)) argc++;
    va_end(ap);

    char **argv = malloc((argc + 2) * sizeof(char *));
    argv[0] = (char *)arg;
    va_start(ap, arg);
    for (int i = 1; i <= argc; i++) argv[i] = va_arg(ap, char *);
    argv[argc + 1] = NULL;
    va_end(ap);

    int ret = execv(path, argv);
    free(argv);
    return ret;
}

int chdir(const char *path) {
    return (int)_syscall1(SYS_CHDIR, (int)path);
}

char *getcwd(char *buf, size_t size) {
    int ret = (int)_syscall2(SYS_GETCWD, (int)buf, size);
    if (ret < 0) return NULL;
    return buf;
}

pid_t getpid(void) {
    return (pid_t)_syscall0(SYS_GETPID);
}

uid_t getuid(void) { return (uid_t)_syscall0(SYS_GETUID); }
gid_t getgid(void) { return (gid_t)_syscall0(SYS_GETGID); }
uid_t geteuid(void) { return (uid_t)_syscall0(SYS_GETEUID); }
gid_t getegid(void) { return (gid_t)_syscall0(SYS_GETEGID); }
int setuid(uid_t uid) { return (int)_syscall1(SYS_SETUID, uid); }
int setgid(gid_t gid) { return (int)_syscall1(SYS_SETGID, gid); }

int pipe(int pipefd[2]) {
    return (int)_syscall1(SYS_PIPE, (int)pipefd);
}

int dup2(int oldfd, int newfd) {
    return (int)_syscall2(SYS_DUP2, oldfd, newfd);
}

void sync(void) {
    _syscall0(SYS_SYNC);
}

int kill(pid_t pid, int sig) {
    return (int)_syscall2(SYS_KILL, pid, sig);
}

sighandler_t signal(int signum, sighandler_t handler) {
    return (sighandler_t)(uintptr_t)_syscall2(SYS_SIGNAL, signum, (int)handler);
}

/* ... */

off_t lseek(int fd, off_t offset, int whence) {
    uint32_t off_lo = (uint32_t)(offset & 0xFFFFFFFF);
    uint32_t off_hi = (uint32_t)((offset >> 32) & 0xFFFFFFFF);
    return (off_t)_syscall4(SYS_LSEEK, fd, off_lo, off_hi, whence);
}