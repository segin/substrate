/*
 * perso_netbsd.c - NetBSD i386 Personality
 *
 * NetBSD syscall numbers and wrappers for binary compatibility.
 * Based on NetBSD 10.x i386 ABI.
 */

#include <exec/perso/personality.h>
#include <stddef.h>
#include <arch/i386/syscall.h>
#include <sys/syscall_impl.h>
#include <sys/kern_syscalls.h>
#include <exec/perso/compat.h>
#include <exec/perso/netbsd/netbsd_syscalls.h>
#include <exec/perso/netbsd/netbsd_user.h>
#include "perso_ipc_sem.h"
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/times.h>
#include <sys/errno.h>
#include <sys/futex.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

int netbsd_sys_getrusage(int who, struct rusage *rusage) {
    if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN) return -EINVAL;

    struct tms t;
    if ((clock_t)kern_times(&t) == (clock_t)-1) return -1;

    struct rusage kr;
    memset(&kr, 0, sizeof(struct rusage));

    // Ticks to timeval. HZ=128.
    // user time
    clock_t ut = (who == RUSAGE_SELF) ? t.tms_utime : t.tms_cutime;
    kr.ru_utime.tv_sec = ut / 128;
    kr.ru_utime.tv_usec = ((ut % 128) * 1000000) / 128;

    // system time
    clock_t st = (who == RUSAGE_SELF) ? t.tms_stime : t.tms_cstime;
    kr.ru_stime.tv_sec = st / 128;
    kr.ru_stime.tv_usec = ((st % 128) * 1000000) / 128;

    if (copyout(&kr, rusage, sizeof(struct rusage)) != 0) return -14;
    return 0;
}

/*
 * NetBSD __futex(2) (syscall 166), NetBSD 8+.  Prototype:
 *   int __futex(int *uaddr, int op, int val, const struct timespec *timeout,
 *               int *uaddr2, int val2, int val3);
 * The op/flag encoding (FUTEX_WAIT/WAKE/REQUEUE/.../WAIT_BITSET, the
 * FUTEX_PRIVATE_FLAG and FUTEX_CLOCK_REALTIME bits) matches Linux and the
 * native sys_futex, so the only ABI difference is that val2 is an explicit
 * argument rather than overloaded onto the timeout slot.  Repack it for the
 * ops that consume val2 (REQUEUE / CMP_REQUEUE / WAKE_OP), where the native
 * sys_futex reads the requeue/secondary-wake limit out of the timeout arg.
 */
int netbsd_sys_futex(int *uaddr, int op, int val, const struct timespec *timeout,
                     int *uaddr2, int val2, int val3) {
    int cmd = op & FUTEX_CMD_MASK;
    void *to_arg;
    switch (cmd) {
        case FUTEX_REQUEUE:
        case FUTEX_CMP_REQUEUE:
        case FUTEX_WAKE_OP:
            to_arg = (void *)(intptr_t)val2;
            break;
        default:
            to_arg = (void *)timeout;
            break;
    }
    return sys_futex(uaddr, op, val, to_arg, uaddr2, val3);
}

/*
 * NetBSD __wait450(2) (syscall 449): the modern wait4 carrying a 64-bit
 * `struct rusage`.  NetBSD 10 libc/init use this rather than the
 * compat_50 wait4 (syscall 7, struct rusage50).  Reap through the native
 * waitpid; when the caller supplies a rusage buffer, zero-fill it —
 * substrate does not track per-child resource usage and NetBSD callers
 * (init) only inspect the wait status.
 */
int netbsd_sys_wait450(int pid, int *status, int options, struct rusage *rusage) {
    int ret = sys_waitpid(pid, status, options);
    if (ret > 0 && rusage) {
        struct rusage kr;
        memset(&kr, 0, sizeof(kr));
        copyout(&kr, rusage, sizeof(kr));
    }
    return ret;
}

/*
 * NetBSD i386 struct timeval is 12 bytes ({ int64_t tv_sec; int32_t tv_usec });
 * substrate's native timeval is 16 bytes because its suseconds_t is 64-bit.
 * Copying the native struct out would write 4 bytes past a NetBSD caller's
 * 12-byte buffer — e.g. libc's __time50 allocates exactly 12 bytes on the
 * stack with the saved %ebx just above it, so the overflow zeroes the caller's
 * PIC/GOT register and the next GOT-relative access faults.  Marshal a
 * NetBSD-layout timeval explicitly.  (struct timespec already matches at 12
 * bytes, so __clock_gettime50 needs no such wrapper.)
 */
struct netbsd_timeval { int64_t tv_sec; int32_t tv_usec; };

int netbsd_sys_gettimeofday(struct netbsd_timeval *tv, struct timezone *tz) {
    struct timeval ktv;
    struct timezone ktz;
    int ret = kern_gettimeofday(&ktv, tz ? &ktz : NULL);
    if (ret != 0) return ret;
    if (tv) {
        struct netbsd_timeval ntv = { ktv.tv_sec, (int32_t)ktv.tv_usec };
        if (copyout(&ntv, tv, sizeof(ntv)) != 0) return -EFAULT;
    }
    if (tz && copyout(&ktz, tz, sizeof(ktz)) != 0) return -EFAULT;
    return 0;
}

