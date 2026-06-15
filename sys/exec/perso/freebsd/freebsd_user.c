#include "freebsd_user.h"
#include <sys/kern_syscalls.h>
#include <sys/syscall_impl.h>
#include <sys/copy.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/namei.h>
#include <sys/errno.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>
#include <string.h>

/* FreeBSD Stat Translation */
static void translate_stat_to_freebsd(struct stat *native, struct freebsd_stat *fbsd) {
    memset(fbsd, 0, sizeof(struct freebsd_stat));
    fbsd->st_dev = native->st_dev;
    fbsd->st_ino = native->st_ino;
    fbsd->st_mode = (uint16_t)native->st_mode;
    fbsd->st_nlink = (uint16_t)native->st_nlink;
    fbsd->st_uid = native->st_uid;
    fbsd->st_gid = native->st_gid;
    fbsd->st_rdev = native->st_rdev;
    fbsd->st_atim.tv_sec = (int32_t)native->st_atime;
    fbsd->st_atim.tv_nsec = (int32_t)native->st_atime_nsec;
    fbsd->st_mtim.tv_sec = (int32_t)native->st_mtime;
    fbsd->st_mtim.tv_nsec = (int32_t)native->st_mtime_nsec;
    fbsd->st_ctim.tv_sec = (int32_t)native->st_ctime;
    fbsd->st_ctim.tv_nsec = (int32_t)native->st_ctime_nsec;
    fbsd->st_size = native->st_size;
    fbsd->st_blocks = native->st_blocks;
    fbsd->st_blksize = native->st_blksize;
}

int freebsd_sys_stat(const char *path, struct freebsd_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct freebsd_stat kfbsd;
        translate_stat_to_freebsd(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd_stat)) != 0) return -14;
    }
    return ret;
}

int freebsd_sys_lstat(const char *path, struct freebsd_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct freebsd_stat kfbsd;
        translate_stat_to_freebsd(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd_stat)) != 0) return -14;
    }
    return ret;
}

int freebsd_sys_fstat(int fd, struct freebsd_stat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct freebsd_stat kfbsd;
        translate_stat_to_freebsd(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd_stat)) != 0) return -14;
    }
    return ret;
}

/* freebsd_atflags forward decl — defined further down with the other oflag/atflag helpers. */
static int freebsd_atflags(int f);
static void translate_stat_to_freebsd13(struct stat *native, struct freebsd13_stat *fbsd);

/* FreeBSD 12+ fstatat (syscall 552).  Uses the wide stat layout — the
 * one Substrate types as `struct freebsd13_stat` after offset-by-offset
 * verification against FreeBSD 14.3/i386 /usr/include/sys/stat.h.
 * (The other "struct freebsd_stat" in our headers has a 4-byte
 * tv_sec in struct freebsd_timespec; FreeBSD 14 i386 has int64_t
 * time_t and 12-byte timespec, which silently shifts every field
 * after st_atim.  Userland reads garbage, fts(3) sees st_size=0 on
 * directories and gives up before ever calling getdirentries — this
 * was the visible "ls only prints `.`" symptom.)
 *
 * Wire both COMPAT11 syscall 493 and modern 552 at the same handler
 * since FreeBSD-13/14 binaries don't issue 493 anyway. */
int freebsd_sys_fstatat(int dirfd, const char *path, struct freebsd13_stat *buf, int flags) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    struct stat native;
    int ret = kern_fstatat(dirfd, kpath, &native, freebsd_atflags(flags));
    if (ret == 0) {
        struct freebsd13_stat kfbsd;
        translate_stat_to_freebsd13(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd13_stat)) != 0) return -14;
    }
    return ret;
}

