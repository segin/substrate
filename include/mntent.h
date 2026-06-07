/*
 * <mntent.h> — filesystem-table (fstab/mtab) access.
 *
 * Substrate libc provides the reentrant getmntent_r(3), which parses one
 * mount-table line into a caller-supplied `struct mntent` plus scratch buffer.
 * setmntent()/endmntent() open and close the table; getmntent() is the
 * non-reentrant convenience form, and hasmntopt() searches the options field.
 */

#ifndef _MNTENT_H
#define _MNTENT_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MNTTAB    "/etc/fstab"
#define MOUNTED   "/etc/mtab"

/* Well-known mnt_type / mnt_opts values. */
#define MNTTYPE_IGNORE  "ignore"
#define MNTTYPE_SWAP    "swap"
#define MNTTYPE_NFS     "nfs"
#define MNTTYPE_RFS     "rfs"    /* SVR4 Remote File Sharing */
#define MNTTYPE_LOFS    "lofs"   /* loopback filesystem */
#define MNTOPT_DEFAULTS "defaults"
#define MNTOPT_RO       "ro"
#define MNTOPT_RW       "rw"
#define MNTOPT_NOAUTO   "noauto"

struct mntent {
    char *mnt_fsname;   /* device or remote filesystem */
    char *mnt_dir;      /* mount point */
    char *mnt_type;     /* filesystem type */
    char *mnt_opts;     /* mount options (comma-separated) */
    int   mnt_freq;     /* dump frequency */
    int   mnt_passno;   /* fsck pass number */
};

FILE          *setmntent(const char *filename, const char *type);
int            endmntent(FILE *stream);
struct mntent *getmntent(FILE *stream);
struct mntent *getmntent_r(FILE *stream, struct mntent *mntbuf,
                           char *buf, int buflen);
char          *hasmntopt(const struct mntent *mnt, const char *opt);

#ifdef __cplusplus
}
#endif

#endif /* _MNTENT_H */
