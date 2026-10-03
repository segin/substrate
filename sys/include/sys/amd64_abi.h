/*
 * sys/amd64_abi.h - user structures of the native amd64 ABI
 *
 * A native 64-bit process is compiled LP64 and exchanges the structures of
 * docs/specs/abi-amd64.md (section 5) with the kernel.  The kernel's own
 * declarations of those structures keep the i386 layout on both kernels
 * (<sys/abi32.h>), so the 64-bit system-call wrappers in
 * exec/perso/perso_native64.c convert between the two at the boundary;
 * these are the 64-bit side.
 *
 * Structures whose amd64 layout happens to equal the kernel's -- struct
 * timeval, itimerval, rlimit, pollfd, utsname, termios, the socket
 * addresses -- need no twin here.
 */
#ifndef _SYS_AMD64_ABI_H
#define _SYS_AMD64_ABI_H

#include <stdint.h>

#define ABI64_ASSERT_SIZE(type, size) \
    _Static_assert(sizeof(type) == (size), #type " must keep its amd64 size")

struct amd64_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};
ABI64_ASSERT_SIZE(struct amd64_timespec, 16);

struct amd64_timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};
ABI64_ASSERT_SIZE(struct amd64_timeval, 16);

/* The FreeBSD/amd64 struct stat, with a 32-bit st_mode over FreeBSD's
 * 16-bit st_mode and st_bsdflags. */
struct amd64_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    int32_t  st_padding1;
    uint64_t st_rdev;
    struct amd64_timespec st_atim;
    struct amd64_timespec st_mtim;
    struct amd64_timespec st_ctim;
    struct amd64_timespec st_birthtim;
    int64_t  st_size;
    int64_t  st_blocks;
    int32_t  st_blksize;
    uint32_t st_flags;
    uint64_t st_gen;
    uint64_t st_spare[10];
};
ABI64_ASSERT_SIZE(struct amd64_stat, 224);

/* The fixed head of a getdents record; the NUL-terminated name follows and
 * the record is padded to a multiple of 8 bytes. */
struct amd64_dirent {
    uint64_t d_fileno;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    uint8_t  d_pad0;
    uint16_t d_namlen;
    uint16_t d_pad1;
    char     d_name[];
};
ABI64_ASSERT_SIZE(struct amd64_dirent, 24);

struct amd64_iovec {
    uint64_t iov_base;
    uint64_t iov_len;
};
ABI64_ASSERT_SIZE(struct amd64_iovec, 16);

struct amd64_rusage {
    struct amd64_timeval ru_utime;
    struct amd64_timeval ru_stime;
    int64_t ru_maxrss;
    int64_t ru_ixrss;
    int64_t ru_idrss;
    int64_t ru_isrss;
    int64_t ru_minflt;
    int64_t ru_majflt;
    int64_t ru_nswap;
    int64_t ru_inblock;
    int64_t ru_oublock;
    int64_t ru_msgsnd;
    int64_t ru_msgrcv;
    int64_t ru_nsignals;
    int64_t ru_nvcsw;
    int64_t ru_nivcsw;
};
ABI64_ASSERT_SIZE(struct amd64_rusage, 144);

/*
 * Is the calling process a native 64-bit one?  The converters shared with
 * 32-bit processes (kern/compat32.c) ask this to pick the user layout.
 * Needs <sys/proc.h>.
 */
#ifdef SUBSTRATE_ARCH_X86_64
#define proc_abi_is_amd64() \
    (current_process != NULL && current_process->bitness == BITNESS_64)
#else
#define proc_abi_is_amd64() 0
#endif

/*
 * The handler a 64-bit native process gets for system call `num`, or NULL
 * where the i386-layout handler already serves it (every call that takes
 * only scalars, strings, byte buffers and structures listed above as
 * layout-identical).
 */
void *native_amd64_syscall(uint32_t num);

#endif /* _SYS_AMD64_ABI_H */