/* FreeBSD 11 Stat Translation */
static void translate_stat_to_freebsd11(struct stat *native, struct freebsd11_stat *fbsd) {
    memset(fbsd, 0, sizeof(struct freebsd11_stat));
    fbsd->st_dev = native->st_dev;
    fbsd->st_ino = (uint32_t)native->st_ino;
    fbsd->st_mode = (uint16_t)native->st_mode;
    fbsd->st_nlink = (uint16_t)native->st_nlink;
    fbsd->st_uid = native->st_uid;
    fbsd->st_gid = native->st_gid;
    fbsd->st_rdev = native->st_rdev;
    fbsd->st_atim.tv_sec = (int32_t)native->st_atime;
    fbsd->st_atim.tv_nsec = (int32_t)native->st_atime_nsec;
    fbsd->st_mtim.tv_sec = (int32_t)native->st_mtime;
    fbsd->st_mtim.tv_nsec = (int32_t)native->st_mtime_nsec;
    fbsd->st_ctim.tv_sec = (int32_t)native->st_ctime;
    fbsd->st_ctim.tv_nsec = (int32_t)native->st_ctime_nsec;
    fbsd->st_size = native->st_size;
    fbsd->st_blocks = native->st_blocks;
    fbsd->st_blksize = native->st_blksize;
}

int freebsd_sys_stat_v11(const char *path, struct freebsd11_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct freebsd11_stat kfbsd;
        translate_stat_to_freebsd11(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd11_stat)) != 0) return -14;
    }
    return ret;
}

int freebsd_sys_lstat_v11(const char *path, struct freebsd11_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct freebsd11_stat kfbsd;
        translate_stat_to_freebsd11(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd11_stat)) != 0) return -14;
    }
    return ret;
}

int freebsd_sys_fstat_v11(int fd, struct freebsd11_stat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct freebsd11_stat kfbsd;
        translate_stat_to_freebsd11(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd11_stat)) != 0) return -14;
    }
    return ret;
}

/* FreeBSD 13 Stat Translation (64-bit timestamps, i386 ABI) */
static void translate_stat_to_freebsd13(struct stat *native, struct freebsd13_stat *fbsd) {
    memset(fbsd, 0, sizeof(struct freebsd13_stat));
    fbsd->st_dev      = native->st_dev;
    fbsd->st_ino      = native->st_ino;
    fbsd->st_mode     = (uint16_t)native->st_mode;
    fbsd->st_nlink    = native->st_nlink;
    fbsd->st_uid      = native->st_uid;
    fbsd->st_gid      = native->st_gid;
    fbsd->st_rdev     = native->st_rdev;
    fbsd->st_atim.tv_sec  = native->st_atime;
    fbsd->st_atim.tv_nsec = (int32_t)native->st_atime_nsec;
    fbsd->st_mtim.tv_sec  = native->st_mtime;
    fbsd->st_mtim.tv_nsec = (int32_t)native->st_mtime_nsec;
    fbsd->st_ctim.tv_sec  = native->st_ctime;
    fbsd->st_ctim.tv_nsec = (int32_t)native->st_ctime_nsec;
    fbsd->st_birthtim.tv_sec  = native->st_ctime;
    fbsd->st_birthtim.tv_nsec = (int32_t)native->st_ctime_nsec;
    fbsd->st_size     = native->st_size;
    fbsd->st_blocks   = native->st_blocks;
    fbsd->st_blksize  = native->st_blksize;
    fbsd->st_flags    = 0;
    fbsd->st_gen      = 0;
}

int freebsd_sys_stat_v13(const char *path, struct freebsd13_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct freebsd13_stat kfbsd;
        translate_stat_to_freebsd13(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd13_stat)) != 0) return -14;
    }
    return ret;
}

int freebsd_sys_lstat_v13(const char *path, struct freebsd13_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct freebsd13_stat kfbsd;
        translate_stat_to_freebsd13(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd13_stat)) != 0) return -14;
    }
    return ret;
}

int freebsd_sys_fstat_v13(int fd, struct freebsd13_stat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct freebsd13_stat kfbsd;
        translate_stat_to_freebsd13(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd13_stat)) != 0) return -14;
    }
    return ret;
}

