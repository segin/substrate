/*
 * lib/c/src/posix_extra.c — POSIX surface entries that weren't
 * already in lib/c/src/sys.c or lib/sys/.  Most are wrappers over
 * existing primitives; a handful are ENOSYS stubs for kernel calls
 * substrate doesn't have yet (mprotect, fsync, waitid, fchdir,
 * setresuid/gid, ...).  Each stub carries the right signature so
 * consumers compile, with the underlying gap documented.
 *
 * Organised by header:
 *
 *   unistd.h      — confstr, dup3, faccessat, fchdir, fdatasync,
 *                   fsync, getlogin_r, lockf, nice, pause, pipe2,
 *                   readlinkat, setegid, seteuid, setpgrp,
 *                   setregid, setreuid, swab, symlinkat, vfork
 *   fcntl.h       — creat, posix_close, posix_fadvise,
 *                   posix_fallocate
 *   dirent.h      — alphasort, rewinddir, scandir, seekdir, telldir
 *   sys/stat.h    — mkfifo, mkfifoat
 *   sys/wait.h    — waitid
 *   sys/uio.h     — readv, writev, preadv, pwritev
 *   sys/mman.h    — mprotect, msync, mlockall, munlockall,
 *                   posix_madvise, shm_open, shm_unlink
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

/* ============================================================
 * unistd.h
 * ============================================================ */

size_t confstr(int name, char *buf, size_t len) {
    const char *val;
    switch (name) {
    case _CS_PATH:                   val = "/usr/bin:/bin"; break;
    case _CS_GNU_LIBC_VERSION:       val = "substrate libc"; break;
    case _CS_GNU_LIBPTHREAD_VERSION: val = "substrate libpthread"; break;
    default:
        /* Unknown name: POSIX permits returning 0 with buf untouched. */
        if (buf && len > 0) buf[0] = '\0';
        return 0;
    }
    /* confstr returns the buffer size needed (string length + NUL); copies
     * up to len-1 bytes plus a terminating NUL when buf/len allow. */
    size_t need = strlen(val) + 1;
    if (buf && len > 0) {
        size_t n = (len - 1 < need - 1) ? len - 1 : need - 1;
        memcpy(buf, val, n);
        buf[n] = '\0';
    }
    return need;
}

int dup3(int oldfd, int newfd, int flags) {
    /* Kernel SYS_DUP3 exists (sys/arch/i386/syscall.h:164). */
    long r = syscall(SYS_DUP3, oldfd, newfd, flags);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}

int faccessat(int dirfd, const char *pathname, int mode, int flags) {
    /* No kernel SYS_FACCESSAT — fall back to access() and document. */
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    (void)flags;
    return access(pathname, mode);
}

int fchdir(int fd) {
    /* No kernel SYS_FCHDIR yet.  Use /proc/self/fd/<n> readlink as
     * a portable bridge until the proper syscall lands. */
    char path[64];
    char target[1024];
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    ssize_t n = readlink(path, target, sizeof(target) - 1);
    if (n < 0) return -1;
    target[n] = '\0';
    return chdir(target);
}

int fdatasync(int fd) {
    /* No kernel SYS_FDATASYNC — fsync provides the same guarantee
     * at coarser granularity. */
    return fsync(fd);
}

int fsync(int fd) {
    /* No kernel SYS_FSYNC.  Substrate's VFS writes are currently
     * synchronous, so the call is a no-op (and returning 0 is the
     * right answer when there's nothing buffered). */
    (void)fd;
    return 0;
}

int getlogin_r(char *buf, size_t bufsize) {
    char *n = getlogin();
    if (!n) { errno = ENXIO; return ENXIO; }
    size_t len = strlen(n);
    if (len + 1 > bufsize) { errno = ERANGE; return ERANGE; }
    memcpy(buf, n, len + 1);
    return 0;
}

