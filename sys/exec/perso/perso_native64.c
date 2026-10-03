/*
 * perso_native64.c - native system calls for 64-bit (amd64) processes
 *
 * A 64-bit process uses the native system-call numbers but the LP64
 * structures of docs/specs/abi-amd64.md, while the kernel's own structures
 * keep the i386 layout (<sys/abi32.h>).  Most calls take only scalars,
 * strings and byte buffers and go to the ordinary handlers.  The ones
 * here carry a structure whose two layouts differ: each wrapper runs the
 * kernel-internal form of the call and converts the structure at the
 * boundary (<sys/amd64_abi.h>).
 *
 * Structures that hold pointers (iovec, sigaction, ...) are converted
 * where the ordinary handlers already convert them, in kern/compat32.c,
 * which reads the layout of the calling process.
 */
#ifdef SUBSTRATE_ARCH_X86_64

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <machine/signal_arch.h>
#include <machine/syscall.h>
#include <kern/file.h>
#include <sys/amd64_abi.h>
#include <sys/copy.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall_impl.h>
#include <sys/time.h>
#include <vfs/vfs.h>

static void timespec_to64(const struct timespec *k, struct amd64_timespec *u) {
    u->tv_sec  = k->tv_sec;
    u->tv_nsec = k->tv_nsec;
}

static int timespec_copyin64(const void *uaddr, struct timespec *k) {
    struct amd64_timespec u;

    if (copyin(uaddr, &u, sizeof(u)) != 0) return -EFAULT;
    k->tv_sec  = u.tv_sec;
    k->tv_nsec = (abi_long_t)u.tv_nsec;
    /* A nanosecond count that does not survive the narrowing is out of
     * range anyway; keep it out of range for the callee's check. */
    if (u.tv_nsec != (int64_t)k->tv_nsec) k->tv_nsec = -1;
    return 0;
}

static int timespec_copyout64(const struct timespec *k, void *uaddr) {
    struct amd64_timespec u;

    timespec_to64(k, &u);
    return copyout(&u, uaddr, sizeof(u)) != 0 ? -EFAULT : 0;
}

static int stat_copyout64(const struct stat *k, void *uaddr) {
    struct amd64_stat u;

    memset(&u, 0, sizeof(u));
    u.st_dev     = k->st_dev;
    u.st_ino     = k->st_ino;
    u.st_nlink   = k->st_nlink;
    u.st_mode    = k->st_mode;
    u.st_uid     = k->st_uid;
    u.st_gid     = k->st_gid;
    u.st_rdev    = k->st_rdev;
    u.st_atim.tv_sec  = k->st_atime;
    u.st_atim.tv_nsec = k->st_atime_nsec;
    u.st_mtim.tv_sec  = k->st_mtime;
    u.st_mtim.tv_nsec = k->st_mtime_nsec;
    u.st_ctim.tv_sec  = k->st_ctime;
    u.st_ctim.tv_nsec = k->st_ctime_nsec;
    u.st_size    = k->st_size;
    u.st_blocks  = k->st_blocks;
    u.st_blksize = (int32_t)k->st_blksize;
    return copyout(&u, uaddr, sizeof(u)) != 0 ? -EFAULT : 0;
}

static int amd64_sys_stat(const char *path, void *buf) {
    char kpath[256];
    struct stat kbuf;

    COPYIN_STR(path, kpath);
    int ret = kern_stat(kpath, &kbuf);
    return ret == 0 ? stat_copyout64(&kbuf, buf) : ret;
}

static int amd64_sys_lstat(const char *path, void *buf) {
    char kpath[256];
    struct stat kbuf;

    COPYIN_STR(path, kpath);
    int ret = kern_lstat(kpath, &kbuf);
    return ret == 0 ? stat_copyout64(&kbuf, buf) : ret;
}

static int amd64_sys_fstat(int fd, void *buf) {
    struct stat kbuf;

    int ret = kern_fstat(fd, &kbuf);
    return ret == 0 ? stat_copyout64(&kbuf, buf) : ret;
}