/*
 * NetBSD lseek(2) (syscall 199): off_t lseek(int fd, int pad, off_t offset,
 * int whence).  The i386 ABI inserts a `pad` int before the 64-bit offset
 * so off_t lands on an 8-byte-aligned stack slot.  Drop the pad and forward
 * the 64-bit offset (low,high) to the native handler, which returns the
 * resulting offset in edx:eax.
 */
int64_t netbsd_sys_lseek(int fd, int pad, uint32_t off_lo, uint32_t off_hi, int whence) {
    (void)pad;
    return sys_lseek(fd, off_lo, off_hi, whence);
}

/*
 * NetBSD pipe(2) (syscall 42) returns the two descriptors in registers —
 * fd[0] in eax, fd[1] in edx — and libc's pipe() stores them from there.
 * The native sys_pipe instead copies them into a user buffer and returns 0,
 * so a NetBSD caller reads eax/edx (0 and stale) and ends up juggling a
 * garbage fd ("Unable to make fd 1: Bad file descriptor", then exit).
 * Return the pair as a 64-bit value; the BSD syscall path puts the high
 * half in edx.
 */
int64_t netbsd_sys_pipe(void) {
    int fds[2];
    int ret = kern_pipe(fds);
    if (ret != 0) return ret;
    return ((int64_t)(uint32_t)fds[1] << 32) | (uint32_t)fds[0];
}

/*
 * NetBSD dup3(2) (compat_100_dup3, syscall 454).  Same as dup2 plus an
 * atomic flag set, but NetBSD's O_CLOEXEC/O_NONBLOCK bits differ from
 * substrate's, and the native sys_dup3 EINVALs any flag bit it doesn't
 * recognise — translate to substrate's encoding.  /bin/sh redirects its
 * fds with dup3(from, to, O_CLOEXEC); leaving it unwired aborts rc.
 */
int netbsd_sys_dup3(int oldfd, int newfd, int flags) {
    int sflags = 0;
    if (flags & NETBSD_O_CLOEXEC)  sflags |= O_CLOEXEC;
    if (flags & NETBSD_O_NONBLOCK) sflags |= O_NONBLOCK;
    return sys_dup3(oldfd, newfd, sflags);
}

/* NetBSD fcntl(2) command numbers (sys/fcntl.h) above F_SETFL. */
#define NB_F_GETLK          7
#define NB_F_SETLK          8
#define NB_F_SETLKW         9
#define NB_F_CLOSEM         10
#define NB_F_MAXFD          11
#define NB_F_DUPFD_CLOEXEC  12

/*
 * NetBSD fcntl(2): F_DUPFD..F_SETFL (0..4) share substrate's numbering, but
 * the record-lock commands renumber (NetBSD 7/8/9 vs substrate 5/6/7) and
 * NetBSD adds F_CLOSEM/F_MAXFD/F_DUPFD_CLOEXEC.  /bin/sh F_DUPFD_CLOEXEC's
 * its script fd above 10 and exits(2) when that fails, so leaving fcntl
 * unwired dropped init straight to single-user.  NetBSD's F_CLOSEM=10
 * collides with FreeBSD's F_DUP2FD=10, so freebsd_sys_fcntl can't be reused.
 */
int netbsd_sys_fcntl(int fd, int cmd, int arg) {
    switch (cmd) {
    case NB_F_GETLK:  return sys_fcntl(fd, F_GETLK, arg);
    case NB_F_SETLK:  return sys_fcntl(fd, F_SETLK, arg);
    case NB_F_SETLKW: return sys_fcntl(fd, F_SETLKW, arg);
    case NB_F_DUPFD_CLOEXEC: {
        int nfd = sys_fcntl(fd, F_DUPFD, arg);
        if (nfd >= 0)
            sys_fcntl(nfd, F_SETFD, FD_CLOEXEC);
        return nfd;
    }
    case NB_F_MAXFD: {
        int max = -1;
        for (int i = 0; i < MAX_FD; i++)
            if (current_process->fds[i]) max = i;
        return max;
    }
    case NB_F_CLOSEM: {
        for (int i = (arg < 0 ? 0 : arg); i < MAX_FD; i++)
            if (current_process->fds[i]) sys_close(i);
        return 0;
    }
    default:
        /* F_DUPFD/GETFD/SETFD/GETFL/SETFL (0..4) share numbering. */
        return sys_fcntl(fd, cmd, arg);
    }
}

#ifndef HOST_TEST

