#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <limits.h>
#include <locale.h>
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
#include <dirent.h>

extern int64_t _syscall0(int);
extern int64_t _syscall1(int, uintptr_t);
extern int64_t _syscall2(int, uintptr_t, uintptr_t);
extern int64_t _syscall3(int, uintptr_t, uintptr_t, uintptr_t);
extern int64_t _syscall4(int, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
extern int64_t _syscall5(int, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
extern int64_t _syscall6(int, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

#undef errno
/*
 * Native Substrate i386 userland does not bootstrap TLS yet: freshly exec'd
 * processes enter with %fs/%gs cleared and the native ABI exposes no userland
 * set_thread_area entry point. Keep errno as plain process-global state until
 * native TLS initialization exists.
 */
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
    int64_t r = _syscall3(SYS_READ, fd, (uintptr_t)buf, (uintptr_t)count);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (ssize_t)r;
}

ssize_t write(int fd, const void *buf, size_t count) {
    int64_t r = _syscall3(SYS_WRITE, fd, (uintptr_t)buf, (uintptr_t)count);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (ssize_t)r;
}

/* Substrate has no pread/pwrite syscall yet; emulate via save / lseek /
 * read / lseek-back.  Not atomic against concurrent fd users — callers
 * that need atomicity must serialize externally. */
ssize_t pread(int fd, void *buf, size_t count, off_t offset) {
    off_t save = lseek(fd, 0, SEEK_CUR);
    if (save == (off_t)-1) return -1;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) return -1;
    ssize_t r = read(fd, buf, count);
    int saved_errno = errno;
    lseek(fd, save, SEEK_SET);
    errno = saved_errno;
    return r;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset) {
    off_t save = lseek(fd, 0, SEEK_CUR);
    if (save == (off_t)-1) return -1;
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) return -1;
    ssize_t r = write(fd, buf, count);
    int saved_errno = errno;
    lseek(fd, save, SEEK_SET);
    errno = saved_errno;
    return r;
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
    return __set_errno((int)_syscall3(SYS_OPEN, (uintptr_t)pathname, flags, mode));
}

int openat(int dirfd, const char *pathname, int flags, ...) {
    int mode = 0;

    if (flags & O_CREAT) {
        va_list ap;

        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    return __set_errno((int)_syscall4(SYS_OPENAT, dirfd, (uintptr_t)pathname, flags, mode));
}

int fcntl(int fd, int cmd, ...) {
    long arg = 0;
    va_list ap;
    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);
    return __set_errno((int)_syscall3(SYS_FCNTL, fd, cmd, (uintptr_t)arg));
}

int unlink(const char *pathname) {
    return __set_errno((int)_syscall1(SYS_UNLINK, (uintptr_t)pathname));
}

int unlinkat(int dirfd, const char *pathname, int flags) {
    return __set_errno((int)_syscall3(SYS_UNLINKAT, dirfd, (uintptr_t)pathname, flags));
}

int link(const char *oldpath, const char *newpath) {
    return __set_errno((int)_syscall2(SYS_LINK, (uintptr_t)oldpath, (uintptr_t)newpath));
}

int symlink(const char *target, const char *linkpath) {
    return __set_errno((int)_syscall2(SYS_SYMLINK, (uintptr_t)target, (uintptr_t)linkpath));
}

int execve(const char *filename, char *const argv[], char *const envp[]) {
    return __set_errno((int)_syscall3(SYS_EXECVE, (uintptr_t)filename, (uintptr_t)argv, (uintptr_t)envp));
}

int execv(const char *filename, char *const argv[]) {
    return execve(filename, argv, environ);
}