static int amd64_sys_fstatat(int dirfd, const char *path, void *buf, int flags) {
    char kpath[256];
    struct stat kbuf;

    COPYIN_STR(path, kpath);
    int ret = kern_fstatat(dirfd, kpath, &kbuf, flags);
    return ret == 0 ? stat_copyout64(&kbuf, buf) : ret;
}

static int amd64_sys_clock_gettime(int clk_id, void *tp) {
    struct timespec kts;

    int ret = kern_clock_gettime(clk_id, &kts);
    return ret == 0 ? timespec_copyout64(&kts, tp) : ret;
}

static int amd64_sys_nanosleep(const void *req, void *rem) {
    struct timespec kreq, krem;

    if (!req) return -EFAULT;
    if (timespec_copyin64(req, &kreq) != 0) return -EFAULT;
    int ret = kern_nanosleep(&kreq, rem ? &krem : NULL);
    if (ret == -EINTR && rem && timespec_copyout64(&krem, rem) != 0)
        return -EFAULT;
    return ret;
}

static void rusage_to64(const struct rusage *k, struct amd64_rusage *u) {
    u->ru_utime.tv_sec  = k->ru_utime.tv_sec;
    u->ru_utime.tv_usec = k->ru_utime.tv_usec;
    u->ru_stime.tv_sec  = k->ru_stime.tv_sec;
    u->ru_stime.tv_usec = k->ru_stime.tv_usec;
    u->ru_maxrss   = k->ru_maxrss;
    u->ru_ixrss    = k->ru_ixrss;
    u->ru_idrss    = k->ru_idrss;
    u->ru_isrss    = k->ru_isrss;
    u->ru_minflt   = k->ru_minflt;
    u->ru_majflt   = k->ru_majflt;
    u->ru_nswap    = k->ru_nswap;
    u->ru_inblock  = k->ru_inblock;
    u->ru_oublock  = k->ru_oublock;
    u->ru_msgsnd   = k->ru_msgsnd;
    u->ru_msgrcv   = k->ru_msgrcv;
    u->ru_nsignals = k->ru_nsignals;
    u->ru_nvcsw    = k->ru_nvcsw;
    u->ru_nivcsw   = k->ru_nivcsw;
}

static int amd64_sys_getrusage(int who, void *usage) {
    struct rusage kru;
    struct amd64_rusage u;

    if (!usage || !current_process) return -EINVAL;
    switch (who) {
    case RUSAGE_SELF:
    case RUSAGE_THREAD:   kru = current_process->rusage; break;
    case RUSAGE_CHILDREN: kru = current_process->rusage_children; break;
    default: return -EINVAL;
    }
    rusage_to64(&kru, &u);
    return copyout(&u, usage, sizeof(u)) != 0 ? -EFAULT : 0;
}

static int amd64_sys_wait4(int pid, int *status, int options, void *rusage) {
    int kstatus = 0;
    struct rusage kru;
    struct amd64_rusage u;

    int ret = kern_wait4(pid, status ? &kstatus : NULL, options,
                         rusage ? &kru : NULL);
    if (ret >= 0) {
        if (status && copyout(&kstatus, status, sizeof(int)) != 0)
            return -EFAULT;
        if (rusage) {
            rusage_to64(&kru, &u);
            if (copyout(&u, rusage, sizeof(u)) != 0) return -EFAULT;
        }
    }
    return ret;
}

static int amd64_sys_utimensat(int dirfd, const char *path, const void *times,
                               int flags) {
    /* The native handler takes the times from user memory in its own
     * layout; hand it nothing and apply ours.  It accepts NULL as "now",
     * which is all a caller can express until it grows a kernel-side
     * entry point taking the two timespecs. */
    struct timespec kts[2];

    if (times) {
        if (timespec_copyin64(times, &kts[0]) != 0 ||
            timespec_copyin64((const char *)times + sizeof(struct amd64_timespec),
                              &kts[1]) != 0)
            return -EFAULT;
    }
    return kern_utimensat(dirfd, path, times ? kts : NULL, flags);
}

static int amd64_sys_futimens(int fd, const void *times) {
    return amd64_sys_utimensat(fd, NULL, times, 0);
}

