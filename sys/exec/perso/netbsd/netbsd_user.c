#include "netbsd_user.h"
#include <sys/kern_syscalls.h>
#include <sys/syscall_impl.h>
#include <sys/stat.h>
#include <sys/copy.h>
#include <sys/fcntl.h>
#include <sys/namei.h>
#include <string.h>
#include <sys/errno.h>
#include <stddef.h>
#include <sys/sysarch.h>

extern int sys_sysarch(int op, void *parms);

/* NetBSD at-flags identical to FreeBSD/POSIX (NOFOLLOW=0x200,
 * REMOVEDIR=0x800).  Substrate native at-flags differ (NOFOLLOW=0x100,
 * REMOVEDIR=0x200), so translate. */
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif
#define NBSD_AT_EACCESS          0x0100
#define NBSD_AT_SYMLINK_NOFOLLOW 0x0200
#define NBSD_AT_SYMLINK_FOLLOW   0x0400
#define NBSD_AT_REMOVEDIR        0x0800
static int netbsd_atflags(int f) {
    int k = 0;
    if (f & NBSD_AT_SYMLINK_NOFOLLOW) k |= AT_SYMLINK_NOFOLLOW;
    if (f & NBSD_AT_REMOVEDIR)        k |= AT_REMOVEDIR;
    return k;
}

/* Helper to translate native stat to NetBSD stat */
static void translate_stat_to_netbsd(const struct stat *native, struct netbsd_stat *nbsd) {
    memset(nbsd, 0, sizeof(*nbsd));
    nbsd->st_dev = native->st_dev;
    nbsd->st_ino = (uint32_t)native->st_ino;
    nbsd->st_mode = native->st_mode;
    nbsd->st_nlink = native->st_nlink;
    nbsd->st_uid = native->st_uid;
    nbsd->st_gid = native->st_gid;
    nbsd->st_rdev = native->st_rdev;
    nbsd->st_atime = (int32_t)native->st_atime;
    nbsd->st_atimensec = (int32_t)native->st_atime_nsec;
    nbsd->st_mtime = (int32_t)native->st_mtime;
    nbsd->st_mtimensec = (int32_t)native->st_mtime_nsec;
    nbsd->st_ctime = (int32_t)native->st_ctime;
    nbsd->st_ctimensec = (int32_t)native->st_ctime_nsec;
    nbsd->st_size = native->st_size;
    nbsd->st_blocks = native->st_blocks;
    nbsd->st_blksize = native->st_blksize;
    nbsd->st_flags = 0;
    nbsd->st_gen = 0;
}

/* Helper to translate native stat to NetBSD stat43 (older compat) */
static void translate_stat_to_netbsd43(const struct stat *native, struct netbsd_stat43 *nbsd43) {
    memset(nbsd43, 0, sizeof(*nbsd43));
    nbsd43->st_dev = (uint16_t)native->st_dev;
    nbsd43->st_ino = (uint32_t)native->st_ino;
    nbsd43->st_mode = native->st_mode;
    nbsd43->st_nlink = native->st_nlink;
    nbsd43->st_uid = native->st_uid;
    nbsd43->st_gid = native->st_gid;
    nbsd43->st_rdev = (uint16_t)native->st_rdev;
    nbsd43->st_size = (int32_t)native->st_size;
    nbsd43->st_atime = (int32_t)native->st_atime;
    nbsd43->st_mtime = (int32_t)native->st_mtime;
    nbsd43->st_ctime = (int32_t)native->st_ctime;
    nbsd43->st_blksize = (int32_t)native->st_blksize;
    nbsd43->st_blocks = (int32_t)native->st_blocks;
    nbsd43->st_flags = 0;
    nbsd43->st_gen = 0;
}