/* NetBSD syscall table - based on i386 column */
static void *netbsd_syscalls[MAX_SYSCALLS] = {
    [NETBSD_SYS_semget]         = (void *)&netbsd_sys_semget,
    [NETBSD_SYS_semop]          = (void *)&netbsd_sys_semop,
    [NETBSD_SYS_____semctl50]   = (void *)&netbsd_sys_semctl,
    [NETBSD_SYS_syscall]        = NULL,             /* syscall (indirect) */
    [NETBSD_SYS_exit]           = &sys_exit,
    [NETBSD_SYS_fork]           = &sys_fork,
    [NETBSD_SYS_read]           = &sys_read,
    [NETBSD_SYS_write]          = &sys_write,
    [NETBSD_SYS_open]           = &sys_open,
    [NETBSD_SYS_close]          = &sys_close,
    [NETBSD_SYS_wait4]          = &sys_waitpid,     /* compat_50 wait4 */
    [NETBSD_SYS___wait450]      = (void *)&netbsd_sys_wait450,  /* modern wait4 */
    [NETBSD_SYS_setsid]         = (void *)&sys_setsid,
    [NETBSD_SYS_readv]          = &sys_readv,
    [NETBSD_SYS_writev]         = &sys_writev,
    [NETBSD_SYS___socket30]     = &sys_socket,
    [NETBSD_SYS___gettimeofday50] = (void *)&netbsd_sys_gettimeofday,
    [NETBSD_SYS___nanosleep50]  = &sys_nanosleep,
    [NETBSD_SYS___sigprocmask14] = (void *)&netbsd_sys_sigprocmask,
    [NETBSD_SYS___sigsuspend14] = (void *)&netbsd_sys_sigsuspend,
    [NETBSD_SYS___sigaction_sigtramp] = (void *)&netbsd_sys_sigaction,
    [NETBSD_SYS_issetugid]      = (void *)&sys_issetugid,
    [NETBSD_SYS__lwp_self]      = (void *)&sys_thr_self,
    [NETBSD_SYS___clock_gettime50] = &sys_clock_gettime,
    [NETBSD_SYS_lseek199]       = (void *)&netbsd_sys_lseek,
    [NETBSD_SYS_getrlimit]      = (void *)&sys_getrlimit,
    [NETBSD_SYS___vfork14]      = &sys_vfork,
    [NETBSD_SYS_dup3]           = (void *)&netbsd_sys_dup3,
    [NETBSD_SYS_creat]          = &sys_creat,
    [NETBSD_SYS_link]           = &sys_link,
    [NETBSD_SYS_unlink]         = &sys_unlink,
    [NETBSD_SYS_obs_execv]      = &sys_compat_execv,  /* obs_execv */
    [NETBSD_SYS_chdir]          = &sys_chdir,
    [NETBSD_SYS_fchdir]         = &sys_fchdir,
    [NETBSD_SYS_mknod]          = &sys_mknod,
    [NETBSD_SYS_chmod]          = &sys_chmod,                       /* 15 */
    [NETBSD_SYS_chown]          = (void *)&netbsd_sys_chown,         /* 16  follows symlinks */
    [NETBSD_SYS_break]          = NULL,            /* break */
    [NETBSD_SYS_getfsstat]      = NULL,            /* getfsstat */
    [NETBSD_SYS_lseek]          = &sys_lseek,
    [NETBSD_SYS_getpid]         = &sys_getpid,
    [NETBSD_SYS_mount]          = &sys_mount,
    /* NetBSD unmount(2) takes (path, flags); use sys_umount2. */
    [NETBSD_SYS_unmount]        = &sys_umount2,
    [NETBSD_SYS_setuid]         = &sys_setuid,
    [NETBSD_SYS_getuid]         = &sys_getuid,
    [NETBSD_SYS_geteuid]        = &sys_geteuid,
    [NETBSD_SYS_ptrace]         = NULL,            /* ptrace - not implemented */
    /* AF_UNIX sockets — see sys/net/af_unix.c.  Native handlers route
     * directly; struct layouts match (sockaddr_un / msghdr / iovec). */
    [NETBSD_SYS_recvmsg]        = &sys_recvmsg,
    [NETBSD_SYS_sendmsg]        = &sys_sendmsg,
    [NETBSD_SYS_recvfrom]       = &sys_recvfrom,
    [NETBSD_SYS_accept]         = &sys_accept,
    [NETBSD_SYS_getpeername]    = &sys_getpeername,
    [NETBSD_SYS_getsockname]    = &sys_getsockname,
    [NETBSD_SYS_access]         = &sys_access,
    [NETBSD_SYS_chflags]        = NULL,            /* chflags */
    [NETBSD_SYS_fchflags]       = NULL,            /* fchflags */
    [NETBSD_SYS_sync]           = &sys_sync,
    [NETBSD_SYS_kill]           = (void *)&netbsd_sys_kill,
    [NETBSD_SYS_compat_stat]    = (void *)&netbsd_sys_compat_stat,       /* compat_stat */
    [NETBSD_SYS_getppid]        = &sys_getpid,     /* getppid - maps to getpid for now */
    [NETBSD_SYS_compat_lstat]   = (void *)&netbsd_sys_compat_lstat,      /* compat_lstat */
    [NETBSD_SYS_dup]            = &sys_dup,
    [NETBSD_SYS_pipe]           = (void *)&netbsd_sys_pipe,
    [NETBSD_SYS_getegid]        = &sys_getegid,
    [NETBSD_SYS_profil]         = NULL,            /* profil */
    [NETBSD_SYS_ktrace]         = NULL,            /* ktrace */
    [NETBSD_SYS_sigaction]      = &sys_sigaction,
    [NETBSD_SYS_getgid]         = &sys_getgid,
    [NETBSD_SYS_sigprocmask]    = (void *)&netbsd_sys_sigprocmask,
    [NETBSD_SYS___getlogin]     = NULL,            /* __getlogin */
    [NETBSD_SYS___setlogin]     = (void *)&sys_setlogin,
    [NETBSD_SYS_acct]           = &sys_acct,
    [NETBSD_SYS_sigpending]     = (void *)&netbsd_sys_sigpending,
    [NETBSD_SYS_sigaltstack]    = &sys_sigaltstack,
    /* NetBSD shares FreeBSD's 4.4BSD ioctl ABI (_IOC-encoded requests,
     * same struct termios / c_cc layout, TIOCSCTTY=_IO('t',97)).  Route
     * through the BSD ioctl translator instead of raw sys_ioctl, which
     * speaks Linux flat constants — otherwise TIOCSCTTY/TIOCGETA/... never
     * match and init can't get a controlling terminal. */
    [NETBSD_SYS_ioctl]          = (void *)&freebsd_sys_ioctl,
    [NETBSD_SYS_oreboot]        = NULL,            /* oreboot */
    [NETBSD_SYS_revoke]         = (void *)&sys_revoke,
    [NETBSD_SYS_symlink]        = NULL,            /* symlink - not implemented */
    [NETBSD_SYS_readlink]       = &sys_readlink,
    [NETBSD_SYS_execve]         = &sys_execve,
    [NETBSD_SYS_umask]          = (void *)&sys_umask,
    [NETBSD_SYS_chroot]         = &sys_chroot,
    [NETBSD_SYS_compat_fstat]   = (void *)&netbsd_sys_compat_fstat,      /* compat_fstat */
    [NETBSD_SYS_compat_getkern] = NULL,            /* compat_getkern */
    [NETBSD_SYS_getpagesize]    = NULL,            /* getpagesize */
    [NETBSD_SYS_compat_msync]   = NULL,            /* compat_msync */
    [NETBSD_SYS_vfork]          = &sys_vfork,
    [NETBSD_SYS_obs_vread]      = NULL,            /* obs_vread */
    [NETBSD_SYS_obs_vwrite]     = NULL,            /* obs_vwrite */
    [NETBSD_SYS_sbrk]           = NULL,            /* sbrk */
    [NETBSD_SYS_sstk]           = NULL,            /* sstk */
    [NETBSD_SYS_mmap]           = (void *)&netbsd_sys_mmap, /* 197 - modern mmap with pad */
    [NETBSD_SYS_vadvise]        = NULL,            /* vadvise */
    [NETBSD_SYS_munmap]         = &sys_munmap,
    [NETBSD_SYS_mprotect]       = &sys_mprotect,
    [NETBSD_SYS_madvise]        = NULL,            /* madvise */
    [NETBSD_SYS_obs_vhangup]    = NULL,            /* obs_vhangup */
    [NETBSD_SYS_obs_vlimit]     = NULL,            /* obs_vlimit */
    [NETBSD_SYS_mincore]        = NULL,            /* mincore */
    [NETBSD_SYS_getgroups]      = NULL,            /* getgroups - not implemented */
    [NETBSD_SYS_setgroups]      = NULL,            /* setgroups - not implemented */
    [NETBSD_SYS_getpgrp]        = &sys_getpgrp,
    [NETBSD_SYS_setpgid]        = &sys_setpgid,
    [NETBSD_SYS_setitimer]      = NULL,            /* setitimer */
    [NETBSD_SYS_compat_wait]    = NULL,            /* compat_wait */
    [NETBSD_SYS_swapon]         = NULL,            /* swapon */
    [NETBSD_SYS_getitimer]      = NULL,            /* getitimer */
    [NETBSD_SYS_gethostname]    = NULL,            /* gethostname */
    [NETBSD_SYS_sethostname]    = NULL,            /* sethostname */
    [NETBSD_SYS_getdtablesize]  = NULL,            /* getdtablesize */
    [NETBSD_SYS_dup2]           = &sys_dup2,
    [NETBSD_SYS_getdopt]        = NULL,            /* getdopt */
    [NETBSD_SYS_fcntl]          = (void *)&netbsd_sys_fcntl,
    [NETBSD_SYS_select]         = NULL,            /* select */
    [NETBSD_SYS_setdopt]        = NULL,            /* setdopt */
    [NETBSD_SYS_fsync]          = NULL,            /* fsync */
    [NETBSD_SYS_setpriority]    = NULL,            /* setpriority */
    [NETBSD_SYS_socket]         = &sys_socket,
    [NETBSD_SYS_connect]        = &sys_connect,
    [NETBSD_SYS_compat_accept]  = &sys_accept,     /* compat: identical surface */
    [NETBSD_SYS_getpriority]    = NULL,            /* getpriority */
    [NETBSD_SYS_compat_send]    = &sys_send,
    [NETBSD_SYS_compat_recv]    = &sys_recv,
    [NETBSD_SYS_compat_sigret]  = NULL,            /* compat_sigret */
    [NETBSD_SYS_bind]           = &sys_bind,
    [NETBSD_SYS_setsockopt]     = &sys_setsockopt,
    [NETBSD_SYS_listen]         = &sys_listen,
    [NETBSD_SYS_obs_vtimes]     = NULL,            /* obs_vtimes */
    [NETBSD_SYS_compat_sigvec]  = NULL,            /* compat_sigvec */
    [NETBSD_SYS_compat_sigblk]  = NULL,            /* compat_sigblk */
    [NETBSD_SYS_compat_sigset]  = NULL,            /* compat_sigset */
    [NETBSD_SYS_sigsuspend]     = (void *)&netbsd_sys_sigsuspend,
    [NETBSD_SYS_compat_sigstk]  = NULL,            /* compat_sigstk */
    [NETBSD_SYS_compat_recvmsg] = &sys_recvmsg,
    [NETBSD_SYS_compat_sendmsg] = &sys_sendmsg,
    [NETBSD_SYS_obs_vtrace]     = NULL,            /* obs_vtrace */
    [NETBSD_SYS_gettimeofday]   = &sys_gettimeofday,
    [NETBSD_SYS_getrusage]      = &netbsd_sys_getrusage,
    [NETBSD_SYS_getsockopt]     = &sys_getsockopt,
    [NETBSD_SYS_resuba]         = NULL,            /* resuba */
    /* Higher syscalls */
    [NETBSD_SYS_mkdir]          = &sys_mkdir,
    [NETBSD_SYS_rmdir]          = &sys_rmdir,
    [NETBSD_SYS_uname]          = &sys_uname,     /* __sysctl - map to uname */
    [NETBSD_SYS___futex]        = (void *)&netbsd_sys_futex,
    [NETBSD_SYS_stat]           = (void *)&netbsd_sys_stat,
    [NETBSD_SYS_fstat]          = (void *)&netbsd_sys_fstat,
    [NETBSD_SYS_lstat]          = (void *)&netbsd_sys_lstat,
    /* sys_50_*stat — what NetBSD 6+ ld.elf_so / libc actually call. */
    [NETBSD_SYS_stat50]         = (void *)&netbsd_sys_stat50,
    [NETBSD_SYS_fstat50]        = (void *)&netbsd_sys_fstat50,
    [NETBSD_SYS_lstat50]        = (void *)&netbsd_sys_lstat50,
    [NETBSD_SYS__lwp_setprivate] = (void *)&netbsd_sys_lwp_setprivate,
    [NETBSD_SYS___sysctl]       = (void *)&netbsd_sys_sysctl,
    [NETBSD_SYS_nanosleep]      = &sys_nanosleep,
    [NETBSD_SYS_poll]           = &sys_poll,
    [NETBSD_SYS_getcwd]         = &sys_getcwd,
    [NETBSD_SYS_getdents]       = &sys_getdents,  /* __getdents30 */
    [NETBSD_SYS_fchown]         = (void *)&sys_fchown,                /* 123 */
    [NETBSD_SYS_fchmod]         = (void *)&sys_fchmod,                /* 124 */
    [NETBSD_SYS_lchmod]         = (void *)&netbsd_sys_lchmod,         /* 274  no follow */
    [NETBSD_SYS_lchown]         = (void *)&sys_lchown,                /* 275  no follow */
    /* __posix_chown / __posix_fchown / __posix_lchown (283/284/285)
     * have identical signatures and identical behaviour to their
     * non-prefixed counterparts on Substrate (we always clear setuid
     * /setgid for unprivileged callers in the chmodat path).  Wire
     * them at the same handlers. */
    [NETBSD_SYS_posix_chown]    = (void *)&netbsd_sys_chown,          /* 283 */
    [NETBSD_SYS_posix_fchown]   = (void *)&sys_fchown,                /* 284 */
    [NETBSD_SYS_posix_lchown]   = (void *)&sys_lchown,                /* 285 */
    [NETBSD_SYS_fchmodat]       = (void *)&netbsd_sys_fchmodat,       /* 463 */
    [NETBSD_SYS_fchownat]       = (void *)&netbsd_sys_fchownat,       /* 464 */

    /* AF_UNIX socket-family additions not present in the original
     * NetBSD section above.  Numbers from 4.4BSD socket dispatch. */
    [NETBSD_SYS_sendto]         = &sys_sendto,                        /* 133 */
    [NETBSD_SYS_shutdown]       = &sys_shutdown,                      /* 134 */
    [NETBSD_SYS_socketpair]     = &sys_socketpair,                    /* 135 */
};

