#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <wchar.h>
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
#include <sys/select.h>
#include <sys/poll.h>

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

int fcntl(int fd, int cmd, ...) {
    long arg = 0;
    va_list ap;
    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);
    return __set_errno((int)_syscall3(SYS_FCNTL, fd, cmd, (int)arg));
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

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout) {
    /* Implement select using poll syscall */
    if (nfds < 0 || nfds > FD_SETSIZE) {
        errno = EINVAL;
        return -1;
    }
    
    /* Count fds and build poll array */
    struct pollfd fds[256];  /* Stack limit */
    int poll_count = 0;
    
    if (nfds > 256) {
        errno = EINVAL;
        return -1;
    }
    
    for (int i = 0; i < nfds; i++) {
        short events = 0;
        if (readfds && FD_ISSET(i, readfds)) events |= POLLIN;
        if (writefds && FD_ISSET(i, writefds)) events |= POLLOUT;
        if (exceptfds && FD_ISSET(i, exceptfds)) events |= POLLERR;
        
        if (events) {
            fds[poll_count].fd = i;
            fds[poll_count].events = events;
            fds[poll_count].revents = 0;
            poll_count++;
        }
    }
    
    /* Convert timeout */
    int poll_timeout = -1;
    if (timeout) {
        poll_timeout = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
    }
    
    /* Call poll */
    int ret = (int)_syscall3(SYS_POLL, (int)fds, poll_count, poll_timeout);
    
    if (ret > 0) {
        /* Convert poll results back to select format */
        fd_set result_read, result_write, result_except;
        if (readfds) FD_ZERO(&result_read);
        if (writefds) FD_ZERO(&result_write);
        if (exceptfds) FD_ZERO(&result_except);
        
        for (int i = 0; i < poll_count; i++) {
            int fd = fds[i].fd;
            short revents = fds[i].revents;
            
            if (revents & POLLIN && readfds) FD_SET(fd, &result_read);
            if (revents & POLLOUT && writefds) FD_SET(fd, &result_write);
            if (revents & POLLERR && exceptfds) FD_SET(fd, &result_except);
        }
        
        if (readfds) *readfds = result_read;
        if (writefds) *writefds = result_write;
        if (exceptfds) *exceptfds = result_except;
    } else if (ret == 0) {
        /* Timeout */
        if (readfds) FD_ZERO(readfds);
        if (writefds) FD_ZERO(writefds);
        if (exceptfds) FD_ZERO(exceptfds);
    } else {
        return __set_errno(ret);
    }
    
    return ret;
}

/* --- Process/file permission syscall wrappers --- */

int fchmod(int fd, mode_t mode) {
    return __set_errno((int)_syscall2(SYS_FCHMOD, fd, (int)mode));
}

int fchown(int fd, uid_t owner, gid_t group) {
    return __set_errno((int)_syscall3(SYS_FCHOWN, fd, (int)owner, (int)group));
}

pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage) {
    pid_t ret = (pid_t)_syscall4(SYS_WAIT4, (int)pid, (int)wstatus,
                                  options, (int)rusage);
    if ((int)ret < 0)
        return (pid_t)__set_errno((int)ret);
    return ret;
}

int getrusage(int who, struct rusage *usage) {
    return __set_errno((int)_syscall2(SYS_GETRUSAGE, who, (int)usage));
}

int getgroups(int size, gid_t list[]) {
    return __set_errno((int)_syscall2(SYS_GETGROUPS, size, (int)list));
}

int setgroups(int size, const gid_t *list) {
    return __set_errno((int)_syscall2(SYS_SETGROUPS, size, (int)list));
}

char *getlogin(void) {
    return NULL; /* stub: no kernel support yet */
}

int getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups) {
    (void)user;
    /* stub: return primary group only */
    if (*ngroups >= 1) {
        groups[0] = group;
        *ngroups = 1;
        return 1;
    }
    *ngroups = 1;
    return -1;
}

/* --- Locale stubs --- */

char *setlocale(int category, const char *locale) {
    (void)category;
    (void)locale;
    return (char *)"C";
}

/* localeconv is in stdlib.c or here */

/* --- Wide-character stubs (ASCII-safe) --- */

int iswspace(wint_t wc) {
    return (wc < 128) && ((wc == ' ') || (wc == '\t') || (wc == '\n') ||
                          (wc == '\r') || (wc == '\f') || (wc == '\v'));
}

int iswalpha(wint_t wc)  { return (wc < 128) && (((wc|32) >= 'a') && ((wc|32) <= 'z')); }
int iswalnum(wint_t wc)  { return (wc < 128) && ((((wc|32) >= 'a') && ((wc|32) <= 'z')) ||
                                                  (wc >= '0' && wc <= '9')); }
int iswdigit(wint_t wc)  { return (wc < 128) && (wc >= '0' && wc <= '9'); }
int iswupper(wint_t wc)  { return (wc < 128) && (wc >= 'A' && wc <= 'Z'); }
int iswlower(wint_t wc)  { return (wc < 128) && (wc >= 'a' && wc <= 'z'); }
int iswprint(wint_t wc)  { return (wc < 128) && (wc >= 0x20 && wc < 0x7f); }
int iswpunct(wint_t wc)  { return (wc < 128) && isgraph((int)wc) && !isalnum((int)wc); }
int iswcntrl(wint_t wc)  { return (wc < 128) && ((wc < 0x20) || (wc == 0x7f)); }
int iswblank(wint_t wc)  { return (wc == ' ') || (wc == '\t'); }
int iswgraph(wint_t wc)  { return (wc < 128) && isgraph((int)wc); }
int iswxdigit(wint_t wc) { return (wc < 128) && isxdigit((int)wc); }