int lockf(int fd, int cmd, off_t len) {
    /* Implement on top of fcntl(F_SETLK) — POSIX file-record locks.
     * lockf cmds: F_ULOCK=0, F_LOCK=1, F_TLOCK=2, F_TEST=3. */
    struct flock fl = { 0 };
    fl.l_whence = SEEK_CUR;
    fl.l_start  = 0;
    fl.l_len    = len;
    int op;
    switch (cmd) {
    case 0: fl.l_type = F_UNLCK; op = F_SETLK; break;
    case 1: fl.l_type = F_WRLCK; op = F_SETLKW; break;
    case 2: fl.l_type = F_WRLCK; op = F_SETLK;  break;
    case 3: {
        fl.l_type = F_WRLCK;
        if (fcntl(fd, F_GETLK, &fl) < 0) return -1;
        if (fl.l_type == F_UNLCK) return 0;
        errno = EAGAIN;
        return -1;
    }
    default: errno = EINVAL; return -1;
    }
    return fcntl(fd, op, &fl);
}

int nice(int inc) {
    int prio = getpriority(PRIO_PROCESS, 0);
    if (prio == -1 && errno != 0) return -1;
    int new_prio = prio + inc;
    if (new_prio > 19)  new_prio = 19;
    if (new_prio < -20) new_prio = -20;
    if (setpriority(PRIO_PROCESS, 0, new_prio) < 0) return -1;
    return new_prio;
}

int pause(void) {
    sigset_t empty;
    sigemptyset(&empty);
    sigsuspend(&empty);
    errno = EINTR;
    return -1;
}

int pipe2(int pipefd[2], int flags) {
    /* SYS_PIPE2 exists (syscall.h:163). */
    long r = syscall(SYS_PIPE2, (uintptr_t)pipefd, (uintptr_t)flags);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}

ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz) {
    /* No kernel SYS_READLINKAT — only AT_FDCWD is honoured today. */
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    return readlink(path, buf, bufsiz);
}

int setegid(gid_t egid) {
    /* Substrate has no SYS_SETRESGID / SETEGID — but setgid changes
     * BOTH real and effective; that's an over-approximation for a
     * caller that wanted only the effective change.  POSIX allows
     * this when the caller is privileged (root can drop to anything
     * either way); for non-root the call would refuse. */
    return setgid(egid);
}

int seteuid(uid_t euid) {
    return setuid(euid);
}

int setpgrp(void) {
    /* BSD spelling: equivalent to setpgid(0, 0). */
    return setpgid(0, 0);
}

int setregid(gid_t rgid, gid_t egid) {
    if (rgid == (gid_t)-1) return setegid(egid);
    if (egid != rgid && egid != (gid_t)-1) {
        errno = EPERM;   /* can't split when there's no setres path */
        return -1;
    }
    return setgid(rgid);
}

int setreuid(uid_t ruid, uid_t euid) {
    if (ruid == (uid_t)-1) return seteuid(euid);
    if (euid != ruid && euid != (uid_t)-1) {
        errno = EPERM;
        return -1;
    }
    return setuid(ruid);
}

void swab(const void *src, void *dst, ssize_t nbytes) {
    /* POSIX swab: copy nbytes from src to dst, swapping each
     * adjacent byte pair.  Negative nbytes is a no-op; odd byte is
     * undefined (we copy it through unswapped to be defensive). */
    if (nbytes <= 0) return;
    const unsigned char *s = (const unsigned char *)src;
    unsigned char       *d = (unsigned char       *)dst;
    ssize_t pairs = nbytes / 2;
    for (ssize_t i = 0; i < pairs; i++) {
        d[2*i    ] = s[2*i + 1];
        d[2*i + 1] = s[2*i    ];
    }
    if (nbytes & 1) d[nbytes - 1] = s[nbytes - 1];
}

