#include <unistd.h>
#include <string.h>
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
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <termios.h>
#include <sys/sysctl.h>

extern int64_t _syscall0(int);
extern int64_t _syscall1(int, int);
extern int64_t _syscall2(int, int, int);
extern int64_t _syscall3(int, int, int, int);
extern int64_t _syscall4(int, int, int, int, int);
extern int64_t _syscall5(int, int, int, int, int, int);
extern int64_t _syscall6(int, int, int, int, int, int, int);

#undef errno
int errno = 0;
char **environ = NULL;

/* Provide __errno_location for code compiled with __linux__ defined */
int *__errno_location(void) { return &errno; }
/* FreeBSD/DragonFly compat */
int *__error(void) { return &errno; }
/* NetBSD/OpenBSD compat */
int *__errno(void) { return &errno; }

/* Convert kernel-style negative error returns to errno convention */
static inline int __set_errno(int r) {
    if (r < 0) {
        errno = -r;
        return -1;
    }
    return r;
}

void _exit(int status) {
    _syscall1(SYS_EXIT, status);
    while(1);
}

int fork(void) {
    return __set_errno((int)_syscall0(SYS_FORK));
}

ssize_t read(int fd, void *buf, size_t count) {
    int64_t r = _syscall3(SYS_READ, fd, (int)buf, (int)count);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (ssize_t)r;
}

ssize_t write(int fd, const void *buf, size_t count) {
    int64_t r = _syscall3(SYS_WRITE, fd, (int)buf, (int)count);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (ssize_t)r;
}

int close(int fd) {
    return __set_errno((int)_syscall1(SYS_CLOSE, fd));
}

int open(const char *pathname, int flags, ...) {
    int mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return __set_errno((int)_syscall3(SYS_OPEN, (int)pathname, flags, mode));
}

int unlink(const char *pathname) {
    return __set_errno((int)_syscall1(SYS_UNLINK, (int)pathname));
}

int link(const char *oldpath, const char *newpath) {
    return __set_errno((int)_syscall2(SYS_LINK, (int)oldpath, (int)newpath));
}

int symlink(const char *target, const char *linkpath) {
    return __set_errno((int)_syscall2(SYS_SYMLINK, (int)target, (int)linkpath));
}

int execve(const char *filename, char *const argv[], char *const envp[]) {
    return __set_errno((int)_syscall3(SYS_EXECVE, (int)filename, (int)argv, (int)envp));
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
    return __set_errno((int)_syscall1(SYS_CHDIR, (int)path));
}

char *getcwd(char *buf, size_t size) {
    int ret = (int)_syscall2(SYS_GETCWD, (int)buf, size);
    if (ret < 0) { errno = -ret; return NULL; }
    return buf;
}

pid_t getpid(void) {
    return (pid_t)_syscall0(SYS_GETPID);
}

pid_t getppid(void) {
    return (pid_t)_syscall0(SYS_GETPPID);
}

uid_t getuid(void) { return (uid_t)_syscall0(SYS_GETUID); }
gid_t getgid(void) { return (gid_t)_syscall0(SYS_GETGID); }
uid_t geteuid(void) { return (uid_t)_syscall0(SYS_GETEUID); }
gid_t getegid(void) { return (gid_t)_syscall0(SYS_GETEGID); }
int setuid(uid_t uid) { return __set_errno((int)_syscall1(SYS_SETUID, uid)); }
int setgid(gid_t gid) { return __set_errno((int)_syscall1(SYS_SETGID, gid)); }

int pipe(int pipefd[2]) {
    return __set_errno((int)_syscall1(SYS_PIPE, (int)pipefd));
}

// Forward declaration
int ioctl(int fd, unsigned long request, ...);

int dup2(int oldfd, int newfd) {
    return __set_errno((int)_syscall2(SYS_DUP2, oldfd, newfd));
}

int dup(int oldfd) {
    return __set_errno((int)_syscall1(SYS_DUP, oldfd));
}

int tcsetpgrp(int fd, pid_t pgrp) {
    return ioctl(fd, TIOCSPGRP, &pgrp);
}

pid_t tcgetpgrp(int fd) {
    pid_t pgrp;
    if (ioctl(fd, TIOCGPGRP, &pgrp) < 0) return -1;
    return pgrp;
}

int tcgetattr(int fd, struct termios *termios_p) {
    return ioctl(fd, TCGETS, termios_p);
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
    (void)optional_actions;
    return ioctl(fd, 0x5402, termios_p); 
}

extern int setpgid(pid_t pid, pid_t pgid);
extern mode_t umask(mode_t mask);
typedef void (*sig_t)(int);

