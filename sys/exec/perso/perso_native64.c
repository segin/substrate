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

#include <machine/idt.h>
#include <machine/signal_arch.h>
#include <machine/syscall.h>
#include <kern/file.h>
#include <kern/time.h>
#include <pm/pm.h>
#include <sys/amd64_abi.h>
#include <sys/copy.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ipc.h>
#include <sys/kern_syscalls.h>
#include <sys/mount.h>
#include <sys/mqueue.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall_impl.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <vfs/vfs.h>
/* struct itimerspec; after <sys/time.h>, whose struct timespec it uses. */
#include <time.h>

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

/*
 * statfs: the process's struct statfs is the Linux-shaped one of the
 * userland <sys/statfs.h>.  Its name-length and fragment-size members
 * have no counterpart in the kernel's record and come from the statvfs
 * view of the same filesystem.
 */
static int statfs_copyout64(const struct statfs *k, const struct statvfs *v,
                            void *uaddr) {
    struct amd64_statfs u;

    memset(&u, 0, sizeof(u));
    u.f_type    = k->f_type;
    u.f_bsize   = k->f_bsize;
    u.f_blocks  = k->f_blocks;
    u.f_bfree   = k->f_bfree;
    u.f_bavail  = k->f_bavail;
    u.f_files   = k->f_files;
    u.f_ffree   = k->f_ffree;
    u.f_fsid    = k->f_fsid;
    u.f_namelen = v->f_namemax;
    u.f_frsize  = v->f_frsize;
    u.f_flags   = (uint32_t)(uint16_t)k->f_flags;
    return copyout(&u, uaddr, sizeof(u)) != 0 ? -EFAULT : 0;
}

static int statvfs_copyout64(const struct statvfs *k, void *uaddr) {
    struct amd64_statvfs u;

    memset(&u, 0, sizeof(u));
    u.f_bsize   = k->f_bsize;
    u.f_frsize  = k->f_frsize;
    u.f_blocks  = k->f_blocks;
    u.f_bfree   = k->f_bfree;
    u.f_bavail  = k->f_bavail;
    u.f_files   = k->f_files;
    u.f_ffree   = k->f_ffree;
    u.f_favail  = k->f_favail;
    u.f_fsid    = k->f_fsid;
    u.f_flag    = k->f_flag;
    u.f_namemax = k->f_namemax;
    memcpy(u.f_fstypename, k->f_fstypename, sizeof(u.f_fstypename));
    memcpy(u.f_basetype, k->f_basetype, sizeof(u.f_basetype));
    return copyout(&u, uaddr, sizeof(u)) != 0 ? -EFAULT : 0;
}

static int amd64_sys_statfs(const char *path, void *buf) {
    char kpath[256];
    struct statfs ks;
    struct statvfs kv;

    COPYIN_STR(path, kpath);
    int ret = kern_statfs(kpath, &ks);
    if (ret == 0) ret = kern_statvfs(kpath, &kv);
    return ret == 0 ? statfs_copyout64(&ks, &kv, buf) : ret;
}

static int amd64_sys_fstatfs(int fd, void *buf) {
    struct statfs ks;
    struct statvfs kv;

    int ret = kern_fstatfs(fd, &ks);
    if (ret == 0) ret = kern_fstatvfs(fd, &kv);
    return ret == 0 ? statfs_copyout64(&ks, &kv, buf) : ret;
}

static int amd64_sys_statvfs(const char *path, void *buf) {
    char kpath[256];
    struct statvfs kv;

    COPYIN_STR(path, kpath);
    int ret = kern_statvfs(kpath, &kv);
    return ret == 0 ? statvfs_copyout64(&kv, buf) : ret;
}

static int amd64_sys_fstatvfs(int fd, void *buf) {
    struct statvfs kv;

    int ret = kern_fstatvfs(fd, &kv);
    return ret == 0 ? statvfs_copyout64(&kv, buf) : ret;
}

static int amd64_sys_sysinfo(void *info) {
    struct sysinfo k;
    struct amd64_sysinfo u;

    if (!info) return -EFAULT;
    int ret = kern_sysinfo(&k);
    if (ret != 0) return ret;
    memset(&u, 0, sizeof(u));
    u.uptime    = k.uptime;
    u.loads[0]  = k.loads[0];
    u.loads[1]  = k.loads[1];
    u.loads[2]  = k.loads[2];
    u.totalram  = k.totalram;
    u.freeram   = k.freeram;
    u.sharedram = k.sharedram;
    u.bufferram = k.bufferram;
    u.totalswap = k.totalswap;
    u.freeswap  = k.freeswap;
    u.procs     = k.procs;
    u.totalhigh = k.totalhigh;
    u.freehigh  = k.freehigh;
    u.mem_unit  = k.mem_unit;
    return copyout(&u, info, sizeof(u)) != 0 ? -EFAULT : 0;
}