/*
 * FreeBSD open(2) flag values differ from Linux/kernel values.
 * Translate before calling kern_openat.
 *
 * FreeBSD → kernel mapping:
 *   O_NONBLOCK  0x004 → 0x800
 *   O_APPEND    0x008 → 0x400
 *   O_SYNC      0x080 → 0x1000
 *   O_NOFOLLOW  0x100 → 0x20000
 *   O_CREAT     0x200 → 0x040
 *   O_TRUNC     0x400 → 0x200
 *   O_EXCL      0x800 → 0x080
 *   O_NOCTTY    0x8000 → 0x100
 *   O_DIRECTORY 0x20000 → 0x10000
 *   O_CLOEXEC   0x100000 → 0x80000
 */
static int freebsd_oflags(int f) {
    int k = f & 0x3;
    if (f & 0x000004) k |= 0x000800;
    if (f & 0x000008) k |= 0x000400;
    if (f & 0x000080) k |= 0x001000;
    if (f & 0x000100) k |= 0x020000;
    if (f & 0x000200) k |= 0x000040;
    if (f & 0x000400) k |= 0x000200;
    if (f & 0x000800) k |= 0x000080;
    if (f & 0x008000) k |= 0x000100;
    if (f & 0x020000) k |= 0x010000;
    if (f & 0x100000) k |= 0x080000;
    return k;
}

/*
 * Translate FreeBSD at-flags (sys/sys/fcntl.h) to Substrate's:
 *   FreeBSD                       Substrate (Linux-style)
 *   AT_EACCESS         0x0100  →  (no equivalent; check using ruid)
 *   AT_SYMLINK_NOFOLLOW 0x0200 →  AT_SYMLINK_NOFOLLOW 0x100
 *   AT_SYMLINK_FOLLOW   0x0400 →  (default for linkat target)
 *   AT_REMOVEDIR        0x0800 →  AT_REMOVEDIR        0x200
 */
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif
#define FBSD_AT_EACCESS          0x0100
#define FBSD_AT_SYMLINK_NOFOLLOW 0x0200
#define FBSD_AT_SYMLINK_FOLLOW   0x0400
#define FBSD_AT_REMOVEDIR        0x0800
static int freebsd_atflags(int f) {
    int k = 0;
    if (f & FBSD_AT_SYMLINK_NOFOLLOW) k |= AT_SYMLINK_NOFOLLOW;
    if (f & FBSD_AT_REMOVEDIR)        k |= AT_REMOVEDIR;
    /* AT_EACCESS not modeled separately by Substrate — checks proceed using
     * effective creds anyway, so callers see the same answer for the common
     * case where ruid == euid. */
    return k;
}

int freebsd_sys_open(const char *path, int flags, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_openat(AT_FDCWD, kpath, freebsd_oflags(flags), mode);
}

/*
 * pipe2 / dup3 thunks.  FreeBSD uses different numeric values for
 * O_CLOEXEC / O_NONBLOCK than Substrate, so we translate before
 * dispatching to the native syscall.
 */
#define FREEBSD_O_NONBLOCK 0x000004
#define FREEBSD_O_CLOEXEC  0x100000

int freebsd_sys_pipe2(int *fds, int flags) {
    int native = 0;
    if (flags & FREEBSD_O_CLOEXEC)  native |= O_CLOEXEC;
    if (flags & FREEBSD_O_NONBLOCK) native |= O_NONBLOCK;
    return sys_pipe2(fds, native);
}

int freebsd_sys_dup3(int oldfd, int newfd, int flags) {
    int native = 0;
    if (flags & FREEBSD_O_CLOEXEC) native |= O_CLOEXEC;
    return sys_dup3(oldfd, newfd, native);
}

int freebsd_sys_openat(int dirfd, const char *path, int flags, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_openat(dirfd, kpath, freebsd_oflags(flags), mode);
}