wint_t towlower(wint_t wc) { return (wc < 128) ? (wint_t)tolower((int)wc) : wc; }
wint_t towupper(wint_t wc) { return (wc < 128) ? (wint_t)toupper((int)wc) : wc; }

typedef unsigned int wctype_t;

wctype_t wctype(const char *property) {
    if (!property) return 0;
    if (__builtin_strcmp(property, "alnum") == 0) return 1;
    if (__builtin_strcmp(property, "alpha") == 0) return 2;
    if (__builtin_strcmp(property, "blank") == 0) return 3;
    if (__builtin_strcmp(property, "cntrl") == 0) return 4;
    if (__builtin_strcmp(property, "digit") == 0) return 5;
    if (__builtin_strcmp(property, "graph") == 0) return 6;
    if (__builtin_strcmp(property, "lower") == 0) return 7;
    if (__builtin_strcmp(property, "print") == 0) return 8;
    if (__builtin_strcmp(property, "punct") == 0) return 9;
    if (__builtin_strcmp(property, "space") == 0) return 10;
    if (__builtin_strcmp(property, "upper") == 0) return 11;
    if (__builtin_strcmp(property, "xdigit") == 0) return 12;
    return 0;
}

int iswctype(wint_t wc, wctype_t desc) {
    switch (desc) {
    case 1:  return iswalnum(wc);
    case 2:  return iswalpha(wc);
    case 3:  return iswblank(wc);
    case 4:  return iswcntrl(wc);
    case 5:  return iswdigit(wc);
    case 6:  return iswgraph(wc);
    case 7:  return iswlower(wc);
    case 8:  return iswprint(wc);
    case 9:  return iswpunct(wc);
    case 10: return iswspace(wc);
    case 11: return iswupper(wc);
    case 12: return iswxdigit(wc);
    default: return 0;
    }
}

long sysconf(int name) {
    switch (name) {
    case 0 /* _SC_ARG_MAX */:      return 65536;
    case 2 /* _SC_CLK_TCK */:      return 100;
    case 3 /* _SC_NGROUPS_MAX */:  return 32;
    case 4 /* _SC_OPEN_MAX */:     return 256;
    case 7 /* _SC_VERSION */:      return 200809L;
    case 8 /* _SC_PAGESIZE */:     return 4096;
    case 9 /* _SC_NPROCESSORS_CONF */: return 1;
    case 10 /* _SC_NPROCESSORS_ONLN */: return 1;
    case 11 /* _SC_PHYS_PAGES */:  return 65536;
    default:                        return -1;
    }
}

#include <sys/statvfs.h>
#include <sys/statfs.h>
#include <errno.h>

int statvfs(const char *path, struct statvfs *buf) {
    (void)path;
    if (!buf) { errno = EFAULT; return -1; }
    /* Stub: return fake values */
    buf->f_bsize  = 4096;
    buf->f_frsize = 4096;
    buf->f_blocks = 65536;
    buf->f_bfree  = 32768;
    buf->f_bavail = 32768;
    buf->f_files  = 4096;
    buf->f_ffree  = 2048;
    buf->f_favail = 2048;
    buf->f_fsid   = 0;
    buf->f_flag   = 0;
    buf->f_namemax = 255;
    return 0;
}

int fstatvfs(int fd, struct statvfs *buf) {
    (void)fd;
    if (!buf) { errno = EFAULT; return -1; }
    buf->f_bsize  = 4096;
    buf->f_frsize = 4096;
    buf->f_blocks = 65536;
    buf->f_bfree  = 32768;
    buf->f_bavail = 32768;
    buf->f_files  = 4096;
    buf->f_ffree  = 2048;
    buf->f_favail = 2048;
    buf->f_fsid   = 0;
    buf->f_flag   = 0;
    buf->f_namemax = 255;
    return 0;
}

int statfs(const char *path, struct statfs *buf) {
    (void)path;
    if (!buf) { errno = EFAULT; return -1; }
    buf->f_type    = 0;
    buf->f_bsize   = 4096;
    buf->f_blocks  = 65536;
    buf->f_bfree   = 32768;
    buf->f_bavail  = 32768;
    buf->f_files   = 4096;
    buf->f_ffree   = 2048;
    buf->f_fsid    = 0;
    buf->f_namelen = 255;
    return 0;
}

int fstatfs(int fd, struct statfs *buf) {
    (void)fd;
    if (!buf) { errno = EFAULT; return -1; }
    buf->f_type    = 0;
    buf->f_bsize   = 4096;
    buf->f_blocks  = 65536;
    buf->f_bfree   = 32768;
    buf->f_bavail  = 32768;
    buf->f_files   = 4096;
    buf->f_ffree   = 2048;
    buf->f_fsid    = 0;
    buf->f_namelen = 255;
    return 0;
}