int execvp(const char *file, char *const argv[]) {
    const char *path;
    const char *p;
    const char *end;
    char buf[4096];

    if (file == NULL || file[0] == '\0') {
        errno = ENOENT;
        return -1;
    }

    /* If file contains a slash, use it directly. */
    if (strchr(file, '/') != NULL) {
        return execv(file, argv);
    }

    path = getenv("PATH");
    if (path == NULL || path[0] == '\0') {
        path = "/bin:/usr/bin";
    }

    for (p = path; ; p = end + 1) {
        size_t dirlen;
        size_t filelen = strlen(file);

        end = strchr(p, ':');
        dirlen = end ? (size_t)(end - p) : strlen(p);

        if (dirlen == 0) {
            /* Empty component means current directory. */
            if (filelen + 1 > sizeof(buf)) {
                if (!end) break;
                continue;
            }
            memcpy(buf, file, filelen + 1);
        } else {
            if (dirlen + 1 + filelen + 1 > sizeof(buf)) {
                if (!end) break;
                continue;
            }
            memcpy(buf, p, dirlen);
            buf[dirlen] = '/';
            memcpy(buf + dirlen + 1, file, filelen + 1);
        }

        execv(buf, argv);

        /* ENOENT/EACCES: try next component. Anything else: stop. */
        if (errno != ENOENT && errno != ENOTDIR && errno != EACCES) {
            return -1;
        }

        if (!end) break;
    }

    errno = ENOENT;
    return -1;
}

int execl(const char *path, const char *arg, ...) {
    va_list ap;
    int argc = 0;
    va_start(ap, arg);
    while (va_arg(ap, char *)) argc++;
    va_end(ap);

    char **argv = malloc((argc + 2) * sizeof(char *));
    if (argv == NULL) {
        errno = ENOMEM;
        return -1;
    }

    argv[0] = (char *)arg;
    va_start(ap, arg);
    for (int i = 1; i <= argc; i++) argv[i] = va_arg(ap, char *);
    argv[argc + 1] = NULL;
    va_end(ap);

    int ret = execv(path, argv);
    free(argv);
    return ret;
}

int execlp(const char *file, const char *arg, ...) {
    va_list ap;
    int argc = 0;
    va_start(ap, arg);
    while (va_arg(ap, char *)) argc++;
    va_end(ap);

    char **argv = malloc((argc + 2) * sizeof(char *));
    if (argv == NULL) {
        errno = ENOMEM;
        return -1;
    }

    argv[0] = (char *)arg;
    va_start(ap, arg);
    for (int i = 1; i <= argc; i++) argv[i] = va_arg(ap, char *);
    argv[argc + 1] = NULL;
    va_end(ap);

    int ret = execvp(file, argv);
    free(argv);
    return ret;
}

int execle(const char *path, const char *arg, ...) {
    va_list ap;
    int argc = 0;
    va_start(ap, arg);
    while (va_arg(ap, char *)) argc++;
    /* After the NULL argv terminator, execle takes one more arg: envp. */
    char *const *envp = va_arg(ap, char *const *);
    va_end(ap);

    char **argv = malloc((argc + 2) * sizeof(char *));
    if (argv == NULL) {
        errno = ENOMEM;
        return -1;
    }

    argv[0] = (char *)arg;
    va_start(ap, arg);
    for (int i = 1; i <= argc; i++) argv[i] = va_arg(ap, char *);
    argv[argc + 1] = NULL;
    va_end(ap);

    int ret = execve(path, argv, (char *const *)envp);
    free(argv);
    return ret;
}

int chdir(const char *path) {
    return __set_errno((int)_syscall1(SYS_CHDIR, (uintptr_t)path));
}

int chroot(const char *path) {
    return __set_errno((int)_syscall1(SYS_CHROOT, (uintptr_t)path));
}