/*
 * getdents: FreeBSD-layout records (struct amd64_dirent), each padded to
 * 8 bytes.  The cursor logic is kern_getdents()'s.
 */
static int amd64_sys_getdents(unsigned int fd, void *dirp, unsigned int count) {
    if (!current_process || fd >= MAX_FD) return -EBADF;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -EBADF;

    unsigned int bpos = 0;
    char rec[sizeof(struct amd64_dirent) + 256 + 8];
    struct amd64_dirent *u = (struct amd64_dirent *)rec;

    while (bpos < count) {
        struct dirent dent;
        struct dirent *d = readdir_fs((fs_node_t *)f->f_data, f->f_offset, &dent);
        if (!d) break;

        size_t name_len = strnlen(d->d_name, 255);
        unsigned int reclen =
            (unsigned int)((offsetof(struct amd64_dirent, d_name) + name_len + 1 + 7) & ~7UL);
        if (bpos + reclen > count) {
            if (bpos == 0) return -EINVAL;
            break;
        }

        uint64_t cur_off = (uint64_t)f->f_offset;
        uint64_t next_off = (d->d_off > cur_off) ? d->d_off : cur_off + 1;
        memset(rec, 0, reclen);
        u->d_fileno = d->d_ino;
        u->d_off    = (int64_t)next_off;
        u->d_reclen = (uint16_t)reclen;
        u->d_type   = d->d_type;
        u->d_namlen = (uint16_t)name_len;
        memcpy(u->d_name, d->d_name, name_len);
        if (copyout(rec, (char *)dirp + bpos, reclen) != 0) return -EFAULT;

        bpos += reclen;
        f->f_offset = next_off;
    }
    return (int)bpos;
}

/*
 * sigset_t is 16 bytes in the amd64 ABI, with the 32 signals the kernel
 * has in its first word.  A set read from the process is that word, which
 * the ordinary handlers already take; a set written back must also clear
 * the other three.
 */
static int sigset_zero_tail(void *uset) {
    static const uint32_t zeros[3];

    return copyout(zeros, (char *)uset + sizeof(uint32_t), sizeof(zeros)) != 0
               ? -EFAULT : 0;
}

static int amd64_sys_sigprocmask(int how, const void *set, void *oset) {
    int ret = sys_sigprocmask(how, set, oset);

    if (ret == 0 && oset) ret = sigset_zero_tail(oset);
    return ret;
}

static int amd64_sys_sigpending(void *set) {
    int ret = sys_sigpending(set);

    if (ret == 0 && set) ret = sigset_zero_tail(set);
    return ret;
}

static void *native_amd64_syscalls[MAX_SYSCALLS] = {
    [SYS_SIGRETURN]     = (void *)&amd64_sys_sigreturn,
    [SYS_SIGPROCMASK]   = (void *)&amd64_sys_sigprocmask,
    [SYS_SIGPENDING]    = (void *)&amd64_sys_sigpending,
    [SYS_STAT]          = (void *)&amd64_sys_stat,
    [SYS_LSTAT]         = (void *)&amd64_sys_lstat,
    [SYS_FSTAT]         = (void *)&amd64_sys_fstat,
    [SYS_FSTATAT]       = (void *)&amd64_sys_fstatat,
    [SYS_CLOCK_GETTIME] = (void *)&amd64_sys_clock_gettime,
    [SYS_NANOSLEEP]     = (void *)&amd64_sys_nanosleep,
    [SYS_GETRUSAGE]     = (void *)&amd64_sys_getrusage,
    [SYS_WAIT4]         = (void *)&amd64_sys_wait4,
    [SYS_UTIMENSAT]     = (void *)&amd64_sys_utimensat,
    [SYS_FUTIMENS]      = (void *)&amd64_sys_futimens,
    [SYS_GETDENTS]      = (void *)&amd64_sys_getdents,
};

void *native_amd64_syscall(uint32_t num) {
    return num < MAX_SYSCALLS ? native_amd64_syscalls[num] : NULL;
}

#endif /* SUBSTRATE_ARCH_X86_64 */
