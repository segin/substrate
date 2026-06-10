#ifndef _SYS_STATFS_H
#define _SYS_STATFS_H

#include <sys/types.h>

/* Linux-compatible f_type magic values */
#define EXT2_SUPER_MAGIC    0xEF53
#define EXT3_SUPER_MAGIC    0xEF53
#define EXT4_SUPER_MAGIC    0xEF53
#define FAT_SUPER_MAGIC     0x4D44
#define MSDOS_SUPER_MAGIC   0x4D44
#define TMPFS_MAGIC         0x01021994
#define PROC_SUPER_MAGIC    0x9FA0
#define NFS_SUPER_MAGIC     0x6969
#define MINIX_SUPER_MAGIC   0x137F
#define MINIX2_SUPER_MAGIC  0x2468

struct statfs {
    uint32_t    f_type;     /* type of file system */
    uint64_t    f_bsize;    /* optimal transfer block size */
    uint64_t    f_blocks;   /* total data blocks in fs */
    uint64_t    f_bfree;    /* free blocks in fs */
    uint64_t    f_bavail;   /* free blocks avail to unprivileged user */
    uint64_t    f_files;    /* total file nodes in fs */
    uint64_t    f_ffree;    /* free file nodes in fs */
    int64_t     f_fsid;     /* file system id */
    uint32_t    f_namelen;  /* maximum length of filenames */
    uint64_t    f_frsize;   /* fragment size */
    uint32_t    f_flags;    /* mount flags */
    uint64_t    f_spare[4]; /* spare for later */
};

#ifdef __cplusplus
extern "C" {
#endif

int statfs(const char *path, struct statfs *buf);
int fstatfs(int fd, struct statfs *buf);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_STATFS_H */