char *getcwd(char *buf, size_t size) {
    char *out = buf;
    int allocated = 0;
    int ret;

    if (out == NULL) {
        if (size == 0u) {
            size = PATH_MAX;
        }
        out = malloc(size);
        if (out == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        allocated = 1;
    } else if (size == 0u) {
        errno = EINVAL;
        return NULL;
    }

    ret = (int)_syscall2(SYS_GETCWD, (uintptr_t)out, size);
    if (ret < 0) {
        if (allocated) {
            free(out);
        }
        errno = -ret;
        return NULL;
    }
    return out;
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
    return __set_errno((int)_syscall1(SYS_PIPE, (uintptr_t)pipefd));
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

int raise(int sig) {
    return kill(getpid(), sig);
}

sighandler_t signal(int signum, sighandler_t handler) {
    return (sighandler_t)(uintptr_t)_syscall2(SYS_SIGNAL, signum, (uintptr_t)handler);
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
    return __set_errno((int)_syscall3(SYS_SIGACTION, sig, (uintptr_t)act, (uintptr_t)oact));
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oset) {
    return __set_errno((int)_syscall3(SYS_SIGPROCMASK, how, (uintptr_t)set, (uintptr_t)oset));
}

int sigpending(sigset_t *set) {
    return __set_errno((int)_syscall1(SYS_SIGPENDING, (uintptr_t)set));
}

int sigsuspend(const sigset_t *mask) {
    return __set_errno((int)_syscall1(SYS_SIGSUSPEND, (uintptr_t)mask));
}

mode_t umask(mode_t mask) {
    return (mode_t)_syscall1(SYS_UMASK, (uintptr_t)mask);
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

    long r = (long)_syscall6(SYS_MMAP, (uintptr_t)addr, (uintptr_t)length,
                             prot, flags, fd, (uintptr_t)pgoff);
    /* Kernel returns negative errno on failure.  Treat any -1..-4095
     * value as an errno (matches Linux kernel convention) and surface
     * via errno + MAP_FAILED.  Without this, a failed mmap returns a
     * high-address-looking pointer that the caller may dereference. */
    if (r < 0 && r >= -4095) {
        errno = (int)(-r);
        return (void *)-1;
    }
    return (void *)(uintptr_t)r;
}

int munmap(void *addr, size_t length) {
    return __set_errno((int)_syscall2(SYS_MUNMAP, (uintptr_t)addr, (uintptr_t)length));
}

pid_t waitpid(pid_t pid, int *status, int options) {
    int64_t r = _syscall3(SYS_WAITPID, pid, (uintptr_t)status, options);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (pid_t)r;
}

pid_t wait(int *status) {
    return waitpid(-1, status, 0);
}

int access(const char *pathname, int mode) {
    return __set_errno((int)_syscall2(SYS_ACCESS, (uintptr_t)pathname, mode));
}

int stat(const char *pathname, struct stat *buf) {
    return __set_errno((int)_syscall2(SYS_STAT, (uintptr_t)pathname, (uintptr_t)buf));
}

int lstat(const char *pathname, struct stat *buf) {
    return __set_errno((int)_syscall2(SYS_LSTAT, (uintptr_t)pathname, (uintptr_t)buf));
}

int fstat(int fd, struct stat *buf) {
    return __set_errno((int)_syscall2(SYS_FSTAT, fd, (uintptr_t)buf));
}

int fstatat(int dirfd, const char *pathname, struct stat *buf, int flags) {
    return __set_errno((int)_syscall4(SYS_FSTATAT, dirfd, (uintptr_t)pathname, (uintptr_t)buf, flags));
}

int chown(const char *pathname, uid_t owner, gid_t group) {
    return __set_errno((int)_syscall3(SYS_CHOWN, (uintptr_t)pathname, owner, group));
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) {
    int64_t r = _syscall3(SYS_READLINK, (uintptr_t)pathname, (uintptr_t)buf, bufsiz);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (ssize_t)r;
}

time_t time(time_t *tloc) {
    return (time_t)_syscall1(SYS_TIME, (uintptr_t)tloc);
}

int stime(const time_t *t) {
    if (t == NULL) { errno = EINVAL; return -1; }
    return __set_errno((int)_syscall1(SYS_STIME, (uintptr_t)t));
}

unsigned int alarm(unsigned int seconds) {
    return (unsigned int)_syscall1(SYS_ALARM, seconds);
}

int getitimer(int which, struct itimerval *curr_value) {
    return __set_errno((int)_syscall2(SYS_GETITIMER, which, (uintptr_t)curr_value));
}

int setitimer(int which, const struct itimerval *new_value, struct itimerval *old_value) {
    return __set_errno((int)_syscall3(SYS_SETITIMER, which, (uintptr_t)new_value, (uintptr_t)old_value));
}

clock_t times(struct tms *buf) {
    return (clock_t)_syscall1(SYS_TIMES, (uintptr_t)buf);
}

int mkdir(const char *pathname, mode_t mode) {
    return __set_errno((int)_syscall2(SYS_MKDIR, (uintptr_t)pathname, mode));
}

int mkdirat(int dirfd, const char *pathname, mode_t mode) {
    return __set_errno((int)_syscall3(SYS_MKDIRAT, dirfd, (uintptr_t)pathname, mode));
}

int rmdir(const char *pathname) {
    return __set_errno((int)_syscall1(SYS_RMDIR, (uintptr_t)pathname));
}

int mknod(const char *pathname, mode_t mode, dev_t dev) {
    return __set_errno((int)_syscall3(SYS_MKNOD, (uintptr_t)pathname, mode, dev));
}

int chmod(const char *pathname, mode_t mode) {
    return __set_errno((int)_syscall2(SYS_CHMOD, (uintptr_t)pathname, mode));
}

/* utimes / utimensat / futimens — real syscalls now that the
 * kernel has SYS_UTIMENSAT / SYS_FUTIMENS wired through vfs
 * setattr_fs.  futimens lowers to utimensat with a NULL path so
 * we only have to maintain one path in the kernel.  */
int utimensat(int dirfd, const char *path, const struct timespec times[2],
              int flags) {
    return __set_errno((int)_syscall4(SYS_UTIMENSAT,
        (uintptr_t)dirfd, (uintptr_t)path, (uintptr_t)times, (uintptr_t)flags));
}

int futimens(int fd, const struct timespec times[2]) {
    return __set_errno((int)_syscall2(SYS_FUTIMENS,
        (uintptr_t)fd, (uintptr_t)times));
}

int utimes(const char *filename, const struct timeval times[2]) {
    if (!times) {
        /* NULL means "use current time".  */
        return utimensat(AT_FDCWD, filename, NULL, 0);
    }
    struct timespec ts[2] = {
        { .tv_sec = times[0].tv_sec, .tv_nsec = times[0].tv_usec * 1000 },
        { .tv_sec = times[1].tv_sec, .tv_nsec = times[1].tv_usec * 1000 }
    };
    return utimensat(AT_FDCWD, filename, ts, 0);
}

/* xattr family.  Read-side talks to the ext2/4 backend (the only one
 * with a getxattr/listxattr hook today); write-side stubs to ENOSYS
 * because no backend implements set/remove yet.  */
ssize_t getxattr(const char *path, const char *name, void *value, size_t size) {
    return (ssize_t)__set_errno((int)_syscall4(SYS_GETXATTR,
        (uintptr_t)path, (uintptr_t)name, (uintptr_t)value, (uintptr_t)size));
}
ssize_t lgetxattr(const char *path, const char *name, void *value, size_t size) {
    return (ssize_t)__set_errno((int)_syscall4(SYS_LGETXATTR,
        (uintptr_t)path, (uintptr_t)name, (uintptr_t)value, (uintptr_t)size));
}
ssize_t fgetxattr(int fd, const char *name, void *value, size_t size) {
    return (ssize_t)__set_errno((int)_syscall4(SYS_FGETXATTR,
        (uintptr_t)fd, (uintptr_t)name, (uintptr_t)value, (uintptr_t)size));
}
ssize_t listxattr(const char *path, char *list, size_t size) {
    return (ssize_t)__set_errno((int)_syscall3(SYS_LISTXATTR,
        (uintptr_t)path, (uintptr_t)list, (uintptr_t)size));
}
ssize_t llistxattr(const char *path, char *list, size_t size) {
    return (ssize_t)__set_errno((int)_syscall3(SYS_LLISTXATTR,
        (uintptr_t)path, (uintptr_t)list, (uintptr_t)size));
}
ssize_t flistxattr(int fd, char *list, size_t size) {
    return (ssize_t)__set_errno((int)_syscall3(SYS_FLISTXATTR,
        (uintptr_t)fd, (uintptr_t)list, (uintptr_t)size));
}
int setxattr(const char *path, const char *name, const void *value, size_t size, int flags) {
    return __set_errno((int)_syscall5(SYS_SETXATTR,
        (uintptr_t)path, (uintptr_t)name, (uintptr_t)value, (uintptr_t)size, (uintptr_t)flags));
}
int lsetxattr(const char *path, const char *name, const void *value, size_t size, int flags) {
    return __set_errno((int)_syscall5(SYS_LSETXATTR,
        (uintptr_t)path, (uintptr_t)name, (uintptr_t)value, (uintptr_t)size, (uintptr_t)flags));
}
int fsetxattr(int fd, const char *name, const void *value, size_t size, int flags) {
    return __set_errno((int)_syscall5(SYS_FSETXATTR,
        (uintptr_t)fd, (uintptr_t)name, (uintptr_t)value, (uintptr_t)size, (uintptr_t)flags));
}
int removexattr(const char *path, const char *name) {
    return __set_errno((int)_syscall2(SYS_REMOVEXATTR, (uintptr_t)path, (uintptr_t)name));
}
int lremovexattr(const char *path, const char *name) {
    return __set_errno((int)_syscall2(SYS_LREMOVEXATTR, (uintptr_t)path, (uintptr_t)name));
}
int fremovexattr(int fd, const char *name) {
    return __set_errno((int)_syscall2(SYS_FREMOVEXATTR, (uintptr_t)fd, (uintptr_t)name));
}
int mount(const char *source, const char *target, const char *filesystemtype, unsigned long mountflags, const void *data) {
    return __set_errno((int)_syscall5(SYS_MOUNT, (uintptr_t)source, (uintptr_t)target, (uintptr_t)filesystemtype, (uintptr_t)mountflags, (uintptr_t)data));
}

int umount(const char *target) {
    return __set_errno((int)_syscall1(SYS_UMOUNT, (uintptr_t)target));
}

int umount2(const char *target, int flags) {
    return __set_errno((int)_syscall2(SYS_UMOUNT2,
                                       (uintptr_t)target, (uintptr_t)flags));
}

/* BSD spelling. */
int unmount(const char *target, int flags) {
    return umount2(target, flags);
}

int rename(const char *oldpath, const char *newpath) {
    return __set_errno((int)_syscall2(SYS_RENAME, (uintptr_t)oldpath, (uintptr_t)newpath));
}

int ftruncate(int fd, off_t length) {
    uint32_t len_lo = (uint32_t)(length & 0xFFFFFFFF);
    uint32_t len_hi = (uint32_t)((length >> 32) & 0xFFFFFFFF);
    return __set_errno((int)_syscall4(SYS_FTRUNCATE, fd, len_lo, len_hi, 0));
}

int truncate(const char *path, off_t length) {
    uint32_t len_lo = (uint32_t)(length & 0xFFFFFFFF);
    uint32_t len_hi = (uint32_t)((length >> 32) & 0xFFFFFFFF);
    return __set_errno((int)_syscall5(SYS_TRUNCATE, (uintptr_t)path, len_lo, len_hi, 0, 0));
}

int uname(struct utsname *buf) {
    return __set_errno((int)_syscall1(SYS_UNAME, (uintptr_t)buf));
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    return __set_errno((int)_syscall2(SYS_NANOSLEEP, (uintptr_t)req, (uintptr_t)rem));
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
    return __set_errno((int)_syscall2(SYS_CLOCK_GETTIME, clk_id, (uintptr_t)tp));
}

int gettimeofday(struct timeval *restrict tp, void *restrict tzp) {
    (void)tzp;
    if (!tp) {
        errno = EINVAL;
        return -1;
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
        return -1;
    }

    tp->tv_sec = ts.tv_sec;
    tp->tv_usec = (suseconds_t)(ts.tv_nsec / 1000);
    return 0;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    return __set_errno((int)_syscall3(SYS_POLL, (uintptr_t)fds, (uintptr_t)nfds, timeout));
}

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    return __set_errno((int)_syscall3(SYS_IOCTL, fd, request, (uintptr_t)arg));
}

int isatty(int fd) {
    struct termios ios;
    return ioctl(fd, TCGETS, &ios) == 0;
}

/*
 * Scan a directory for an entry whose stat.st_rdev matches `rdev`
 * and whose type is character device.  Writes the full path into
 * `out` (size out_sz) and returns 0 on hit, -1 otherwise.  Subdirs
 * (like /dev/pts) are descended one level deep so /dev/pts/N names
 * are discoverable too.
 */
static int ttyname_scan_dir(const char *dir, dev_t rdev,
                            char *out, size_t out_sz, int recurse) {
    DIR *dp = opendir(dir);
    if (!dp) return -1;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;

        char path[256];
        int n = snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        if (n <= 0 || (size_t)n >= sizeof(path)) continue;

        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (S_ISCHR(st.st_mode) && st.st_rdev == rdev) {
            strncpy(out, path, out_sz - 1);
            out[out_sz - 1] = '\0';
            closedir(dp);
            return 0;
        }
        if (recurse && S_ISDIR(st.st_mode)) {
            if (ttyname_scan_dir(path, rdev, out, out_sz, 0) == 0) {
                closedir(dp);
                return 0;
            }
        }
    }
    closedir(dp);
    return -1;
}

