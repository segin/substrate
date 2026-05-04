#include "freebsd_user.h"
#include <sys/kern_syscalls.h>
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

/* FreeBSD 12+ fstatat (syscall 552): uses the wide struct freebsd_stat
 * (64-bit ino/time).  ls(1) drives directory traversal through fts(3),
 * which calls fstatat() for every entry — the COMPAT11 syscall 493
 * remains for legacy binaries but modern userland uses 552. */
int sys_freebsd_fstatat(int dirfd, const char *path, struct freebsd_stat *buf, int flags) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    struct stat native;
    int ret = kern_fstatat(dirfd, kpath, &native, freebsd_atflags(flags));
    if (ret == 0) {
        struct freebsd_stat kfbsd;
        translate_stat_to_freebsd(&native, &kfbsd);
        if (copyout(&kfbsd, buf, sizeof(struct freebsd_stat)) != 0) return -14;
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

/* FreeBSD AT_SYMLINK_NOFOLLOW=0x200 → kernel AT_SYMLINK_NOFOLLOW=0x100 */
static int freebsd_atflags(int f) {
    return (f & 0x200) ? AT_SYMLINK_NOFOLLOW : 0;
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
