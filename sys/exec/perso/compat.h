/*
 * compat.h - Compatibility wrapper declarations
 */

#ifndef _COMPAT_H
#define _COMPAT_H

#include <stdint.h>
#include <stddef.h>

/* 32-bit lseek for foreign personalities with 32-bit off_t */
int32_t compat_lseek32(int fd, int32_t offset, int whence);

/* 32-bit time for Y2038-unsafe personalities */
int32_t compat_time32(int32_t *tloc);

/* FreeBSD-specific translations */
struct freebsd_stat;
struct freebsd11_stat;
int freebsd_sys_uname(void *buf);
int freebsd_sys_uname_v4(void *buf);
int64_t freebsd_sys_lseek(int fd, int pad, uint32_t off_lo, uint32_t off_hi, int whence);
int64_t freebsd_sys_lseek13(int fd, uint32_t off_lo, uint32_t off_hi, int whence);
void *freebsd_sys_mmap(void *addr, size_t len, int prot, int flags, int fd, uint32_t off_lo, uint32_t off_hi);
int freebsd_sys_ioctl(int fd, uint32_t request, void *arg);
int freebsd_sys_fcntl(int fd, int cmd, int arg);

/*
 * FreeBSD thr_exit(long *state): write TID_TERMINATED (1) into *state and
 * umtx-wake any pthread_join() waiter parked on it, then terminate the
 * thread.  libthr passes &curthread->tid; the native sys_thr_exit only wakes
 * native (thread-object) joiners, not the umtx word a FreeBSD joiner sleeps on.
 */
int freebsd_sys_thr_exit(long *state);

/*
 * FreeBSD thr_self(long *id): the kernel writes the calling thread's id through
 * *id and returns 0 (suword_lwpid).  Native sys_thr_self returns the tid in the
 * return register and ignores the pointer — using it for FreeBSD left libthr's
 * main-thread tid 0, which collides with UMUTEX_UNOWNED and corrupts owned-mutex
 * bookkeeping.  This honors the real out-pointer ABI.
 */
int freebsd_sys_thr_self(long *id);

/* rtprio_thread(2): thread realtime/idle scheduling class — accepted as a
 * no-op (substrate has no rtprio classes; libthr only needs it not to fail). */
int freebsd_sys_rtprio_thread(int function, long lwpid, void *rtp);

/* clock_gettime(2) with FreeBSD clockid translation (CLOCK_MONOTONIC=4 etc.
 * -> substrate native 0/1).  std::chrono::steady_clock depends on this. */
int freebsd_sys_clock_gettime(int clk_id, void *tp);
int freebsd_sys_gettimeofday(void *tv, void *tz);
int freebsd_sys_getrusage(int who, void *usage);
int freebsd_sys_getitimer(int which, void *curr_value);
int freebsd_sys_wait4(int pid, int *status, int options, void *rusage);

/* clock_getres(2) with the same FreeBSD clockid translation; reports the
 * 1/HZ tick resolution (substrate has no native sys_clock_getres). */
int freebsd_sys_clock_getres(int clk_id, void *res);

/* execv wrapper for ancient NetBSD/SunOS binaries */
int sys_compat_execv(const char *path, char **argv);

#endif /* _COMPAT_H */