int ttyname_r(int fd, char *buf, size_t buflen) {
    if (!buf || buflen < 2) {
        errno = ERANGE;
        return ERANGE;
    }
    if (!isatty(fd)) {
        errno = ENOTTY;
        return ENOTTY;
    }

    /*
     * Fast path: /proc/self/fd/<n> is a kernel-maintained symlink
     * whose target is the path the descriptor was opened with.
     * That target is the canonical tty name in nearly every case
     * (getty / login open /dev/ttyN, the shell inherits via fork).
     */
    char proc_link[32];
    snprintf(proc_link, sizeof(proc_link), "/proc/self/fd/%d", fd);
    ssize_t n = readlink(proc_link, buf, buflen - 1);
    if (n > 0) {
        buf[n] = '\0';
        /* Sanity-check: the target must still be a chardev with the
         * same st_rdev as fd — otherwise the symlink lied (rare
         * but possible if the file was renamed). */
        struct stat fst, tst;
        if (fstat(fd, &fst) == 0 && stat(buf, &tst) == 0 &&
            S_ISCHR(tst.st_mode) && tst.st_rdev == fst.st_rdev) {
            return 0;
        }
        /* fall through to the dev-scan fallback. */
    }

    /*
     * Slow path (proc not mounted, or symlink stale): stat the fd,
     * then walk /dev looking for a chardev with a matching st_rdev.
     */
    struct stat fst;
    if (fstat(fd, &fst) != 0) {
        return errno;
    }

    if (ttyname_scan_dir("/dev", fst.st_rdev, buf, buflen, 1) == 0) {
        return 0;
    }

    errno = ENOTTY;
    return ENOTTY;
}