/* ----------------------------------------------------------------------
 * FreeBSD at-family wrappers
 *
 * Where Substrate has a kern_<X>at helper, the wrapper just translates
 * FreeBSD at-flags and forwards.  Where it doesn't (linkat, renameat,
 * symlinkat, faccessat, fchownat for the wide-arg case), we fall back
 * to the non-at variant when the dirfd is AT_FDCWD — that covers the
 * vast majority of userland paths (sh, coreutils, build systems).
 * Non-AT_FDCWD dirfds fall through with -ENOSYS until proper kernel
 * helpers grow for them.
 * ---------------------------------------------------------------------- */

int freebsd_sys_faccessat(int dirfd, const char *path, int amode, int flag) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    (void)flag;  /* AT_EACCESS handled implicitly */
    if (dirfd == AT_FDCWD) return kern_access(kpath, amode);
    return -ENOSYS;
}

int freebsd_sys_fchmodat(int dirfd, const char *path, int mode, int flag) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_chmodat(dirfd, kpath, mode, freebsd_atflags(flag));
}

int freebsd_sys_fchownat(int dirfd, const char *path, int uid, int gid, int flag) {
    /* sys_fchownat does its own copyinstr and supports arbitrary dirfd.
     * Just translate the flag bits and forward. */
    return sys_fchownat(dirfd, path, uid, gid, freebsd_atflags(flag));
}

int freebsd_sys_linkat(int olddir, const char *oldpath, int newdir, const char *newpath, int flag) {
    char kold[256], knew[256];
    if (copyinstr(oldpath, kold, sizeof(kold), NULL) != 0) return -EFAULT;
    if (copyinstr(newpath, knew, sizeof(knew), NULL) != 0) return -EFAULT;
    (void)flag;  /* AT_SYMLINK_FOLLOW currently default */
    if (olddir == AT_FDCWD && newdir == AT_FDCWD) return kern_link(kold, knew);
    return -ENOSYS;
}

int freebsd_sys_mkdirat(int dirfd, const char *path, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_mkdirat(dirfd, kpath, mode);
}

int freebsd_sys_readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz) {
    /* sys_readlinkat does its own copyinstr/copyout. */
    return sys_readlinkat(dirfd, path, buf, bufsiz);
}

int freebsd_sys_renameat(int olddir, const char *oldpath, int newdir, const char *newpath) {
    char kold[256], knew[256];
    if (copyinstr(oldpath, kold, sizeof(kold), NULL) != 0) return -EFAULT;
    if (copyinstr(newpath, knew, sizeof(knew), NULL) != 0) return -EFAULT;
    if (olddir == AT_FDCWD && newdir == AT_FDCWD) return kern_rename(kold, knew);
    return -ENOSYS;
}

int freebsd_sys_symlinkat(const char *target, int newdir, const char *newpath) {
    char ktgt[256], knew[256];
    if (copyinstr(target,  ktgt, sizeof(ktgt), NULL) != 0) return -EFAULT;
    if (copyinstr(newpath, knew, sizeof(knew), NULL) != 0) return -EFAULT;
    if (newdir == AT_FDCWD) return kern_symlink(ktgt, knew);
    return -ENOSYS;
}

int freebsd_sys_unlinkat(int dirfd, const char *path, int flag) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_unlinkat(dirfd, kpath, freebsd_atflags(flag));
}

/* ------------------------------------------------------------------
 * chown/chmod family — wire each FreeBSD syscall number to a handler
 * with the right symlink-follow semantics.  Substrate has:
 *   sys_chmod(path, mode)              — follows symlinks
 *   sys_fchmod(fd, mode)               — by fd
 *   sys_lchown(path, uid, gid)         — does NOT follow symlinks
 *   sys_fchown(fd, uid, gid)           — by fd
 *   sys_fchownat(dirfd, path, uid, gid, flag) — handles both modes
 *   kern_chmodat(dirfd, path, mode, flag)     — handles both modes
 * Missing in native: a chown(path) that DOES follow, and an lchmod
 * that does NOT.  We synthesize them here using the at-variants.
 * ------------------------------------------------------------------ */

