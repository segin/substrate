#ifndef _FCNTL_H
#define _FCNTL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/syscall.h>
#include <sys/types.h>

#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_ACCMODE   0x0003

#define O_CREAT     0x0040
#define O_EXCL      0x0080
#define O_NOCTTY    0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_NONBLOCK  0x0800
/* BSD-style aliases for O_NONBLOCK / async-IO.  Ported code
 * (xorg-server's kinput.c, BSD daemons, ...) reaches for these. */
#define FNDELAY     O_NONBLOCK
#define O_NDELAY    O_NONBLOCK
#define O_ASYNC     0x2000
#define FASYNC      O_ASYNC
/* Synchronous writes.  Value matches the kernel's <sys/fcntl.h> O_SYNC
 * (0x1000), which vnode_ops.c already honours as IO_SYNC; the userspace
 * header had simply omitted it.  substrate has no separate data-sync
 * (O_DSYNC) or read-sync (O_RSYNC) path, so they alias O_SYNC. */
#define O_SYNC      0x1000
#define O_DSYNC     O_SYNC
#define O_RSYNC     O_SYNC
#define O_DIRECTORY 0x10000
#define O_NOFOLLOW  0x20000
#define O_CLOEXEC   0x80000

#define AT_FDCWD            (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR        0x200

/* lseek() whence — POSIX allows these in <fcntl.h> alongside
 * <stdio.h> and <unistd.h>.  BSD code (OpenSSH's bsd-flock.c)
 * reaches for them after including only fcntl.h. */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* fcntl() commands */
#define F_DUPFD     0
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4
#define F_GETLK     5
#define F_SETLK     6
#define F_SETLKW    7
/* BSD-style SIGIO ownership.  Substrate's kernel doesn't deliver
 * SIGIO yet — these are accepted by fcntl() (no-op) so ported code
 * using async-I/O ownership compiles and runs.  Programs that
 * depend on actually receiving SIGIO will silently never see it
 * and should fall back to poll()/select(). */
#define F_GETOWN    9
#define F_SETOWN    8

/* file descriptor flags */
#define FD_CLOEXEC  1

/* record-lock types */
#define F_RDLCK     0
#define F_WRLCK     1
#define F_UNLCK     2

struct flock {
    short l_type;
    short l_whence;
    off_t l_start;
    off_t l_len;
    pid_t l_pid;
};

int open(const char *pathname, int flags, ...);
int openat(int dirfd, const char *pathname, int flags, ...);

/* LFS aliases — substrate's off_t is already 64-bit (O_LARGEFILE is a no-op). */
#if defined(_LARGEFILE64_SOURCE) || defined(_GNU_SOURCE)
#define open64   open
#define openat64 openat
#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#endif
int creat(const char *pathname, int mode);
int fcntl(int fd, int cmd, ...);

int posix_close(int fd, int flag);
int posix_fadvise(int fd, off_t offset, off_t len, int advice);
int posix_fallocate(int fd, off_t offset, off_t len);

/* posix_fadvise advice values. */
#define POSIX_FADV_NORMAL     0
#define POSIX_FADV_RANDOM     1
#define POSIX_FADV_SEQUENTIAL 2
#define POSIX_FADV_WILLNEED   3
#define POSIX_FADV_DONTNEED   4
#define POSIX_FADV_NOREUSE    5

#ifdef __cplusplus
}
#endif

#endif