char *ttyname(int fd) {
    static char buf[256];
    int r = ttyname_r(fd, buf, sizeof(buf));
    if (r != 0) {
        errno = r;
        return NULL;
    }
    return buf;
}

int gethostname(char *name, size_t len) {
    struct utsname u;
    if (!name || len == 0) {
        errno = EINVAL;
        return -1;
    }
    if (uname(&u) < 0) return -1;
    /* If the host name doesn't fit, glibc/POSIX behaviour differ —
     * Linux returns -1/ENAMETOOLONG, but for compatibility with
     * existing callers we truncate and force-terminate. */
    strncpy(name, u.nodename, len);
    name[len - 1] = '\0';
    return 0;
}

int sethostname(const char *name, size_t len) {
    return __set_errno((int)_syscall2(SYS_SETHOSTNAME,
                                       (uintptr_t)name, len));
}

int futex(int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2, int val3) {
    return __set_errno((int)_syscall6(240, (uintptr_t)uaddr, op, val, (uintptr_t)timeout, (uintptr_t)uaddr2, val3));
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
    /* Implement select using the poll syscall.  Stack-allocate the
     * pollfd array for the common case (≤ 256 fds) but fall back to
     * malloc for larger sets so we don't hit a hard EINVAL at 256. */
    if (nfds < 0 || nfds > FD_SETSIZE) {
        errno = EINVAL;
        return -1;
    }

    struct pollfd stack_fds[256];
    struct pollfd *fds = stack_fds;
    int allocated = 0;
    if (nfds > 256) {
        fds = malloc((size_t)nfds * sizeof(struct pollfd));
        if (!fds) { errno = ENOMEM; return -1; }
        allocated = 1;
    }
    int poll_count = 0;

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
    int ret = (int)_syscall3(SYS_POLL, (uintptr_t)fds, poll_count, poll_timeout);

    if (ret > 0) {
        /* Convert poll results back to select format and recount —
         * POSIX select() returns the total number of bits set across
         * all three bitmaps.  An fd that's both readable and writable
         * (same fd in rfds and wfds) is one pollfd entry but TWO
         * select bits, so the raw poll return value undercounts. */
        fd_set result_read, result_write, result_except;
        if (readfds) FD_ZERO(&result_read);
        if (writefds) FD_ZERO(&result_write);
        if (exceptfds) FD_ZERO(&result_except);

        int bits = 0;
        for (int i = 0; i < poll_count; i++) {
            int fd = fds[i].fd;
            short revents = fds[i].revents;

            if ((revents & (POLLIN | POLLHUP)) && readfds) {
                FD_SET(fd, &result_read); bits++;
            }
            if ((revents & POLLOUT) && writefds) {
                FD_SET(fd, &result_write); bits++;
            }
            if ((revents & (POLLERR | POLLPRI)) && exceptfds) {
                FD_SET(fd, &result_except); bits++;
            }
        }

        if (readfds) *readfds = result_read;
        if (writefds) *writefds = result_write;
        if (exceptfds) *exceptfds = result_except;
        ret = bits;
    } else if (ret == 0) {
        /* Timeout */
        if (readfds) FD_ZERO(readfds);
        if (writefds) FD_ZERO(writefds);
        if (exceptfds) FD_ZERO(exceptfds);
    } else {
        if (allocated) free(fds);
        return __set_errno(ret);
    }
    if (allocated) free(fds);
    
    return ret;
}