int freebsd_sys_chown(const char *path, int uid, int gid) {
    /* FreeBSD chown(2) follows symlinks.  sys_fchownat with flag=0
     * gives that semantic and already does its own copyinstr. */
    return sys_fchownat(AT_FDCWD, path, uid, gid, 0);
}

int freebsd_sys_lchmod(const char *path, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_chmodat(AT_FDCWD, kpath, mode, AT_SYMLINK_NOFOLLOW);
}

int freebsd_sys_fstatat_v13(int dirfd, const char *path, struct freebsd13_stat *buf, int flags) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    struct stat native;
    int ret = kern_fstatat(dirfd, kpath, &native, freebsd_atflags(flags));
    if (ret == 0) {
        struct freebsd13_stat kfbsd;
        translate_stat_to_freebsd13(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd13_stat)) != 0) return -EFAULT;
    }
    return ret;
}

/* COMPAT11 fstatat (syscall 493) — narrow ino_t/time_t.  Same dirfd
 * semantics as the modern variant; only the output struct differs. */
int freebsd_sys_fstatat_v11(int dirfd, const char *path, struct freebsd11_stat *buf, int flags) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    struct stat native;
    int ret = kern_fstatat(dirfd, kpath, &native, freebsd_atflags(flags));
    if (ret == 0) {
        struct freebsd11_stat kfbsd;
        translate_stat_to_freebsd11(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd11_stat)) != 0) return -EFAULT;
    }
    return ret;
}

/* Pre-FreeBSD-5 ostat translator + the three syscall handlers (38/40/62).
 * No native equivalent for st_flags or st_gen; zero them.  16-bit fields
 * silently truncate values that would overflow on a modern filesystem. */
static void translate_stat_to_ostat(struct stat *native, struct freebsd_ostat *o) {
    memset(o, 0, sizeof(*o));
    o->st_dev = (uint16_t)native->st_dev;
    o->st_ino = (uint32_t)native->st_ino;
    o->st_mode = (uint16_t)native->st_mode;
    o->st_nlink = (uint16_t)native->st_nlink;
    o->st_uid = (uint16_t)native->st_uid;
    o->st_gid = (uint16_t)native->st_gid;
    o->st_rdev = (uint16_t)native->st_rdev;
    o->st_size = (int32_t)native->st_size;
    o->st_atim_sec = (int32_t)native->st_atime;
    o->st_atim_nsec = (int32_t)native->st_atime_nsec;
    o->st_mtim_sec = (int32_t)native->st_mtime;
    o->st_mtim_nsec = (int32_t)native->st_mtime_nsec;
    o->st_ctim_sec = (int32_t)native->st_ctime;
    o->st_ctim_nsec = (int32_t)native->st_ctime_nsec;
    o->st_blksize = (int32_t)native->st_blksize;
    o->st_blocks = (int32_t)native->st_blocks;
}

int freebsd_sys_ostat(const char *path, struct freebsd_ostat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct freebsd_ostat ko;
        translate_stat_to_ostat(&native, &ko);
        if (copyout(&ko, buf, sizeof(ko)) != 0) return -EFAULT;
    }
    return ret;
}

int freebsd_sys_olstat(const char *path, struct freebsd_ostat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct freebsd_ostat ko;
        translate_stat_to_ostat(&native, &ko);
        if (copyout(&ko, buf, sizeof(ko)) != 0) return -EFAULT;
    }
    return ret;
}

int freebsd_sys_ofstat(int fd, struct freebsd_ostat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct freebsd_ostat ko;
        translate_stat_to_ostat(&native, &ko);
        if (copyout(&ko, buf, sizeof(ko)) != 0) return -EFAULT;
    }
    return ret;
}

/* COMPAT11 getdirentries (syscall 196).  Same shape as the modern
 * variant but emits the narrow freebsd11_dirent layout (32-bit ino,
 * no record-level alignment padding).  basep is `long *` (32-bit on
 * i386). */