static const char *netbsd_names[MAX_SYSCALLS] = {
    [NETBSD_SYS_syscall]        = "syscall",
    [NETBSD_SYS_exit]           = "exit",
    [NETBSD_SYS_fork]           = "fork",
    [NETBSD_SYS_read]           = "read",
    [NETBSD_SYS_write]          = "write",
    [NETBSD_SYS_open]           = "open",
    [NETBSD_SYS_close]          = "close",
    [NETBSD_SYS_wait4]          = "wait4",
    [NETBSD_SYS___wait450]      = "__wait450",
    [NETBSD_SYS_setsid]         = "setsid",
    [NETBSD_SYS_readv]          = "readv",
    [NETBSD_SYS_writev]         = "writev",
    [NETBSD_SYS___socket30]     = "__socket30",
    [NETBSD_SYS___gettimeofday50] = "__gettimeofday50",
    [NETBSD_SYS___nanosleep50]  = "__nanosleep50",
    [NETBSD_SYS___sigprocmask14] = "__sigprocmask14",
    [NETBSD_SYS___sigsuspend14] = "__sigsuspend14",
    [NETBSD_SYS_issetugid]      = "issetugid",
    [NETBSD_SYS__lwp_self]      = "_lwp_self",
    [NETBSD_SYS___clock_gettime50] = "__clock_gettime50",
    [NETBSD_SYS_lseek199]       = "lseek",
    [NETBSD_SYS_getrlimit]      = "getrlimit",
    [NETBSD_SYS___vfork14]      = "__vfork14",
    [NETBSD_SYS_dup3]           = "dup3",
    [NETBSD_SYS_creat]          = "creat",
    [NETBSD_SYS_link]           = "link",
    [NETBSD_SYS_unlink]         = "unlink",
    [NETBSD_SYS_obs_execv]      = "obs_execv",
    [NETBSD_SYS_chdir]          = "chdir",
    [NETBSD_SYS_fchdir]         = "fchdir",
    [NETBSD_SYS_mknod]          = "mknod",
    [NETBSD_SYS_chmod]          = "chmod",
    [NETBSD_SYS_chown]          = "chown",
    [NETBSD_SYS_break]          = "break",
    [NETBSD_SYS_getfsstat]      = "getfsstat",
    [NETBSD_SYS_lseek]          = "lseek",
    [NETBSD_SYS_getpid]         = "getpid",
    [NETBSD_SYS_mount]          = "mount",
    [NETBSD_SYS_unmount]        = "unmount",
    [NETBSD_SYS_setuid]         = "setuid",
    [NETBSD_SYS_getuid]         = "getuid",
    [NETBSD_SYS_geteuid]        = "geteuid",
    [NETBSD_SYS_ptrace]         = "ptrace",
    [NETBSD_SYS_recvmsg]        = "recvmsg",
    [NETBSD_SYS_sendmsg]        = "sendmsg",
    [NETBSD_SYS_recvfrom]       = "recvfrom",
    [NETBSD_SYS_accept]         = "accept",
    [NETBSD_SYS_getpeername]    = "getpeername",
    [NETBSD_SYS_getsockname]    = "getsockname",
    [NETBSD_SYS_access]         = "access",
    [NETBSD_SYS_chflags]        = "chflags",
    [NETBSD_SYS_fchflags]       = "fchflags",
    [NETBSD_SYS_sync]           = "sync",
    [NETBSD_SYS_kill]           = "kill",
    [NETBSD_SYS_compat_stat]    = "compat_stat",
    [NETBSD_SYS_getppid]        = "getppid",
    [NETBSD_SYS_compat_lstat]   = "compat_lstat",
    [NETBSD_SYS_dup]            = "dup",
    [NETBSD_SYS_pipe]           = "pipe",
    [NETBSD_SYS_getegid]        = "getegid",
    [NETBSD_SYS_profil]         = "profil",
    [NETBSD_SYS_ktrace]         = "ktrace",
    [NETBSD_SYS_sigaction]      = "sigaction",
    [NETBSD_SYS_getgid]         = "getgid",
    [NETBSD_SYS_sigprocmask]    = "sigprocmask",
    [NETBSD_SYS___getlogin]     = "__getlogin",
    [NETBSD_SYS___setlogin]     = "__setlogin",
    [NETBSD_SYS_acct]           = "acct",
    [NETBSD_SYS_sigpending]     = "sigpending",
    [NETBSD_SYS_sigaltstack]    = "sigaltstack",
    [NETBSD_SYS_ioctl]          = "ioctl",
    [NETBSD_SYS_oreboot]        = "oreboot",
    [NETBSD_SYS_revoke]         = "revoke",
    [NETBSD_SYS_symlink]        = "symlink",
    [NETBSD_SYS_readlink]       = "readlink",
    [NETBSD_SYS_execve]         = "execve",
    [NETBSD_SYS_umask]          = "umask",
    [NETBSD_SYS_chroot]         = "chroot",
    [NETBSD_SYS_compat_fstat]   = "compat_fstat",
    [NETBSD_SYS_compat_getkern] = "compat_getkern",
    [NETBSD_SYS_getpagesize]    = "getpagesize",
    [NETBSD_SYS_compat_msync]   = "compat_msync",
    [NETBSD_SYS_vfork]          = "vfork",
    [NETBSD_SYS_obs_vread]      = "obs_vread",
    [NETBSD_SYS_obs_vwrite]     = "obs_vwrite",
    [NETBSD_SYS_sbrk]           = "sbrk",
    [NETBSD_SYS_sstk]           = "sstk",
    [NETBSD_SYS_mmap]           = "mmap",
    [NETBSD_SYS_vadvise]        = "vadvise",
    [NETBSD_SYS_munmap]         = "munmap",
    [NETBSD_SYS_mprotect]       = "mprotect",
    [NETBSD_SYS_madvise]        = "madvise",
    [NETBSD_SYS_obs_vhangup]    = "obs_vhangup",
    [NETBSD_SYS_obs_vlimit]     = "obs_vlimit",
    [NETBSD_SYS_mincore]        = "mincore",
    [NETBSD_SYS_getgroups]      = "getgroups",
    [NETBSD_SYS_setgroups]      = "setgroups",
    [NETBSD_SYS_getpgrp]        = "getpgrp",
    [NETBSD_SYS_setpgid]        = "setpgid",
    [NETBSD_SYS_setitimer]      = "setitimer",
    [NETBSD_SYS_compat_wait]    = "compat_wait",
    [NETBSD_SYS_swapon]         = "swapon",
    [NETBSD_SYS_getitimer]      = "getitimer",
    [NETBSD_SYS_gethostname]    = "gethostname",
    [NETBSD_SYS_sethostname]    = "sethostname",
    [NETBSD_SYS_getdtablesize]  = "getdtablesize",
    [NETBSD_SYS_dup2]           = "dup2",
    [NETBSD_SYS_getdopt]        = "getdopt",
    [NETBSD_SYS_fcntl]          = "fcntl",
    [NETBSD_SYS_select]         = "select",
    [NETBSD_SYS_setdopt]        = "setdopt",
    [NETBSD_SYS_fsync]          = "fsync",
    [NETBSD_SYS_setpriority]    = "setpriority",
    [NETBSD_SYS_socket]         = "socket",
    [NETBSD_SYS_connect]        = "connect",
    [NETBSD_SYS_compat_accept]  = "compat_accept",
    [NETBSD_SYS_getpriority]    = "getpriority",
    [NETBSD_SYS_compat_send]    = "compat_send",
    [NETBSD_SYS_compat_recv]    = "compat_recv",
    [NETBSD_SYS_compat_sigret]  = "compat_sigret",
    [NETBSD_SYS_bind]           = "bind",
    [NETBSD_SYS_setsockopt]     = "setsockopt",
    [NETBSD_SYS_listen]         = "listen",
    [NETBSD_SYS_obs_vtimes]     = "obs_vtimes",
    [NETBSD_SYS_compat_sigvec]  = "compat_sigvec",
    [NETBSD_SYS_compat_sigblk]  = "compat_sigblk",
    [NETBSD_SYS_compat_sigset]  = "compat_sigset",
    [NETBSD_SYS_sigsuspend]     = "sigsuspend",
    [NETBSD_SYS_compat_sigstk]  = "compat_sigstk",
    [NETBSD_SYS_compat_recvmsg] = "compat_recvmsg",
    [NETBSD_SYS_compat_sendmsg] = "compat_sendmsg",
    [NETBSD_SYS_obs_vtrace]     = "obs_vtrace",
    [NETBSD_SYS_gettimeofday]   = "gettimeofday",
    [NETBSD_SYS_getrusage]      = "getrusage",
    [NETBSD_SYS_getsockopt]     = "getsockopt",
    [NETBSD_SYS_resuba]         = "resuba",
    /* Higher syscalls */
    [NETBSD_SYS_mkdir]          = "mkdir",
    [NETBSD_SYS_rmdir]          = "rmdir",
    [NETBSD_SYS_uname]          = "uname",
    [NETBSD_SYS___futex]        = "__futex",
    [NETBSD_SYS_stat]           = "stat",
    [NETBSD_SYS_fstat]          = "fstat",
    [NETBSD_SYS_lstat]          = "lstat",
    [NETBSD_SYS_stat50]         = "__stat50",
    [NETBSD_SYS_fstat50]        = "__fstat50",
    [NETBSD_SYS_lstat50]        = "__lstat50",
    [NETBSD_SYS__lwp_setprivate] = "_lwp_setprivate",
    [NETBSD_SYS___sysctl]       = "__sysctl",
    [NETBSD_SYS_nanosleep]      = "nanosleep",
    [NETBSD_SYS_poll]           = "poll",
    [NETBSD_SYS_getcwd]         = "getcwd",
    [NETBSD_SYS_getdents]       = "getdents",
    [NETBSD_SYS___sigaction_sigtramp] = "__sigaction_sigtramp",
    [NETBSD_SYS_fchown]         = "fchown",
    [NETBSD_SYS_fchmod]         = "fchmod",
    [NETBSD_SYS_lchmod]         = "lchmod",
    [NETBSD_SYS_lchown]         = "lchown",
    [NETBSD_SYS_posix_chown]    = "__posix_chown",
    [NETBSD_SYS_posix_fchown]   = "__posix_fchown",
    [NETBSD_SYS_posix_lchown]   = "__posix_lchown",
    [NETBSD_SYS_fchmodat]       = "fchmodat",
    [NETBSD_SYS_fchownat]       = "fchownat",
    [NETBSD_SYS_sendto]         = "sendto",
    [NETBSD_SYS_shutdown]       = "shutdown",
    [NETBSD_SYS_socketpair]     = "socketpair",
};