static int amd64_sys_proc_info(pid_t pid, void *info) {
    sys_procinfo_t k;
    struct amd64_procinfo u;

    /* As for a 32-bit process, pid 0 is the caller. */
    if (pid == 0 && current_process) pid = current_process->pid;
    int ret = kern_proc_info(pid, &k);
    if (ret != 0) return ret;
    memset(&u, 0, sizeof(u));
    u.pid        = k.pid;
    u.ppid       = k.ppid;
    u.pgid       = k.pgid;
    u.sid        = k.sid;
    u.uid        = k.uid;
    u.gid        = k.gid;
    u.euid       = k.euid;
    u.egid       = k.egid;
    u.state      = k.state;
    u.bitness    = k.bitness;
    u.perso_id   = k.perso_id;
    u.tty        = k.tty;
    u.nice       = k.nice;
    memcpy(u.name, k.name, sizeof(u.name));
    u.start_time = k.start_time;
    u.user_time  = k.user_time;
    u.sys_time   = k.sys_time;
    u.vsize      = k.vsize;
    u.rss        = k.rss;
    return copyout(&u, info, sizeof(u)) != 0 ? -EFAULT : 0;
}

/*
 * rlim_t is 64 bits in the process and 32 in the kernel.  "No limit" maps
 * to "no limit" both ways; a finite limit the kernel cannot represent is
 * one it could never enforce, and becomes "no limit" too.
 */
static uint64_t rlim_to64(rlim_t v) {
    return v == RLIM_INFINITY ? AMD64_RLIM_INFINITY : (uint64_t)v;
}

static rlim_t rlim_from64(uint64_t v) {
    return v >= (uint64_t)RLIM_INFINITY ? RLIM_INFINITY : (rlim_t)v;
}

static int amd64_sys_getrlimit(int resource, void *rlp) {
    struct amd64_rlimit u;

    if (!current_process) return -EINVAL;
    if (resource < 0 || resource >= RLIM_NLIMITS) return -EINVAL;
    if (!rlp) return -EFAULT;
    u.rlim_cur = rlim_to64(current_process->rlimits[resource].rlim_cur);
    u.rlim_max = rlim_to64(current_process->rlimits[resource].rlim_max);
    return copyout(&u, rlp, sizeof(u)) != 0 ? -EFAULT : 0;
}

static int amd64_sys_setrlimit(int resource, const void *rlp) {
    struct amd64_rlimit u;
    struct rlimit k;

    if (!current_process) return -EINVAL;
    if (resource < 0 || resource >= RLIM_NLIMITS) return -EINVAL;
    if (!rlp) return -EFAULT;
    if (copyin(rlp, &u, sizeof(u)) != 0) return -EFAULT;
    /* Compared at full width, so two limits that both narrow to "no
     * limit" still fail when the soft one is the larger. */
    if (u.rlim_cur > u.rlim_max) return -EINVAL;
    k.rlim_cur = rlim_from64(u.rlim_cur);
    k.rlim_max = rlim_from64(u.rlim_max);
    return kern_native_setrlimit(resource, &k);
}

static void itimerspec_to64(const struct itimerspec *k,
                            struct amd64_itimerspec *u) {
    timespec_to64(&k->it_interval, &u->it_interval);
    timespec_to64(&k->it_value, &u->it_value);
}

static int amd64_sys_timer_settime(int id, int flags, const void *nv, void *ov) {
    struct itimerspec knv, kov;
    struct amd64_itimerspec u;

    if (!nv) return -EINVAL;
    if (timespec_copyin64(nv, &knv.it_interval) != 0 ||
        timespec_copyin64((const char *)nv + sizeof(struct amd64_timespec),
                          &knv.it_value) != 0)
        return -EFAULT;
    int ret = kern_timer_settime(id, flags, &knv, ov ? &kov : NULL);
    if (ret == 0 && ov) {
        itimerspec_to64(&kov, &u);
        if (copyout(&u, ov, sizeof(u)) != 0) return -EFAULT;
    }
    return ret;
}

static int amd64_sys_timer_gettime(int id, void *curr) {
    struct itimerspec k;
    struct amd64_itimerspec u;

    if (!curr) return -EFAULT;
    int ret = kern_timer_gettime(id, &k);
    if (ret != 0) return ret;
    itimerspec_to64(&k, &u);
    return copyout(&u, curr, sizeof(u)) != 0 ? -EFAULT : 0;
}