ssize_t freebsd_sys_getdirentries_v11(int fd, char *buf, unsigned int nbytes, int32_t *basep) {
    if (fd < 0 || fd >= MAX_FD) return -EBADF;
    if (!current_process) return -EINVAL;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -EBADF;

    if (basep) {
        int32_t start_off = (int32_t)f->f_offset;
        if (copyout(&start_off, basep, sizeof(int32_t)) != 0) return -EFAULT;
    }

    if (nbytes > 65536) nbytes = 65536;
    char *kbuf = kmalloc(nbytes);
    if (!kbuf) return -ENOMEM;

    unsigned int bpos = 0;
    struct freebsd11_dirent kfbd;

    for (;;) {
        struct dirent *d = readdir_fs((fs_node_t *)f->f_data, f->f_offset);
        if (!d) break;

        /* readdir_fs's index is an opaque BYTE OFFSET (ext2 is byte-offset
         * based -- see kern_getdents); advance by the entry's d_off cursor,
         * not a fixed 1, or we re-read mid-record forever. */
        uint64_t cur_off  = (uint64_t)f->f_offset;
        uint64_t next_off = (d->d_off > cur_off) ? d->d_off : cur_off + 1;

        uint8_t namlen = (uint8_t)strnlen(d->d_name, 255);
        /* header=8 bytes + name + NUL, aligned to 4 bytes. */
        uint16_t reclen = (uint16_t)(((size_t)8 + namlen + 1 + 3) & ~(size_t)3);

        if (bpos + reclen > nbytes) {
            if (bpos == 0) { kfree(kbuf, nbytes); return -EINVAL; }
            break;
        }

        memset(&kfbd, 0, reclen);
        kfbd.d_fileno = (uint32_t)d->d_ino;
        kfbd.d_reclen = reclen;
        kfbd.d_type   = d->d_type;
        kfbd.d_namlen = namlen;
        memcpy(kfbd.d_name, d->d_name, namlen);
        kfbd.d_name[namlen] = '\0';

        memcpy(kbuf + bpos, &kfbd, reclen);
        bpos += reclen;
        f->f_offset = (off_t)next_off;
    }

    if (bpos == 0) { kfree(kbuf, nbytes); return 0; }

    if (copyout(kbuf, buf, bpos) != 0) {
        kfree(kbuf, nbytes);
        return -EFAULT;
    }
    kfree(kbuf, nbytes);
    return (ssize_t)bpos;
}

/*
 * getdirentries_freebsd13 (syscall 554): read directory entries in FreeBSD 14 format.
 * Returns FreeBSD struct dirent (64-bit ino_t, 8-byte aligned records).
 * basep receives the file offset at the start of the call.
 */