/* --- Process/file permission syscall wrappers --- */

int fchmod(int fd, mode_t mode) {
    return __set_errno((int)_syscall2(SYS_FCHMOD, fd, (int)mode));
}

int lchown(const char *pathname, uid_t owner, gid_t group) {
    return __set_errno((int)_syscall3(SYS_LCHOWN, (uintptr_t)pathname, (int)owner, (int)group));
}

int fchown(int fd, uid_t owner, gid_t group) {
    return __set_errno((int)_syscall3(SYS_FCHOWN, fd, (int)owner, (int)group));
}

int fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flag) {
    return __set_errno((int)_syscall5(SYS_FCHOWNAT, dirfd, (uintptr_t)pathname, (int)owner, (int)group, (int)flag));
}

int lchmod(const char *pathname, mode_t mode) {
    return __set_errno((int)_syscall2(SYS_LCHMOD, (uintptr_t)pathname, (int)mode));
}

int fchmodat(int dirfd, const char *pathname, mode_t mode, int flag) {
    return __set_errno((int)_syscall4(SYS_FCHMODAT, dirfd, (uintptr_t)pathname, (int)mode, (int)flag));
}

pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage) {
    pid_t ret = (pid_t)_syscall4(SYS_WAIT4, (int)pid, (uintptr_t)wstatus,
                                  options, (uintptr_t)rusage);
    if ((int)ret < 0)
        return (pid_t)__set_errno((int)ret);
    return ret;
}