int setpgid(pid_t pid, pid_t pgid) {
    return __set_errno((int)_syscall2(SYS_SETPGID, (int)pid, (int)pgid));
}

pid_t getpgid(pid_t pid) {
    return (pid_t)__set_errno((int)_syscall1(SYS_GETPGID, (int)pid));
}

pid_t getpgrp(void) {
    return getpgid(0);
}

void sync(void) {
    _syscall0(SYS_SYNC);
}

int kill(pid_t pid, int sig) {
    int ret = (int)_syscall2(SYS_KILL, pid, sig);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

sighandler_t signal(int signum, sighandler_t handler) {
    return (sighandler_t)(uintptr_t)_syscall2(SYS_SIGNAL, signum, (int)handler);
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
    return __set_errno((int)_syscall3(SYS_SIGACTION, sig, (int)act, (int)oact));
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oset) {
    return __set_errno((int)_syscall3(SYS_SIGPROCMASK, how, (int)set, (int)oset));
}

int sigpending(sigset_t *set) {
    return __set_errno((int)_syscall1(SYS_SIGPENDING, (int)set));
}

int sigsuspend(const sigset_t *mask) {
    return __set_errno((int)_syscall1(SYS_SIGSUSPEND, (int)mask));
}

mode_t umask(mode_t mask) {
    return (mode_t)_syscall1(SYS_UMASK, (int)mask);
}

int sigemptyset(sigset_t *set) {
    if (!set) { errno = EINVAL; return -1; }
    *set = 0;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (!set) { errno = EINVAL; return -1; }
    *set = 0xFFFFFFFF;
    return 0;
}

int sigaddset(sigset_t *set, int signo) {
    if (!set || signo <= 0 || signo > 32) { errno = EINVAL; return -1; }
    *set |= (1U << (signo - 1));
    return 0;
}

int sigdelset(sigset_t *set, int signo) {
    if (!set || signo <= 0 || signo > 32) { errno = EINVAL; return -1; }
    *set &= ~(1U << (signo - 1));
    return 0;
}

int sigismember(const sigset_t *set, int signo) {
    if (!set || signo <= 0 || signo > 32) { errno = EINVAL; return -1; }
    return (*set & (1U << (signo - 1))) != 0;
}

/* ... */

off_t lseek(int fd, off_t offset, int whence) {
    uint32_t off_lo = (uint32_t)(offset & 0xFFFFFFFF);
    uint32_t off_hi = (uint32_t)((offset >> 32) & 0xFFFFFFFF);
    int64_t r = _syscall4(SYS_LSEEK, fd, off_lo, off_hi, whence);
    if (r < 0) { errno = (int)(-r); return (off_t)-1; }
    return (off_t)r;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    // We pass offset in 4096-byte units (page offset) to the kernel
    // to allow mapping > 4GB files with 32-bit registers.
    // Check if offset is page aligned
    if (offset & 0xFFF) {
        errno = EINVAL;
        return (void *)-1;
    }
    uint32_t pgoff = (uint32_t)(offset >> 12);
    
    return (void *)(uintptr_t)_syscall6(SYS_MMAP, (int)addr, (int)length, prot, flags, fd, (int)pgoff);
}

int munmap(void *addr, size_t length) {
    return __set_errno((int)_syscall2(SYS_MUNMAP, (int)addr, (int)length));
}

pid_t waitpid(pid_t pid, int *status, int options) {
    int64_t r = _syscall3(SYS_WAITPID, pid, (int)status, options);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (pid_t)r;
}

pid_t wait(int *status) {
    return waitpid(-1, status, 0);
}

int access(const char *pathname, int mode) {
    return __set_errno((int)_syscall2(SYS_ACCESS, (int)pathname, mode));
}

int stat(const char *pathname, struct stat *buf) {
    return __set_errno((int)_syscall2(SYS_STAT, (int)pathname, (int)buf));
}

int lstat(const char *pathname, struct stat *buf) {
    return __set_errno((int)_syscall2(SYS_LSTAT, (int)pathname, (int)buf));
}

int fstat(int fd, struct stat *buf) {
    return __set_errno((int)_syscall2(SYS_FSTAT, fd, (int)buf));
}

int chown(const char *pathname, uid_t owner, gid_t group) {
    return __set_errno((int)_syscall3(SYS_LCHOWN, (int)pathname, owner, group));
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
    int64_t r = _syscall3(SYS_READLINK, (int)pathname, (int)buf, bufsiz);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (ssize_t)r;
}

time_t time(time_t *tloc) {
    return (time_t)_syscall1(SYS_TIME, (int)tloc);
}

unsigned int alarm(unsigned int seconds) {
    return (unsigned int)_syscall1(SYS_ALARM, seconds);
}

int getitimer(int which, struct itimerval *curr_value) {
    return __set_errno((int)_syscall2(SYS_GETITIMER, which, (int)curr_value));
}

int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value) {
    return __set_errno((int)_syscall3(SYS_SETITIMER, which, (int)new_value, (int)old_value));
}