ssize_t freebsd_sys_getdirentries(int fd, char *buf, size_t nbytes, int64_t *basep) {
    if (fd < 0 || fd >= MAX_FD) return -EBADF;
    if (!current_process) return -EINVAL;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -EBADF;

    if (basep) {
        int64_t start_off = (int64_t)f->f_offset;
        if (copyout(&start_off, basep, sizeof(int64_t)) != 0) return -EFAULT;
    }

    if (nbytes > 65536) nbytes = 65536;
    char *kbuf = kmalloc(nbytes);
    if (!kbuf) return -ENOMEM;

    size_t bpos = 0;
    struct freebsd_dirent kfbd;

    for (;;) {
        struct dirent *d = readdir_fs((fs_node_t *)f->f_data, f->f_offset);
        if (!d) break;

        /* readdir_fs's index is an opaque BYTE OFFSET into the directory
         * (ext2 is byte-offset/cookie based, see kern_getdents); the next
         * cursor is the entry's d_off, with a +1 fallback for index-based
         * filesystems that leave d_off == 0.  Advancing by a fixed 1 lands
         * mid-record and re-reads the same entry forever -- the FreeBSD
         * rcorder hang reading /etc/rc.d. */
        uint64_t cur_off  = (uint64_t)f->f_offset;
        uint64_t next_off = (d->d_off > cur_off) ? d->d_off : cur_off + 1;

        uint16_t namlen = (uint16_t)strnlen(d->d_name, 255);
        /* header=24 bytes + name + NUL, aligned to 8 bytes */
        uint16_t reclen = (uint16_t)(((size_t)24 + namlen + 1 + 7) & ~(size_t)7);

        if (bpos + reclen > nbytes) {
            if (bpos == 0) { kfree(kbuf, nbytes); return -EINVAL; }
            break;
        }

        memset(&kfbd, 0, reclen);
        kfbd.d_fileno = d->d_ino;
        kfbd.d_off    = (int64_t)next_off;
        kfbd.d_reclen = reclen;
        kfbd.d_type   = d->d_type;
        kfbd.d_pad0   = 0;
        kfbd.d_namlen = namlen;
        kfbd.d_pad1   = 0;
        memcpy(kfbd.d_name, d->d_name, namlen);
        kfbd.d_name[namlen] = '\0';

        memcpy(kbuf + bpos, &kfbd, reclen);
        bpos += reclen;
        f->f_offset = (off_t)next_off;
    }

    if (bpos == 0) { kfree(kbuf, nbytes); return 0; }

    if (copyout(kbuf, buf, bpos) != 0) {
        kfree(kbuf, nbytes);
        return -EFAULT;
    }
    kfree(kbuf, nbytes);
    return (ssize_t)bpos;
}

/* ===================================================================
 * Additional FreeBSD syscall wrappers.  Syscall numbers verified against
 * ~/freebsd sys/kern/syscalls.master.  FreeBSD's modern pread/pwrite/
 * truncate carry no i386 off_t PAD argument (unlike NetBSD), so off_t is
 * passed as (lo,hi) directly after the preceding argument.
 * =================================================================== */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#endif

/* No-op success for operations substrate has no backing for (file flags,
 * memory locking, fadvise/fallocate hints, scheduler policy). */
int freebsd_sys_zero(void) { return 0; }

/* mkfifo(path, mode): mknod() with the S_IFIFO type bit. */
int freebsd_sys_mkfifo(const char *path, int mode) {
    return sys_mknod(path, (mode & 07777) | S_IFIFO, 0);
}

/* pread(fd, buf, nbyte, off_t offset): read at an absolute offset without
 * moving the descriptor's file pointer. */
int64_t freebsd_sys_pread(int fd, void *buf, size_t nbyte,
                          uint32_t off_lo, uint32_t off_hi) {
    int64_t saved = sys_lseek(fd, 0, 0, SEEK_CUR);
    if (saved < 0) return saved;
    int64_t pos = sys_lseek(fd, off_lo, off_hi, SEEK_SET);
    if (pos < 0) return pos;
    ssize_t n = sys_read(fd, (char *)buf, nbyte);
    sys_lseek(fd, (uint32_t)saved, (uint32_t)((uint64_t)saved >> 32), SEEK_SET);
    return n;
}

int64_t freebsd_sys_pwrite(int fd, const void *buf, size_t nbyte,
                           uint32_t off_lo, uint32_t off_hi) {
    int64_t saved = sys_lseek(fd, 0, 0, SEEK_CUR);
    if (saved < 0) return saved;
    int64_t pos = sys_lseek(fd, off_lo, off_hi, SEEK_SET);
    if (pos < 0) return pos;
    ssize_t n = sys_write(fd, (const char *)buf, nbyte);
    sys_lseek(fd, (uint32_t)saved, (uint32_t)((uint64_t)saved >> 32), SEEK_SET);
    return n;
}

