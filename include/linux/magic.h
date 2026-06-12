#ifndef _LINUX_MAGIC_H
#define _LINUX_MAGIC_H

/*
 * Filesystem super-block magic numbers, as exposed by Linux's
 * <linux/magic.h>.  Code that walks statfs(2)'s f_type (CDE's dtcopy) reaches
 * for this header.  The substrate-supported set already lives in
 * <sys/statfs.h>; the definitions here are #ifndef-guarded so including both
 * headers is harmless, and the rest mirror the upstream Linux values for
 * source compatibility.
 */

#ifndef ADFS_SUPER_MAGIC
#define ADFS_SUPER_MAGIC      0xadf5
#endif
#ifndef AFFS_SUPER_MAGIC
#define AFFS_SUPER_MAGIC      0xadff
#endif
#ifndef AUTOFS_SUPER_MAGIC
#define AUTOFS_SUPER_MAGIC    0x0187
#endif
#ifndef CODA_SUPER_MAGIC
#define CODA_SUPER_MAGIC      0x73757245
#endif
#ifndef CRAMFS_MAGIC
#define CRAMFS_MAGIC          0x28cd3d45
#endif
#ifndef DEVPTS_SUPER_MAGIC
#define DEVPTS_SUPER_MAGIC    0x1cd1
#endif
#ifndef EFS_SUPER_MAGIC
#define EFS_SUPER_MAGIC       0x414a53
#endif
#ifndef EXT2_SUPER_MAGIC
#define EXT2_SUPER_MAGIC      0xEF53
#endif
#ifndef EXT3_SUPER_MAGIC
#define EXT3_SUPER_MAGIC      0xEF53
#endif
#ifndef EXT4_SUPER_MAGIC
#define EXT4_SUPER_MAGIC      0xEF53
#endif
#ifndef HPFS_SUPER_MAGIC
#define HPFS_SUPER_MAGIC      0xf995e849
#endif
#ifndef ISOFS_SUPER_MAGIC
#define ISOFS_SUPER_MAGIC     0x9660
#endif
#ifndef JFFS2_SUPER_MAGIC
#define JFFS2_SUPER_MAGIC     0x72b6
#endif
#ifndef MINIX_SUPER_MAGIC
#define MINIX_SUPER_MAGIC     0x137F
#endif
#ifndef MINIX2_SUPER_MAGIC
#define MINIX2_SUPER_MAGIC    0x2468
#endif
#ifndef MSDOS_SUPER_MAGIC
#define MSDOS_SUPER_MAGIC     0x4d44
#endif
#ifndef NCP_SUPER_MAGIC
#define NCP_SUPER_MAGIC       0x564c
#endif
#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC       0x6969
#endif
#ifndef NTFS_SB_MAGIC
#define NTFS_SB_MAGIC         0x5346544e
#endif
#ifndef PROC_SUPER_MAGIC
#define PROC_SUPER_MAGIC      0x9fa0
#endif
#ifndef QNX4_SUPER_MAGIC
#define QNX4_SUPER_MAGIC      0x002f
#endif
#ifndef REISERFS_SUPER_MAGIC
#define REISERFS_SUPER_MAGIC  0x52654973
#endif
#ifndef SMB_SUPER_MAGIC
#define SMB_SUPER_MAGIC       0x517B
#endif
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC           0x01021994
#endif
#ifndef USBDEVICE_SUPER_MAGIC
#define USBDEVICE_SUPER_MAGIC 0x9fa2
#endif
#ifndef XENFS_SUPER_MAGIC
#define XENFS_SUPER_MAGIC     0xabba1974
#endif
#ifndef XFS_SUPER_MAGIC
#define XFS_SUPER_MAGIC       0x58465342
#endif

/* Network / legacy filesystem magics referenced by ported code
 * (e.g. libarchive's archive_read_disk_posix fs-type detection). */
#ifndef AFS_SUPER_MAGIC
#define AFS_SUPER_MAGIC       0x5346414F
#endif
#ifndef CIFS_SUPER_MAGIC
#define CIFS_SUPER_MAGIC      0xFF534D42
#endif
#ifndef DEVFS_SUPER_MAGIC
#define DEVFS_SUPER_MAGIC     0x1373
#endif

#endif /* _LINUX_MAGIC_H */