static void mq_attr_to64(const struct mq_attr *k, struct amd64_mq_attr *u) {
    u->mq_flags   = k->mq_flags;
    u->mq_maxmsg  = k->mq_maxmsg;
    u->mq_msgsize = k->mq_msgsize;
    u->mq_curmsgs = k->mq_curmsgs;
}

/* A value the kernel's 32-bit member cannot hold is out of range for a
 * queue anyway; saturate so the callee's own range check rejects it. */
static abi_long_t mq_long_from64(int64_t v) {
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (abi_long_t)v;
}

static int mq_attr_copyin64(const void *uaddr, struct mq_attr *k) {
    struct amd64_mq_attr u;

    if (copyin(uaddr, &u, sizeof(u)) != 0) return -EFAULT;
    k->mq_flags   = mq_long_from64(u.mq_flags);
    k->mq_maxmsg  = mq_long_from64(u.mq_maxmsg);
    k->mq_msgsize = mq_long_from64(u.mq_msgsize);
    k->mq_curmsgs = mq_long_from64(u.mq_curmsgs);
    return 0;
}

static int amd64_sys_mq_open(const char *uname, int oflag, mode_t mode,
                             const void *uattr) {
    char kname[MQ_NAME_MAX + 1];
    size_t got = 0;
    struct mq_attr kattr;
    const struct mq_attr *attrp = NULL;

    int nrc = copyinstr(uname, kname, sizeof(kname), &got);
    if (nrc != 0) return -nrc;
    if ((oflag & O_CREAT) && uattr) {
        if (mq_attr_copyin64(uattr, &kattr) != 0) return -EFAULT;
        attrp = &kattr;
    }
    return kern_mq_open(kname, oflag, mode, attrp);
}

static int amd64_sys_mq_getsetattr(int mqd, const void *unew, void *uold) {
    struct mq_attr knew, kold;
    struct amd64_mq_attr u;
    const struct mq_attr *newp = NULL;

    if (unew) {
        if (mq_attr_copyin64(unew, &knew) != 0) return -EFAULT;
        newp = &knew;
    }
    int ret = kern_mq_setattr(mqd, newp, uold ? &kold : NULL);
    if (ret == 0 && uold) {
        mq_attr_to64(&kold, &u);
        if (copyout(&u, uold, sizeof(u)) != 0) return -EFAULT;
    }
    return ret;
}

/*
 * shmctl, semctl: only the status record differs.  IPC_SET reads no more
 * than the leading struct ipc_perm, which is the same in both layouts, so
 * it and every other command go to the ordinary handler.
 */
static int amd64_sys_shmctl(int shmid, int cmd, void *buf) {
    struct shmid_ds k;
    struct amd64_shmid_ds u;

    if (cmd != IPC_STAT && cmd != SHM_STAT)
        return sys_shmctl(shmid, cmd, buf);
    int ret = kern_shm_stat(shmid, &k);
    if (ret < 0) return ret;
    memset(&u, 0, sizeof(u));
    u.shm_perm   = k.shm_perm;
    u.shm_segsz  = k.shm_segsz;
    u.shm_atime  = k.shm_atime;
    u.shm_dtime  = k.shm_dtime;
    u.shm_ctime  = k.shm_ctime;
    u.shm_cpid   = k.shm_cpid;
    u.shm_lpid   = k.shm_lpid;
    u.shm_nattch = k.shm_nattch;
    return copyout(&u, buf, sizeof(u)) != 0 ? -EFAULT : 0;
}

/*
 * fcntl(2): only the record-lock commands carry a structure, and its two
 * 64-bit fields sit 4 bytes further on in a 64-bit process's than in the
 * kernel's.
 */
static int amd64_sys_fcntl(int fd, int cmd, uintptr_t arg) {
    struct amd64_flock u;
    struct kflock k;
    int rc;

    if (cmd != F_GETLK && cmd != F_SETLK && cmd != F_SETLKW)
        return sys_fcntl(fd, cmd, (int)arg);
    if (copyin((const void *)arg, &u, sizeof(u)) != 0)
        return -EFAULT;
    memset(&k, 0, sizeof(k));
    k.l_type   = u.l_type;
    k.l_whence = u.l_whence;
    k.l_start  = u.l_start;
    k.l_len    = u.l_len;
    k.l_pid    = u.l_pid;
    rc = proc_advlock(current_process, fd, cmd, &k);
    if (rc != 0 || cmd != F_GETLK)
        return rc;
    u.l_type   = k.l_type;
    u.l_whence = k.l_whence;
    u.l_start  = k.l_start;
    u.l_len    = k.l_len;
    u.l_pid    = k.l_pid;
    return copyout(&u, (void *)arg, sizeof(u)) != 0 ? -EFAULT : 0;
}