int netbsd_sys_stat(const char *path, struct netbsd_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat knbsd;
        translate_stat_to_netbsd(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_lstat(const char *path, struct netbsd_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat knbsd;
        translate_stat_to_netbsd(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_fstat(int fd, struct netbsd_stat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct netbsd_stat knbsd;
        translate_stat_to_netbsd(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14; // EFAULT
    }
    return ret;
}

/* NetBSD 6+ "wide" stat translator — fills the 124-byte struct
 * netbsd_stat50 layout with 64-bit ino_t/dev_t and 12-byte timespecs. */
static void translate_stat_to_netbsd50(const struct stat *native,
                                       struct netbsd_stat50 *nbsd) {
    memset(nbsd, 0, sizeof(*nbsd));
    nbsd->st_dev   = native->st_dev;
    nbsd->st_mode  = native->st_mode;
    nbsd->st_ino   = native->st_ino;
    nbsd->st_nlink = native->st_nlink;
    nbsd->st_uid   = native->st_uid;
    nbsd->st_gid   = native->st_gid;
    nbsd->st_rdev  = native->st_rdev;
    nbsd->st_atim.tv_sec  = native->st_atime;
    nbsd->st_atim.tv_nsec = (int32_t)native->st_atime_nsec;
    nbsd->st_mtim.tv_sec  = native->st_mtime;
    nbsd->st_mtim.tv_nsec = (int32_t)native->st_mtime_nsec;
    nbsd->st_ctim.tv_sec  = native->st_ctime;
    nbsd->st_ctim.tv_nsec = (int32_t)native->st_ctime_nsec;
    /* No native birthtime — mirror ctime, the closest analogue. */
    nbsd->st_birthtim.tv_sec  = native->st_ctime;
    nbsd->st_birthtim.tv_nsec = (int32_t)native->st_ctime_nsec;
    nbsd->st_size    = native->st_size;
    nbsd->st_blocks  = native->st_blocks;
    nbsd->st_blksize = native->st_blksize;
    nbsd->st_flags   = 0;
    nbsd->st_gen     = 0;
}

int netbsd_sys_stat50(const char *path, struct netbsd_stat50 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat50 knbsd;
        translate_stat_to_netbsd50(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14;
    }
    return ret;
}

int netbsd_sys_lstat50(const char *path, struct netbsd_stat50 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat50 knbsd;
        translate_stat_to_netbsd50(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14;
    }
    return ret;
}

int netbsd_sys_fstat50(int fd, struct netbsd_stat50 *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct netbsd_stat50 knbsd;
        translate_stat_to_netbsd50(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14;
    }
    return ret;
}

int netbsd_sys_compat_stat(const char *path, struct netbsd_stat43 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat43 knbsd43;
        translate_stat_to_netbsd43(&native, &knbsd43);
        if (copyout(&knbsd43, buf, sizeof(knbsd43)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_compat_lstat(const char *path, struct netbsd_stat43 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat43 knbsd43;
        translate_stat_to_netbsd43(&native, &knbsd43);
        if (copyout(&knbsd43, buf, sizeof(knbsd43)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_compat_fstat(int fd, struct netbsd_stat43 *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct netbsd_stat43 knbsd43;
        translate_stat_to_netbsd43(&native, &knbsd43);
        if (copyout(&knbsd43, buf, sizeof(knbsd43)) != 0) return -14; // EFAULT
    }
    return ret;
}

/* ------------------------------------------------------------------
 * NetBSD chown/chmod family
 *
 * NetBSD chown(2) follows symlinks, lchown does not (POSIX semantics).
 * Substrate's native sys_chown doesn't exist; sys_lchown is no-follow,
 * so route through sys_fchownat with flag=0 for follow.  lchmod has no
 * native equivalent — wrap kern_chmodat with AT_SYMLINK_NOFOLLOW.
 * ------------------------------------------------------------------ */

int netbsd_sys_chown(const char *path, int uid, int gid) {
    return sys_fchownat(AT_FDCWD, path, uid, gid, 0);
}

int netbsd_sys_lchmod(const char *path, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_chmodat(AT_FDCWD, kpath, mode, AT_SYMLINK_NOFOLLOW);
}

int netbsd_sys_fchmodat(int dirfd, const char *path, int mode, int flag) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_chmodat(dirfd, kpath, mode, netbsd_atflags(flag));
}

int netbsd_sys_fchownat(int dirfd, const char *path, int uid, int gid, int flag) {
    return sys_fchownat(dirfd, path, uid, gid, netbsd_atflags(flag));
}

/*
 * NetBSD i386 mmap shim — slot 197 takes a `long PAD` argument
 * between fd and pos so off_t lands 8-byte aligned on the user
 * stack.  Substrate's native sys_mmap signature has no pad; drop
 * the pad and forward the rest.
 *
 * Signature per NetBSD's syscalls.master line 438:
 *   void *mmap(void *addr, size_t len, int prot, int flags,
 *              int fd, long pad, off_t pos);
 *
 * NetBSD MAP_* flag values diverge from Substrate's (Linux-style)
 * native values most importantly for MAP_ANON: NetBSD uses 0x1000,
 * Substrate uses 0x020.  Without translation our sys_mmap saw fd=-1
 * without MAP_ANONYMOUS set and refused with EPERM ("Cannot map
 * anonymous memory").  Translate the bits that differ; MAP_SHARED /
 * MAP_PRIVATE / MAP_FIXED match by accident at 0x001/0x002/0x010.
 */
#define NETBSD_MAP_ANON     0x1000
#define NETBSD_MAP_STACK    0x2000
#define KERN_MAP_ANONYMOUS  0x020
#define KERN_MAP_FIXED      0x010
#define KERN_MAP_PRIVATE    0x002
#define KERN_MAP_SHARED     0x001
void *netbsd_sys_mmap(void *addr, size_t len, int prot, int flags,
                      int fd, long pad, uint64_t pos) {
    (void)pad;
    int kflags = flags & (KERN_MAP_SHARED | KERN_MAP_PRIVATE | KERN_MAP_FIXED);
    if (flags & NETBSD_MAP_ANON)
        kflags |= KERN_MAP_ANONYMOUS;
    /* NetBSD MAP_STACK is mostly advisory (the kernel allocates a
     * grow-down region).  Closest fit: anonymous private mapping. */
    if (flags & NETBSD_MAP_STACK)
        kflags |= KERN_MAP_PRIVATE | KERN_MAP_ANONYMOUS;
    return sys_mmap(addr, len, prot, kflags, fd, pos);
}

/* _lwp_setprivate(addr) — NetBSD's i386 TLS install.  ld.elf_so calls
 * this on its very first syscall after exec; the kernel must point
 * %gs:0 at the supplied TCB or every TLS access faults.  Mirrors the
 * cpu_lwp_setprivate() path in NetBSD's machine-dependent code. */
int netbsd_sys_lwp_setprivate(uintptr_t tcb) {
    return i386_set_gsbase((uint32_t)tcb);
}