static struct syscall_fmt netbsd_fmts[MAX_SYSCALLS] = {
    [NETBSD_SYS_exit]           = { 1, { ARG_INT } },
    [NETBSD_SYS_read]           = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [NETBSD_SYS_write]          = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [NETBSD_SYS_open]           = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [NETBSD_SYS_close]          = { 1, { ARG_INT } },
    [NETBSD_SYS_wait4]          = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [NETBSD_SYS___wait450]      = { 4, { ARG_INT, ARG_PTR, ARG_INT, ARG_PTR } },
    [NETBSD_SYS_setsid]         = { 0, { 0 } },
    [NETBSD_SYS_readv]          = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [NETBSD_SYS_writev]         = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [NETBSD_SYS___socket30]     = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [NETBSD_SYS___gettimeofday50] = { 2, { ARG_PTR, ARG_PTR } },
    [NETBSD_SYS___nanosleep50]  = { 2, { ARG_PTR, ARG_PTR } },
    [NETBSD_SYS___sigprocmask14] = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [NETBSD_SYS___sigsuspend14] = { 1, { ARG_PTR } },
    [NETBSD_SYS_issetugid]      = { 0, { 0 } },
    [NETBSD_SYS__lwp_self]      = { 0, { 0 } },
    [NETBSD_SYS___clock_gettime50] = { 2, { ARG_INT, ARG_PTR } },
    [NETBSD_SYS_lseek199]       = { 5, { ARG_INT, ARG_INT, ARG_HEX, ARG_HEX, ARG_INT } },
    [NETBSD_SYS_getrlimit]      = { 2, { ARG_INT, ARG_PTR } },
    [NETBSD_SYS___vfork14]      = { 0, { 0 } },
    [NETBSD_SYS_dup3]           = { 3, { ARG_INT, ARG_INT, ARG_HEX } },
    [NETBSD_SYS_link]           = { 2, { ARG_STR, ARG_STR } },
    [NETBSD_SYS_unlink]         = { 1, { ARG_STR } },
    [NETBSD_SYS_chdir]          = { 1, { ARG_STR } },
    [NETBSD_SYS_fchdir]         = { 1, { ARG_INT } },
    [NETBSD_SYS_mknod]          = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [NETBSD_SYS_chmod]          = { 2, { ARG_STR, ARG_HEX } },
    [NETBSD_SYS_chown]          = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [NETBSD_SYS_lseek]          = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [NETBSD_SYS_mount]          = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } },
    [NETBSD_SYS_unmount]        = { 2, { ARG_STR, ARG_HEX } },
    [NETBSD_SYS_setuid]         = { 1, { ARG_INT } },
    [NETBSD_SYS_access]         = { 2, { ARG_STR, ARG_HEX } },
    [NETBSD_SYS_kill]           = { 2, { ARG_INT, ARG_INT } },
    [NETBSD_SYS_compat_stat]    = { 2, { ARG_STR, ARG_PTR } },
    [NETBSD_SYS_compat_lstat]   = { 2, { ARG_STR, ARG_PTR } },
    [NETBSD_SYS_dup]            = { 1, { ARG_INT } },
    [NETBSD_SYS_pipe]           = { 1, { ARG_PTR } },
    [NETBSD_SYS_sigaction]      = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [NETBSD_SYS_sigprocmask]    = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [NETBSD_SYS_ioctl]          = { 3, { ARG_INT, ARG_HEX, ARG_HEX } },
    [NETBSD_SYS_symlink]        = { 2, { ARG_STR, ARG_STR } },
    [NETBSD_SYS_readlink]       = { 3, { ARG_STR, ARG_PTR, ARG_INT } },
    [NETBSD_SYS_execve]         = { 3, { ARG_STR, ARG_PTR, ARG_PTR } },
    [NETBSD_SYS_umask]          = { 1, { ARG_HEX } },
    [NETBSD_SYS_chroot]         = { 1, { ARG_STR } },
    [NETBSD_SYS_dup2]           = { 2, { ARG_INT, ARG_INT } },
    [NETBSD_SYS_mkdir]          = { 2, { ARG_STR, ARG_HEX } },
    [NETBSD_SYS_rmdir]          = { 1, { ARG_STR } },
    [NETBSD_SYS_uname]          = { 1, { ARG_PTR } },
    [NETBSD_SYS_stat]           = { 2, { ARG_STR, ARG_PTR } },
    [NETBSD_SYS_fstat]          = { 2, { ARG_INT, ARG_PTR } },
    [NETBSD_SYS_lstat]          = { 2, { ARG_STR, ARG_PTR } },
    [NETBSD_SYS_stat50]         = { 2, { ARG_STR, ARG_PTR } },
    [NETBSD_SYS_fstat50]        = { 2, { ARG_INT, ARG_PTR } },
    [NETBSD_SYS_lstat50]        = { 2, { ARG_STR, ARG_PTR } },
    [NETBSD_SYS__lwp_setprivate] = { 1, { ARG_HEX } },
    [NETBSD_SYS___sysctl]       = { 6, { ARG_PTR, ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR, ARG_INT } },
    [NETBSD_SYS_nanosleep]      = { 2, { ARG_PTR, ARG_PTR } },
    [NETBSD_SYS_poll]           = { 3, { ARG_PTR, ARG_INT, ARG_INT } },
    [NETBSD_SYS_getcwd]         = { 2, { ARG_PTR, ARG_INT } },
    [NETBSD_SYS_getdents]       = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [NETBSD_SYS___sigaction_sigtramp] = { 5, { ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR, ARG_INT } },
    [NETBSD_SYS_fchown]         = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [NETBSD_SYS_fchmod]         = { 2, { ARG_INT, ARG_INT } },
    [NETBSD_SYS_lchmod]         = { 2, { ARG_STR, ARG_INT } },
    [NETBSD_SYS_lchown]         = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [NETBSD_SYS_posix_chown]    = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [NETBSD_SYS_posix_fchown]   = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [NETBSD_SYS_posix_lchown]   = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [NETBSD_SYS_fchmodat]       = { 4, { ARG_INT, ARG_STR, ARG_HEX, ARG_HEX } },
    [NETBSD_SYS_fchownat]       = { 5, { ARG_INT, ARG_STR, ARG_INT, ARG_INT, ARG_HEX } },
};

extern void netbsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
extern int netbsd_sys_sigreturn(void *regs);

struct personality personality_netbsd = {
    .name = "NetBSD",
    .id = PERS_NETBSD,
    .syscall_table = netbsd_syscalls,
    .syscall_names = netbsd_names,
    .syscall_fmts = netbsd_fmts,
    .syscall_count = MAX_SYSCALLS,
    .sendsig = netbsd_sendsig,
    .sigreturn = netbsd_sys_sigreturn,
    .rt_sigreturn = NULL,
    .path_prefix = "/perso/netbsd"
};

#endif /* HOST_TEST */