static int amd64_sys_semctl(int semid, int semnum, int cmd, uintptr_t arg) {
    struct semid_ds k;
    struct amd64_semid_ds u;

    if (cmd != IPC_STAT)
        return sys_semctl(semid, semnum, cmd, arg);
    int ret = kern_sem_stat(semid, &k);
    if (ret < 0) return ret;
    memset(&u, 0, sizeof(u));
    u.sem_perm  = k.sem_perm;
    u.sem_otime = k.sem_otime;
    u.sem_ctime = k.sem_ctime;
    u.sem_nsems = k.sem_nsems;
    return copyout(&u, (void *)arg, sizeof(u)) != 0 ? -EFAULT : 0;
}

/*
 * A thread's exit value and a queued signal's value are pointer-sized
 * data, not addresses, so all 64 bits count -- (void *)-1 must come back
 * from pthread_join() as it went in.  The dispatcher hands a handler the
 * low word of each argument; take the whole register from the frame.
 */
static int amd64_sys_thr_exit(void *retval) {
    const registers_t *regs = current_thread->syscall_regs;

    (void)retval;
    return sys_thr_exit((void *)(uintptr_t)regs->rdi);
}

static int amd64_sys_sigqueue(int pid, int sig, uintptr_t sival) {
    const registers_t *regs = current_thread->syscall_regs;

    (void)sival;
    return sys_sigqueue(pid, sig, (uintptr_t)regs->rdx);
}

static void *native_amd64_syscalls[MAX_SYSCALLS] = {
    [SYS_SIGRETURN]     = (void *)&amd64_sys_sigreturn,
    [SYS_SIGPROCMASK]   = (void *)&amd64_sys_sigprocmask,
    [SYS_SIGPENDING]    = (void *)&amd64_sys_sigpending,
    [SYS_SIGQUEUE]      = (void *)&amd64_sys_sigqueue,
    [SYS_STAT]          = (void *)&amd64_sys_stat,
    [SYS_LSTAT]         = (void *)&amd64_sys_lstat,
    [SYS_FSTAT]         = (void *)&amd64_sys_fstat,
    [SYS_FCNTL]         = (void *)&amd64_sys_fcntl,
    [SYS_FSTATAT]       = (void *)&amd64_sys_fstatat,
    [SYS_STATFS]        = (void *)&amd64_sys_statfs,
    [SYS_FSTATFS]       = (void *)&amd64_sys_fstatfs,
    [SYS_STATVFS]       = (void *)&amd64_sys_statvfs,
    [SYS_FSTATVFS]      = (void *)&amd64_sys_fstatvfs,
    [SYS_CLOCK_GETTIME] = (void *)&amd64_sys_clock_gettime,
    [SYS_NANOSLEEP]     = (void *)&amd64_sys_nanosleep,
    [SYS_GETRUSAGE]     = (void *)&amd64_sys_getrusage,
    [SYS_WAIT4]         = (void *)&amd64_sys_wait4,
    [SYS_UTIMENSAT]     = (void *)&amd64_sys_utimensat,
    [SYS_FUTIMENS]      = (void *)&amd64_sys_futimens,
    [SYS_GETDENTS]      = (void *)&amd64_sys_getdents,
    [SYS_SYSINFO]       = (void *)&amd64_sys_sysinfo,
    [SYS_PROC_INFO]     = (void *)&amd64_sys_proc_info,
    [SYS_GETRLIMIT]     = (void *)&amd64_sys_getrlimit,
    [SYS_SETRLIMIT]     = (void *)&amd64_sys_setrlimit,
    [SYS_TIMER_SETTIME] = (void *)&amd64_sys_timer_settime,
    [SYS_TIMER_GETTIME] = (void *)&amd64_sys_timer_gettime,
    [SYS_MQ_OPEN]       = (void *)&amd64_sys_mq_open,
    [SYS_MQ_GETSETATTR] = (void *)&amd64_sys_mq_getsetattr,
    [SYS_SHMCTL]        = (void *)&amd64_sys_shmctl,
    [SYS_SEMCTL]        = (void *)&amd64_sys_semctl,
    [SYS_THR_EXIT]      = (void *)&amd64_sys_thr_exit,
};

void *native_amd64_syscall(uint32_t num) {
    return num < MAX_SYSCALLS ? native_amd64_syscalls[num] : NULL;
}

#endif /* SUBSTRATE_ARCH_X86_64 */