int getrusage(int who, struct rusage *usage) {
    return __set_errno((int)_syscall2(SYS_GETRUSAGE, who, (uintptr_t)usage));
}

int getgroups(int size, gid_t list[]) {
    return __set_errno((int)_syscall2(SYS_GETGROUPS, size, (uintptr_t)list));
}

int setgroups(int size, const gid_t *list) {
    return __set_errno((int)_syscall2(SYS_SETGROUPS, size, (uintptr_t)list));
}

char *getlogin(void) {
    return NULL; /* stub: no kernel support yet */
}

/* getgrouplist now lives in src/grp.c — it actually walks /etc/group. */

/* --- Locale stubs --- */

char *setlocale(int category, const char *locale) {
    (void)category;
    (void)locale;
    return (char *)"C";
}

/* POSIX "C" locale.  Char-typed fields use CHAR_MAX (127) per spec
 * meaning "no value available".  Sufficient for any program that
 * doesn't actually rely on locale data — which is all of substrate
 * userland today, plus mpfr's vasprintf (which only consults
 * decimal_point and falls back cleanly to "." when grouping is ""). */
struct lconv *localeconv(void) {
    static struct lconv c_locale = {
        .decimal_point     = (char *)".",
        .thousands_sep     = (char *)"",
        .grouping          = (char *)"",
        .int_curr_symbol   = (char *)"",
        .currency_symbol   = (char *)"",
        .mon_decimal_point = (char *)"",
        .mon_thousands_sep = (char *)"",
        .mon_grouping      = (char *)"",
        .positive_sign     = (char *)"",
        .negative_sign     = (char *)"",
        .int_frac_digits   = 127,
        .frac_digits       = 127,
        .p_cs_precedes     = 127,
        .p_sep_by_space    = 127,
        .n_cs_precedes     = 127,
        .n_sep_by_space    = 127,
        .p_sign_posn       = 127,
        .n_sign_posn       = 127,
    };
    return &c_locale;
}

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
    case 2 /* _SC_CLK_TCK */:      return 128;  /* substrate HZ */
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
    return __set_errno((int)_syscall2(SYS_STATVFS,
                                       (uintptr_t)path, (uintptr_t)buf));
}