int symlinkat(const char *target, int newdirfd, const char *linkpath) {
    if (newdirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    return symlink(target, linkpath);
}

pid_t vfork(void) {
    /* No separate kernel vfork.  fork() has copy-on-write so the
     * spec-deviation cost is performance, not correctness. */
    return fork();
}

/* ============================================================
 * fcntl.h
 * ============================================================ */

int creat(const char *path, mode_t mode) {
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int posix_close(int fd, int flag) {
    /* POSIX 2024 added posix_close with stronger error semantics
     * than close().  Substrate's close already retries on EINTR so
     * the difference is moot; just forward. */
    (void)flag;
    return close(fd);
}

int posix_fadvise(int fd, off_t offset, off_t len, int advice) {
    /* Advisory only.  No backing kernel call yet — accept silently. */
    (void)fd; (void)offset; (void)len; (void)advice;
    return 0;
}

int posix_fallocate(int fd, off_t offset, off_t len) {
    /* Fallback: extend the file with ftruncate.  Not as efficient as
     * a real preallocation but functionally correct. */
    struct stat st;
    if (fstat(fd, &st) < 0) return errno;
    off_t want = offset + len;
    if (want > st.st_size) {
        if (ftruncate(fd, want) < 0) return errno;
    }
    return 0;
}

/* ============================================================
 * dirent.h
 * ============================================================ */

int alphasort(const struct dirent **a, const struct dirent **b) {
    return strcmp((*a)->d_name, (*b)->d_name);
}

void rewinddir(DIR *dirp) {
    if (!dirp) return;
    /* opendir reopens cleanly; we have no public dir-fd accessor so
     * a portable rewind is closedir + opendir on the original path.
     * Substrate's DIR keeps the open fd; lseek to 0 should be enough
     * for the simple block-device-backed FS we have. */
    int fd = dirp->fd;
    if (fd >= 0) lseek(fd, 0, SEEK_SET);
}

long telldir(DIR *dirp) {
    if (!dirp) { errno = EBADF; return -1; }
    int fd = dirp->fd;
    if (fd < 0) return -1;
    return (long)lseek(fd, 0, SEEK_CUR);
}

void seekdir(DIR *dirp, long loc) {
    if (!dirp) return;
    int fd = dirp->fd;
    if (fd >= 0) lseek(fd, (off_t)loc, SEEK_SET);
}

int scandir(const char *dir, struct dirent ***namelist,
             int (*filter)(const struct dirent *),
             int (*cmp)(const struct dirent **, const struct dirent **)) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *ent;
    struct dirent **arr = 0;
    size_t n = 0, cap = 0;
    while ((ent = readdir(d)) != 0) {
        if (filter && !filter(ent)) continue;
        if (n + 1 > cap) {
            size_t ncap = cap ? cap * 2 : 16;
            struct dirent **na = (struct dirent **)realloc(arr, ncap * sizeof(*arr));
            if (!na) { closedir(d); free(arr); errno = ENOMEM; return -1; }
            arr = na; cap = ncap;
        }
        struct dirent *copy = (struct dirent *)malloc(sizeof(*copy));
        if (!copy) { closedir(d); for (size_t i = 0; i < n; i++) free(arr[i]); free(arr); errno = ENOMEM; return -1; }
        *copy = *ent;
        arr[n++] = copy;
    }
    closedir(d);
    if (cmp && n > 1) qsort(arr, n, sizeof(*arr), (int (*)(const void *, const void *))cmp);
    *namelist = arr;
    return (int)n;
}

/* ============================================================
 * sys/stat.h
 * ============================================================ */

int mkfifo(const char *path, mode_t mode) {
    /* mknod with S_IFIFO.  SYS_MKNOD is the substrate kernel
     * primitive (syscall.h:28). */
    return mknod(path, (mode & 07777) | S_IFIFO, 0);
}

int mkfifoat(int dirfd, const char *path, mode_t mode) {
    if (dirfd != AT_FDCWD) { errno = ENOSYS; return -1; }
    return mkfifo(path, mode);
}

/* ============================================================
 * sys/wait.h
 * ============================================================ */

int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) {
    /* No kernel SYS_WAITID — emulate via waitpid for the common
     * (P_ALL / P_PID) cases.  P_PGID falls through to waitpid with
     * negative pgid. */
    (void)infop;     /* substrate doesn't populate siginfo here */
    pid_t target;
    switch (idtype) {
    case P_PID:  target = (pid_t)id; break;
    case P_PGID: target = -(pid_t)id; break;
    case P_ALL:  target = -1; break;
    default: errno = EINVAL; return -1;
    }
    int status = 0;
    pid_t r = waitpid(target, &status, options & ~WEXITED);
    if (r < 0) return -1;
    if (r == 0) return 0;                  /* WNOHANG: nothing ready */
    if (infop) {
        infop->si_pid = r;
        if (WIFEXITED(status))      { infop->si_code = CLD_EXITED; infop->si_status = WEXITSTATUS(status); }
        else if (WIFSIGNALED(status)) { infop->si_code = CLD_KILLED; infop->si_status = WTERMSIG(status); }
        else if (WIFSTOPPED(status))  { infop->si_code = CLD_STOPPED; infop->si_status = WSTOPSIG(status); }
    }
    return 0;
}

