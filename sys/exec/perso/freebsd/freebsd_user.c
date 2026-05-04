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

int sys_freebsd_stat(const char *path, struct freebsd_stat *buf) {
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

int sys_freebsd_lstat(const char *path, struct freebsd_stat *buf) {
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

int sys_freebsd_fstat(int fd, struct freebsd_stat *buf) {
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
int sys_freebsd_fstatat(int dirfd, const char *path, struct freebsd13_stat *buf, int flags) {
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

int sys_freebsd11_stat(const char *path, struct freebsd11_stat *buf) {
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

int sys_freebsd11_lstat(const char *path, struct freebsd11_stat *buf) {
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

int sys_freebsd11_fstat(int fd, struct freebsd11_stat *buf) {
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

int sys_freebsd13_stat(const char *path, struct freebsd13_stat *buf) {
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

int sys_freebsd13_lstat(const char *path, struct freebsd13_stat *buf) {
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

int sys_freebsd13_fstat(int fd, struct freebsd13_stat *buf) {
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

int sys_freebsd_open(const char *path, int flags, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_openat(AT_FDCWD, kpath, freebsd_oflags(flags), mode);
}

int sys_freebsd_openat(int dirfd, const char *path, int flags, int mode) {
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

int sys_freebsd_faccessat(int dirfd, const char *path, int amode, int flag) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    (void)flag;  /* AT_EACCESS handled implicitly */
    if (dirfd == AT_FDCWD) return kern_access(kpath, amode);
    return -ENOSYS;
}

int sys_freebsd_fchmodat(int dirfd, const char *path, int mode, int flag) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_chmodat(dirfd, kpath, mode, freebsd_atflags(flag));
}

int sys_freebsd_fchownat(int dirfd, const char *path, int uid, int gid, int flag) {
    /* sys_fchownat does its own copyinstr and supports arbitrary dirfd.
     * Just translate the flag bits and forward. */
    return sys_fchownat(dirfd, path, uid, gid, freebsd_atflags(flag));
}

int sys_freebsd_linkat(int olddir, const char *oldpath, int newdir, const char *newpath, int flag) {
    char kold[256], knew[256];
    if (copyinstr(oldpath, kold, sizeof(kold), NULL) != 0) return -EFAULT;
    if (copyinstr(newpath, knew, sizeof(knew), NULL) != 0) return -EFAULT;
    (void)flag;  /* AT_SYMLINK_FOLLOW currently default */
    if (olddir == AT_FDCWD && newdir == AT_FDCWD) return kern_link(kold, knew);
    return -ENOSYS;
}

int sys_freebsd_mkdirat(int dirfd, const char *path, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_mkdirat(dirfd, kpath, mode);
}

int sys_freebsd_readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz) {
    /* sys_readlinkat does its own copyinstr/copyout. */
    return sys_readlinkat(dirfd, path, buf, bufsiz);
}

int sys_freebsd_renameat(int olddir, const char *oldpath, int newdir, const char *newpath) {
    char kold[256], knew[256];
    if (copyinstr(oldpath, kold, sizeof(kold), NULL) != 0) return -EFAULT;
    if (copyinstr(newpath, knew, sizeof(knew), NULL) != 0) return -EFAULT;
    if (olddir == AT_FDCWD && newdir == AT_FDCWD) return kern_rename(kold, knew);
    return -ENOSYS;
}

int sys_freebsd_symlinkat(const char *target, int newdir, const char *newpath) {
    char ktgt[256], knew[256];
    if (copyinstr(target,  ktgt, sizeof(ktgt), NULL) != 0) return -EFAULT;
    if (copyinstr(newpath, knew, sizeof(knew), NULL) != 0) return -EFAULT;
    if (newdir == AT_FDCWD) return kern_symlink(ktgt, knew);
    return -ENOSYS;
}

int sys_freebsd_unlinkat(int dirfd, const char *path, int flag) {
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

int sys_freebsd_chown(const char *path, int uid, int gid) {
    /* FreeBSD chown(2) follows symlinks.  sys_fchownat with flag=0
     * gives that semantic and already does its own copyinstr. */
    return sys_fchownat(AT_FDCWD, path, uid, gid, 0);
}

int sys_freebsd_lchmod(const char *path, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_chmodat(AT_FDCWD, kpath, mode, AT_SYMLINK_NOFOLLOW);
}

int sys_freebsd13_fstatat(int dirfd, const char *path, struct freebsd13_stat *buf, int flags) {
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
int sys_freebsd11_fstatat(int dirfd, const char *path, struct freebsd11_stat *buf, int flags) {
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

int sys_freebsd_ostat(const char *path, struct freebsd_ostat *buf) {
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

int sys_freebsd_olstat(const char *path, struct freebsd_ostat *buf) {
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

int sys_freebsd_ofstat(int fd, struct freebsd_ostat *buf) {
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
ssize_t sys_freebsd11_getdirentries(int fd, char *buf, unsigned int nbytes, int32_t *basep) {
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
        f->f_offset++;
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
ssize_t sys_freebsd_getdirentries(int fd, char *buf, size_t nbytes, int64_t *basep) {
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

        uint16_t namlen = (uint16_t)strnlen(d->d_name, 255);
        /* header=24 bytes + name + NUL, aligned to 8 bytes */
        uint16_t reclen = (uint16_t)(((size_t)24 + namlen + 1 + 7) & ~(size_t)7);

        if (bpos + reclen > nbytes) {
            if (bpos == 0) { kfree(kbuf, nbytes); return -EINVAL; }
            break;
        }

        memset(&kfbd, 0, reclen);
        kfbd.d_fileno = d->d_ino;
        kfbd.d_off    = (int64_t)(f->f_offset + 1);
        kfbd.d_reclen = reclen;
        kfbd.d_type   = d->d_type;
        kfbd.d_pad0   = 0;
        kfbd.d_namlen = namlen;
        kfbd.d_pad1   = 0;
        memcpy(kfbd.d_name, d->d_name, namlen);
        kfbd.d_name[namlen] = '\0';

        memcpy(kbuf + bpos, &kfbd, reclen);
        bpos += reclen;
        f->f_offset++;
    }

    if (bpos == 0) { kfree(kbuf, nbytes); return 0; }

    if (copyout(kbuf, buf, bpos) != 0) {
        kfree(kbuf, nbytes);
        return -EFAULT;
    }
    kfree(kbuf, nbytes);
    return (ssize_t)bpos;
}