clock_t times(struct tms *buf) {
    return (clock_t)_syscall1(SYS_TIMES, (int)buf);
}

int mkdir(const char *pathname, mode_t mode) {
    return __set_errno((int)_syscall2(SYS_MKDIR, (int)pathname, mode));
}

int rmdir(const char *pathname) {
    return __set_errno((int)_syscall1(SYS_RMDIR, (int)pathname));
}

int mknod(const char *pathname, mode_t mode, dev_t dev) {
    return __set_errno((int)_syscall3(SYS_MKNOD, (int)pathname, mode, dev));
}

int chmod(const char *pathname, mode_t mode) {
    return __set_errno((int)_syscall2(SYS_CHMOD, (int)pathname, mode));
}

int utimes(const char *filename, const struct timeval times[2]) {
    (void)filename; (void)times;
    errno = ENOSYS;
    return -1;
}
int mount(const char *source, const char *target, const char *filesystemtype, unsigned long mountflags, const void *data) {
    return __set_errno((int)_syscall5(SYS_MOUNT, (int)source, (int)target, (int)filesystemtype, (int)mountflags, (int)data));
}

int umount(const char *target) {
    return __set_errno((int)_syscall1(SYS_UMOUNT, (int)target));
}

int rename(const char *oldpath, const char *newpath) {
    return __set_errno((int)_syscall2(SYS_RENAME, (int)oldpath, (int)newpath));
}

int ftruncate(int fd, off_t length) {
    uint32_t len_lo = (uint32_t)(length & 0xFFFFFFFF);
    uint32_t len_hi = (uint32_t)((length >> 32) & 0xFFFFFFFF);
    return __set_errno((int)_syscall4(SYS_FTRUNCATE, fd, len_lo, len_hi, 0));
}

int truncate(const char *path, off_t length) {
    uint32_t len_lo = (uint32_t)(length & 0xFFFFFFFF);
    uint32_t len_hi = (uint32_t)((length >> 32) & 0xFFFFFFFF);
    return __set_errno((int)_syscall5(SYS_TRUNCATE, (int)path, len_lo, len_hi, 0, 0));
}

int uname(struct utsname *buf) {
    return __set_errno((int)_syscall1(SYS_UNAME, (int)buf));
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    return __set_errno((int)_syscall2(SYS_NANOSLEEP, (int)req, (int)rem));
}

unsigned int sleep(unsigned int seconds) {
    struct timespec req = { .tv_sec = (time_t)seconds, .tv_nsec = 0 };
    struct timespec rem = { 0 };
    if (nanosleep(&req, &rem) == 0) return 0;
    return (unsigned int)rem.tv_sec;
}

int usleep(useconds_t usec) {
    struct timespec req = { .tv_sec = (time_t)(usec / 1000000), .tv_nsec = (long)((usec % 1000000) * 1000) };
    return nanosleep(&req, NULL);
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    return __set_errno((int)_syscall2(SYS_CLOCK_GETTIME, clk_id, (int)tp));
}

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    return __set_errno((int)_syscall3(SYS_IOCTL, fd, request, (int)arg));
}

int isatty(int fd) {
    struct termios ios;
    return ioctl(fd, TCGETS, &ios) == 0;
}

char *ttyname(int fd) {
    if (!isatty(fd)) return NULL;
    static char buf[32];
    sprintf(buf, "/dev/tty%d", fd);
    return buf;
}

int gethostname(char *name, size_t len) {
    struct utsname u;
    if (uname(&u) < 0) return -1;
    strncpy(name, u.nodename, len);
    name[len - 1] = '\0';
    return 0;
}

int sethostname(const char *name, size_t len) {
    (void)name; (void)len;
    errno = ENOSYS;
    return -1;
}

int futex(int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2, int val3) {
    return __set_errno((int)_syscall6(240, (int)uaddr, op, val, (int)timeout, (int)uaddr2, val3));
}

int getpriority(int which, id_t who) {
    int ret = (int)_syscall2(SYS_GETPRIORITY, which, who);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret - 20;
}

int setpriority(int which, id_t who, int prio) {
    return __set_errno((int)_syscall3(SYS_SETPRIORITY, which, who, prio));
}