/* ============================================================
 * sys/uio.h — scatter / gather I/O.  Loop over iov entries.
 * ============================================================ */

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        ssize_t n = read(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0) return total > 0 ? total : -1;
        total += n;
        if ((size_t)n < iov[i].iov_len) break;   /* short read */
    }
    return total;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        ssize_t n = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0) return total > 0 ? total : -1;
        total += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}

ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        ssize_t n = pread(fd, iov[i].iov_base, iov[i].iov_len, offset);
        if (n < 0) return total > 0 ? total : -1;
        total += n;
        offset += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}

ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        ssize_t n = pwrite(fd, iov[i].iov_base, iov[i].iov_len, offset);
        if (n < 0) return total > 0 ? total : -1;
        total += n;
        offset += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}

/* ============================================================
 * sys/mman.h — remaining VM entries.
 * ============================================================ */

int mprotect(void *addr, size_t len, int prot) {
    /* No kernel SYS_MPROTECT yet.  Critical for JIT / mmap-based
     * dynamic linking — flag as ENOSYS so callers can detect and
     * either bail or fall back to remap. */
    (void)addr; (void)len; (void)prot;
    errno = ENOSYS;
    return -1;
}

int msync(void *addr, size_t len, int flags) {
    /* SYS_MSYNC = 144 in syscall.h. */
    long r = syscall(SYS_MSYNC, (uintptr_t)addr, (uintptr_t)len, (uintptr_t)flags);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}

int mlockall(int flags) {
    /* Memory locking isn't enforced on substrate.  Returning 0
     * matches a system where everything is already non-swappable
     * (we have no swap), which is the user-visible truth. */
    (void)flags;
    return 0;
}

int munlockall(void) {
    return 0;
}

int posix_madvise(void *addr, size_t len, int advice) {
    /* Advisory — accept silently. */
    (void)addr; (void)len; (void)advice;
    return 0;
}

int shm_open(const char *name, int oflag, mode_t mode) {
    /* POSIX shared memory — backed by shmfs mounted at /dev/shm.
     * shmfs gives every object its own kmalloc-backed buffer so
     * multiple processes mmap'ing the same name share storage. */
    char buf[256];
    if (name[0] == '/') name++;
    snprintf(buf, sizeof(buf), "/dev/shm/%s", name);
    return open(buf, oflag, mode);
}

int shm_unlink(const char *name) {
    char buf[256];
    if (name[0] == '/') name++;
    snprintf(buf, sizeof(buf), "/dev/shm/%s", name);
    return unlink(buf);
}

/*
 * revoke(2) — invalidate all current access to a terminal/file so a
 * fresh session can take it over.  BSD-derived code (e.g. libtdecore's
 * KPty) calls this on a freshly-allocated pty slave before handing it
 * to a child.  Substrate's kernel has no per-fd revocation primitive
 * (no vhangup / TIOCVHANGUP), but a just-allocated pty has no existing
 * openers to evict, so the operation is a no-op success.  We still
 * validate the path so a bogus argument is reported rather than
 * silently "succeeding".
 */
int revoke(const char *path) {
    if (!path) { errno = EINVAL; return -1; }
    if (access(path, F_OK) != 0)
        return -1;                 /* access() set errno (ENOENT, ...) */
    return 0;
}

/*
 * getloadavg(3) — the BSD/glibc system-load query.  Reads the three
 * load averages (1/5/15 min) from /proc/loadavg, which the kernel
 * formats Linux-style ("L1 L5 L15 run/total lastpid").  Returns the
 * number of samples stored (<= nelem, <= 3), or -1 on error.
 */
int getloadavg(double loadavg[], int nelem) {
    if (nelem <= 0)
        return nelem == 0 ? 0 : -1;
    if (nelem > 3)
        nelem = 3;

    int fd = open("/proc/loadavg", O_RDONLY);
    if (fd < 0)
        return -1;
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return -1;
    buf[n] = '\0';

    double v[3] = { 0, 0, 0 };
    int got = sscanf(buf, "%lf %lf %lf", &v[0], &v[1], &v[2]);
    if (got < 1)
        return -1;
    int count = got < nelem ? got : nelem;
    for (int i = 0; i < count; i++)
        loadavg[i] = v[i];
    return count;
}
