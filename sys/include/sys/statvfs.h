#ifndef _SYS_STATVFS_H
#define _SYS_STATVFS_H

#include <sys/types.h>
#include <stdint.h>

/*
 * POSIX statvfs(2) view of a filesystem.  Mirrors userspace
 * include/sys/statvfs.h so syscall.c can copy_out one structure.
 *
 * Distinct from struct statfs: statvfs is the POSIX/SUS form
 * (long fields, no mount-source path), statfs is the BSD form
 * (uint64 fields, includes f_mntfromname/f_mntonname).
 */
struct statvfs {
    unsigned long  f_bsize;      /* preferred I/O block size */
    unsigned long  f_frsize;     /* fundamental block size */
    uint64_t       f_blocks;     /* total blocks in f_frsize units */
    uint64_t       f_bfree;      /* free blocks */
    uint64_t       f_bavail;     /* free blocks available to unprivileged */
    uint64_t       f_files;      /* total inodes */
    uint64_t       f_ffree;      /* free inodes */
    uint64_t       f_favail;     /* free inodes available to unprivileged */
    unsigned long  f_fsid;       /* filesystem ID */
    unsigned long  f_flag;       /* ST_RDONLY etc. */
    unsigned long  f_namemax;    /* max filename length */
    char           f_fstypename[16];
    char           f_basetype[16];
};

#define ST_RDONLY   0x0001
#define ST_NOSUID   0x0002

#endif /* _SYS_STATVFS_H */