int fstatvfs(int fd, struct statvfs *buf) {
    return __set_errno((int)_syscall2(SYS_FSTATVFS,
                                       (uintptr_t)fd, (uintptr_t)buf));
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

/* POSIX pathconf — substrate returns conservative defaults.
 * Real implementation would dispatch on filesystem type.
 *
 * POSIX requires that an unrecognised `name` set errno=EINVAL and
 * return -1, distinct from "the variable is meaningful but has no
 * limit" (which is -1 with errno unchanged).  Callers like
 * libarchive's get_xfer_size() rely on this distinction — if we
 * silently return -1 without EINVAL they treat the absence as a
 * failure and refuse to proceed.  */
long pathconf(const char *path, int name) {
    (void)path;
    switch (name) {
    case 0:  /* _PC_LINK_MAX */          return 8;
    case 3:  /* _PC_NAME_MAX */          return 255;
    case 4:  /* _PC_PATH_MAX */          return 4096;
    case 5:  /* _PC_PIPE_BUF */          return 4096;
    case 6:  /* _PC_CHOWN_RESTRICTED */  return 1;
    case 7:  /* _PC_NO_TRUNC */          return 1;
    case 8:  /* _PC_VDISABLE */          return 0xFF;
    default:
        errno = EINVAL;
        return -1;
    }
}

long fpathconf(int fd, int name) {
    (void)fd;
    return pathconf("/", name);
}

int getpagesize(void) { return 4096; }

/* mktemp — stub.  Mutates the template's trailing XXXXXX in place
 * with a (weak) pseudo-random suffix; doesn't actually create the
 * file (the caller must open(2) it).  Real impl would call
 * mkstemp().  libstdc++ doesn't use it; libiberty does. */
char *mktemp(char *template) {
    char *end = template;
    while (*end) end++;
    char *p = end;
    while (p > template && p[-1] == 'X') p--;
    static unsigned long counter = 0x12345678UL;
    for (char *q = p; q < end; q++) {
        counter = counter * 1103515245UL + 12345UL;
        *q = 'a' + ((counter >> 16) & 0xff) % 26;
    }
    return template;
}

wctrans_t wctrans(const char *property) {
    if (!property) return 0;
    if (__builtin_strcmp(property, "tolower") == 0) return 1;
    if (__builtin_strcmp(property, "toupper") == 0) return 2;
    return 0;
}
wint_t towctrans(wint_t wc, wctrans_t desc) {
    switch (desc) {
    case 1: return towlower(wc);
    case 2: return towupper(wc);
    default: return wc;
    }
}

#include <sched.h>
int sched_yield(void) {
    /* SYS_SCHED_YIELD is a 0-arg syscall that always succeeds. */
    syscall(SYS_SCHED_YIELD);
    return 0;
}

/* The full sched_* surface beyond yield isn't kernel-backed yet —
 * stub the rest as ENOSYS so callers link cleanly. */
int sched_getscheduler(pid_t pid) { (void)pid; errno = ENOSYS; return -1; }
int sched_setscheduler(pid_t pid, int policy, const struct sched_param *p)
{ (void)pid; (void)policy; (void)p; errno = ENOSYS; return -1; }
int sched_getparam(pid_t pid, struct sched_param *p)
{ (void)pid; (void)p; errno = ENOSYS; return -1; }
int sched_setparam(pid_t pid, const struct sched_param *p)
{ (void)pid; (void)p; errno = ENOSYS; return -1; }
int sched_get_priority_min(int policy) { (void)policy; return 0; }
int sched_get_priority_max(int policy) { (void)policy; return policy == SCHED_FIFO || policy == SCHED_RR ? 99 : 0; }
int sched_rr_get_interval(pid_t pid, struct timespec *t) {
    (void)pid;
    if (t) { t->tv_sec = 0; t->tv_nsec = 10000000; /* 10 ms */ }
    return 0;
}
