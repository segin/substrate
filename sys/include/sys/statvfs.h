#ifndef _SYS_STATVFS_H
#define _SYS_STATVFS_H

#include <sys/types.h>
#include <sys/abi32.h>
#include <stdint.h>

/*
 * POSIX statvfs(2) view of a filesystem.  Mirrors userspace
 * include/sys/statvfs.h so syscall.c can copy_out one structure.
 *
 * Distinct from struct statfs: statvfs is the POSIX/SUS form
 * (long fields, no mount-source path), statfs is the BSD form
 * (uint64 fields, includes f_mntfromname/f_mntonname).
 */
/* The i386 layout on both kernels (<sys/abi32.h>). */
struct statvfs {
    abi_ulong_t    f_bsize;      /* preferred I/O block size */
    abi_ulong_t    f_frsize;     /* fundamental block size */
    abi_uint64_t   f_blocks;     /* total blocks in f_frsize units */
    abi_uint64_t   f_bfree;      /* free blocks */
    abi_uint64_t   f_bavail;     /* free blocks available to unprivileged */
    abi_uint64_t   f_files;      /* total inodes */
    abi_uint64_t   f_ffree;      /* free inodes */
    abi_uint64_t   f_favail;     /* free inodes available to unprivileged */
    abi_ulong_t    f_fsid;       /* filesystem ID */
    abi_ulong_t    f_flag;       /* ST_RDONLY etc. */
    abi_ulong_t    f_namemax;    /* max filename length */
    char           f_fstypename[16];
    char           f_basetype[16];
};
ABI32_ASSERT_SIZE(struct statvfs, 100);

#define ST_RDONLY   0x0001
#define ST_NOSUID   0x0002

#endif /* _SYS_STATVFS_H */