/* fpathconf(fd, name): report fixed limits for the common names. */
int freebsd_sys_fpathconf(int fd, int name) {
    (void)fd;
    switch (name) {
    case 1:  return 32767;  /* _PC_LINK_MAX */
    case 2:  return 255;    /* _PC_MAX_CANON */
    case 3:  return 255;    /* _PC_MAX_INPUT */
    case 4:  return 255;    /* _PC_NAME_MAX */
    case 5:  return 1024;   /* _PC_PATH_MAX */
    case 6:  return 512;    /* _PC_PIPE_BUF */
    case 7:  return 1;      /* _PC_CHOWN_RESTRICTED */
    case 8:  return 0;      /* _PC_NO_TRUNC */
    case 9:  return 0;      /* _PC_VDISABLE */
    default: return -EINVAL;
    }
}

/* sched_get_priority_max/min(policy): substrate uses a single priority
 * band; report a small fixed range. */
int freebsd_sys_sched_prio_max(int policy) { (void)policy; return 31; }
int freebsd_sys_sched_prio_min(int policy) { (void)policy; return 0; }

/*
 * FreeBSD kenv(2): kernel environment accessor.  Substrate has no kernel
 * environment (there is no boot loader stuffing tunables into kernel env),
 * so a GET reports "not found", DUMP yields nothing, and SET/UNSET are
 * accepted no-ops.  rc(8) reads loader vars through kenv; returning ENOENT
 * (rather than ENOSYS) lets the scripts take their "variable unset" path.
 */
#define FBSD_KENV_GET    0
#define FBSD_KENV_SET    1
#define FBSD_KENV_UNSET  2
int freebsd_sys_kenv(int what, const char *name, char *value, int len) {
    (void)name; (void)value; (void)len;
    if (what == FBSD_KENV_GET) {
        return -ENOENT;     /* no such variable */
    }
    return 0;               /* SET/UNSET no-op; DUMP variants -> 0 bytes */
}

/*
 * FreeBSD nmount(2): the modern mount(8) entry point.  Arguments arrive as
 * an iovec array of NUL-terminated name/value pairs ("fstype", "fspath",
 * "from", plus fs-specific options).  Pull out the three that map onto
 * substrate's kern_mount(source, target, fstype, flags, data) and forward;
 * fs-specific options are ignored.
 */
int freebsd_sys_nmount(const struct freebsd_iovec *iov, unsigned int niov,
                       int flags) {
    if (niov == 0 || (niov & 1) || niov > 64) {
        return -EINVAL;
    }
    struct freebsd_iovec kiov[64];
    if (copyin(iov, kiov, niov * sizeof(kiov[0])) != 0) {
        return -EFAULT;
    }

    char fstype[32] = "";
    char fspath[256] = "";
    char from[256]   = "";

    for (unsigned int i = 0; i + 1 < niov; i += 2) {
        char nm[32];
        size_t nlen = kiov[i].iov_len;
        if (nlen == 0 || nlen > sizeof(nm)) {
            continue;
        }
        if (copyin(kiov[i].iov_base, nm, nlen) != 0) {
            continue;
        }
        nm[nlen - 1 < sizeof(nm) ? nlen - 1 : sizeof(nm) - 1] = '\0';

        char  *dst = NULL;
        size_t cap = 0;
        if (strcmp(nm, "fstype") == 0)      { dst = fstype; cap = sizeof(fstype); }
        else if (strcmp(nm, "fspath") == 0) { dst = fspath; cap = sizeof(fspath); }
        else if (strcmp(nm, "from") == 0)   { dst = from;   cap = sizeof(from); }
        if (!dst) {
            continue;
        }
        size_t vlen = kiov[i + 1].iov_len;
        if (vlen == 0 || vlen > cap) {
            continue;
        }
        if (copyin(kiov[i + 1].iov_base, dst, vlen) == 0) {
            dst[vlen - 1 < cap ? vlen - 1 : cap - 1] = '\0';
        }
    }

    if (fstype[0] == '\0' || fspath[0] == '\0') {
        return -EINVAL;
    }
    return kern_mount(from[0] ? from : NULL, fspath, fstype,
                      (unsigned long)flags, NULL);
}
