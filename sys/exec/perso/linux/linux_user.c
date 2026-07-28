#include <string.h>

#include <exec/perso/linux/linux_user.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/kern_syscalls.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>


/* Map a substrate filesystem (by its native f_fstypename) to the Linux
 * superblock magic that a Linux program expects in statfs.f_type.  Substrate's
 * native f_type is an internal value a Linux binary would not recognise, so
 * translate to the well-known Linux magics; fall back to the native value (then
 * ext2's magic) when the type is unknown.  Grafted from PR #1301 and shared by
 * both the statfs and statfs64 fills. */
static uint32_t linux_fs_magic(const char *fstypename, uint32_t native_type) {
    if (fstypename) {
        if (strncmp(fstypename, "ext2", 4) == 0)   return 0xEF53;     /* EXT2_SUPER_MAGIC */
        if (strncmp(fstypename, "fat", 3) == 0)     return 0x4d44;     /* MSDOS_SUPER_MAGIC */
        if (strncmp(fstypename, "procfs", 6) == 0)  return 0x9fa0;     /* PROC_SUPER_MAGIC  */
        if (strncmp(fstypename, "sysfs", 5) == 0)   return 0x62656572; /* SYSFS_MAGIC       */
        if (strncmp(fstypename, "devfs", 5) == 0)   return 0x1373;     /* DEVFS_SUPER_MAGIC */
        if (strncmp(fstypename, "tmpfs", 5) == 0)   return 0x01021994; /* TMPFS_MAGIC       */
    }
    return native_type ? native_type : 0xEF53;
}

static void linux_fill_statfs(struct linux_statfs *kbuf, const struct statfs *native) {
    memset(kbuf, 0, sizeof(*kbuf));
    kbuf->f_type = linux_fs_magic(native->f_fstypename, native->f_type);
    kbuf->f_bsize = native->f_bsize;
    kbuf->f_blocks = (uint32_t)(native->f_blocks & 0xFFFFFFFF);
    kbuf->f_bfree = (uint32_t)(native->f_bfree & 0xFFFFFFFF);
    kbuf->f_bavail = (uint32_t)(native->f_bavail & 0xFFFFFFFF);
    kbuf->f_files = (uint32_t)(native->f_files & 0xFFFFFFFF);
    kbuf->f_ffree = (uint32_t)(native->f_ffree & 0xFFFFFFFF);
    kbuf->f_fsid[0] = (uint32_t)(native->f_fsid & 0xFFFFFFFF);
    kbuf->f_fsid[1] = (uint32_t)((native->f_fsid >> 32) & 0xFFFFFFFF);
    kbuf->f_namelen = 255; /* Common max length */
    kbuf->f_frsize = native->f_bsize;
    kbuf->f_flags = native->f_flags;
}

static void linux_fill_statfs64(struct linux_statfs64 *kbuf, const struct statfs *native) {
    memset(kbuf, 0, sizeof(*kbuf));
    kbuf->f_type = linux_fs_magic(native->f_fstypename, native->f_type);
    kbuf->f_bsize = native->f_bsize;
    kbuf->f_blocks = native->f_blocks;
    kbuf->f_bfree = native->f_bfree;
    kbuf->f_bavail = native->f_bavail;
    kbuf->f_files = native->f_files;
    kbuf->f_ffree = native->f_ffree;
    kbuf->f_fsid[0] = (uint32_t)(native->f_fsid & 0xFFFFFFFF);
    kbuf->f_fsid[1] = (uint32_t)((native->f_fsid >> 32) & 0xFFFFFFFF);
    kbuf->f_namelen = 255; /* Common max length */
    kbuf->f_frsize = native->f_bsize;
    kbuf->f_flags = native->f_flags;
}

static void linux_fill_stat64(struct linux_stat64 *kbuf, const struct stat *native) {
    memset(kbuf, 0, sizeof(*kbuf));
    kbuf->st_dev = native->st_dev;
    kbuf->__st_ino = native->st_ino;
    kbuf->st_mode = native->st_mode;
    kbuf->st_nlink = native->st_nlink;
    kbuf->st_uid = native->st_uid;
    kbuf->st_gid = native->st_gid;
    kbuf->st_rdev = native->st_rdev;
    kbuf->st_size = native->st_size;
    kbuf->st_blksize = native->st_blksize;
    kbuf->st_blocks = native->st_blocks;
    kbuf->st_atime = (uint32_t)(native->st_atime & 0xFFFFFFFF);
    kbuf->st_atime_nsec = native->st_atime_nsec;
    kbuf->st_mtime = (uint32_t)(native->st_mtime & 0xFFFFFFFF);
    kbuf->st_mtime_nsec = native->st_mtime_nsec;
    kbuf->st_ctime = (uint32_t)(native->st_ctime & 0xFFFFFFFF);
    kbuf->st_ctime_nsec = native->st_ctime_nsec;
    kbuf->st_ino = native->st_ino;
}

static void linux_fill_statx_timestamp(struct linux_statx_timestamp *dst, int64_t sec, uint32_t nsec) {
    dst->tv_sec = sec;
    dst->tv_nsec = nsec;
    dst->__reserved = 0;
}

static void linux_fill_statx(struct linux_statx *kbuf, const struct stat *native, unsigned int mask) {
    memset(kbuf, 0, sizeof(*kbuf));
    kbuf->stx_mask = LINUX_STATX_BASIC_STATS;
    if (mask & LINUX_STATX_BTIME) {
        kbuf->stx_mask |= LINUX_STATX_BTIME;
    }
    kbuf->stx_blksize = native->st_blksize;
    kbuf->stx_nlink = native->st_nlink;
    kbuf->stx_uid = native->st_uid;
    kbuf->stx_gid = native->st_gid;
    kbuf->stx_mode = native->st_mode;
    kbuf->stx_ino = native->st_ino;
    kbuf->stx_size = native->st_size;
    kbuf->stx_blocks = native->st_blocks;
    linux_fill_statx_timestamp(&kbuf->stx_atime, native->st_atime, native->st_atime_nsec);
    linux_fill_statx_timestamp(&kbuf->stx_btime, native->st_ctime, native->st_ctime_nsec);
    linux_fill_statx_timestamp(&kbuf->stx_ctime, native->st_ctime, native->st_ctime_nsec);
    linux_fill_statx_timestamp(&kbuf->stx_mtime, native->st_mtime, native->st_mtime_nsec);
    kbuf->stx_rdev_major = native->st_rdev >> 8;
    kbuf->stx_rdev_minor = native->st_rdev & 0xff;
    kbuf->stx_dev_major = native->st_dev >> 8;
    kbuf->stx_dev_minor = native->st_dev & 0xff;
}

/* Linux stat translation: native -> linux_stat */
int linux_sys_stat(const char *path, struct linux_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    
    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret < 0) return ret;
    
    struct linux_stat kbuf;
    memset(&kbuf, 0, sizeof(kbuf));
    kbuf.st_dev = native.st_dev;
    kbuf.st_ino = native.st_ino;
    kbuf.st_mode = native.st_mode;
    kbuf.st_nlink = native.st_nlink;
    kbuf.st_uid = native.st_uid;
    kbuf.st_gid = native.st_gid;
    kbuf.st_rdev = native.st_rdev;
    kbuf.st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);
    kbuf.st_blksize = native.st_blksize;
    kbuf.st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    kbuf.st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    kbuf.st_atime_nsec = native.st_atime_nsec;
    kbuf.st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    kbuf.st_mtime_nsec = native.st_mtime_nsec;
    kbuf.st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    kbuf.st_ctime_nsec = native.st_ctime_nsec;
    
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

int linux_sys_lstat(const char *path, struct linux_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret < 0) return ret;
    
    struct linux_stat kbuf;
    memset(&kbuf, 0, sizeof(kbuf));
    kbuf.st_dev = native.st_dev;
    kbuf.st_ino = native.st_ino;
    kbuf.st_mode = native.st_mode;
    kbuf.st_nlink = native.st_nlink;
    kbuf.st_uid = native.st_uid;
    kbuf.st_gid = native.st_gid;
    kbuf.st_rdev = native.st_rdev;
    kbuf.st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);
    kbuf.st_blksize = native.st_blksize;
    kbuf.st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    kbuf.st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    kbuf.st_atime_nsec = native.st_atime_nsec;
    kbuf.st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    kbuf.st_mtime_nsec = native.st_mtime_nsec;
    kbuf.st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    kbuf.st_ctime_nsec = native.st_ctime_nsec;
    
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

int linux_sys_fstat(int fd, struct linux_stat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret < 0) return ret;
    
    struct linux_stat kbuf;
    memset(&kbuf, 0, sizeof(kbuf));
    kbuf.st_dev = native.st_dev;
    kbuf.st_ino = native.st_ino;
    kbuf.st_mode = native.st_mode;
    kbuf.st_nlink = native.st_nlink;
    kbuf.st_uid = native.st_uid;
    kbuf.st_gid = native.st_gid;
    kbuf.st_rdev = native.st_rdev;
    kbuf.st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);
    kbuf.st_blksize = native.st_blksize;
    kbuf.st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    kbuf.st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    kbuf.st_atime_nsec = native.st_atime_nsec;
    kbuf.st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    kbuf.st_mtime_nsec = native.st_mtime_nsec;
    kbuf.st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    kbuf.st_ctime_nsec = native.st_ctime_nsec;
    
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

/* Linux stat64 translation: native -> linux_stat64 */
int linux_sys_stat64(const char *path, struct linux_stat64 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret < 0) return ret;
    
    struct linux_stat64 kbuf;
    linux_fill_stat64(&kbuf, &native);
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

int linux_sys_lstat64(const char *path, struct linux_stat64 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret < 0) return ret;
    
    struct linux_stat64 kbuf;
    linux_fill_stat64(&kbuf, &native);
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

int linux_sys_fstat64(int fd, struct linux_stat64 *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret < 0) return ret;
    
    struct linux_stat64 kbuf;
    linux_fill_stat64(&kbuf, &native);
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

int linux_sys_fstatat64(int dirfd, const char *path, struct linux_stat64 *buf, int flags) {
    char kpath[256];
    struct stat native;
    struct linux_stat64 kbuf;
    int ret;

    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;

    /* AT_EMPTY_PATH: stat dirfd itself; otherwise pass only the
     * nofollow bit through (kern_fstatat's 4th arg is a nofollow flag,
     * not the raw at-flags). */
    if ((flags & LINUX_AT_EMPTY_PATH) && kpath[0] == '\0') {
        ret = kern_fstat(dirfd, &native);
    } else {
        ret = kern_fstatat(dirfd, kpath, &native, flags & LINUX_AT_SYMLINK_NOFOLLOW);
    }
    if (ret < 0) return ret;

    linux_fill_stat64(&kbuf, &native);
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

int linux_sys_statx(int dirfd, const char *path, int flags, unsigned int mask, struct linux_statx *buf) {
    char kpath[256];
    struct stat native;
    struct linux_statx kbuf;
    int ret;

    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;

    /*
     * AT_EMPTY_PATH with an empty path operates on dirfd itself (fstat
     * semantics).  glibc's ld.so stats every library it opens this way:
     *   statx(fd, "", AT_EMPTY_PATH|AT_NO_AUTOMOUNT, STATX_BASIC_STATS, &buf)
     * Path-resolving the empty string instead returns ENOTDIR, which made
     * ld.so abort with "cannot stat shared object" for every .so.
     */
    if ((flags & LINUX_AT_EMPTY_PATH) && kpath[0] == '\0') {
        ret = kern_fstat(dirfd, &native);
    } else {
        ret = kern_fstatat(dirfd, kpath, &native, flags & LINUX_AT_SYMLINK_NOFOLLOW);
    }
    if (ret < 0) return ret;

    linux_fill_statx(&kbuf, &native, mask);
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

int linux_sys_getcwd(char *buf, size_t size) {
    char *kbuf;
    size_t len;
    int ret;

    if (!buf) return -EFAULT;
    if (size == 0) return -EINVAL;
    if (size > 4096) size = 4096;

    kbuf = kmalloc(size);
    if (!kbuf) return -ENOMEM;

    ret = kern_getcwd(kbuf, size);
    if (ret < 0) {
        kfree(kbuf, size);
        return ret;
    }

    len = strlen(kbuf) + 1;
    if (len > size) {
        kfree(kbuf, size);
        return -ERANGE;
    }
    if (copyout(kbuf, buf, len) != 0) {
        kfree(kbuf, size);
        return -EFAULT;
    }

    kfree(kbuf, size);
    return (int)len;
}


int linux_sys_statfs(const char *path, struct linux_statfs *buf) {
    char *kpath = kmalloc(4096);
    if (!kpath) return -ENOMEM;
    int err = copyinstr(path, kpath, 4096, NULL);
    if (err != 0) {
        kfree(kpath, 4096);
        return -err;
    }

    struct statfs native;
    int ret = kern_statfs(kpath, &native);
    kfree(kpath, 4096);
    if (ret != 0) {
        if (ret > 0) return -ret;
        return ret;
    }

    struct linux_statfs kbuf;
    linux_fill_statfs(&kbuf, &native);
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

int linux_sys_fstatfs(int fd, struct linux_statfs *buf) {
    struct statfs native;
    int ret = kern_fstatfs(fd, &native);
    if (ret != 0) {
        if (ret > 0) return -ret;
        return ret;
    }

    struct linux_statfs kbuf;
    linux_fill_statfs(&kbuf, &native);
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

int linux_sys_statfs64(const char *path, size_t sz, struct linux_statfs64 *buf) {
    if (sz != sizeof(struct linux_statfs64)) return -EINVAL;

    char *kpath = kmalloc(4096);
    if (!kpath) return -ENOMEM;
    int err = copyinstr(path, kpath, 4096, NULL);
    if (err != 0) {
        kfree(kpath, 4096);
        return -err;
    }

    struct statfs native;
    int ret = kern_statfs(kpath, &native);
    kfree(kpath, 4096);
    if (ret != 0) {
        if (ret > 0) return -ret;
        return ret;
    }

    struct linux_statfs64 kbuf;
    linux_fill_statfs64(&kbuf, &native);
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

int linux_sys_fstatfs64(int fd, size_t sz, struct linux_statfs64 *buf) {
    if (sz != sizeof(struct linux_statfs64)) return -EINVAL;
    struct statfs native;
    int ret = kern_fstatfs(fd, &native);
    if (ret != 0) {
        if (ret > 0) return -ret;
        return ret;
    }

    struct linux_statfs64 kbuf;
    linux_fill_statfs64(&kbuf, &native);
    if (copyout(&kbuf, buf, sizeof(kbuf)) != 0) return -EFAULT;
    return 0;
}

/*
 * Old Linux readdir(2) (i386 syscall 89) — the pre-getdents interface used by
 * a.out-era libc (Slackware 1.x).  This is a Linux-personality ABI: Substrate
 * itself has no old readdir.  Unlike getdents it reads exactly ONE entry
 * regardless of `count`, into a struct old_linux_dirent:
 *
 *     unsigned long  d_ino;
 *     unsigned long  d_offset;
 *     unsigned short d_namlen;
 *     char           d_name[1];   // d_namlen bytes + trailing NUL
 *
 * Returns 1 when an entry was read, 0 at end-of-directory, -errno on error.
 */
struct old_linux_dirent {
    unsigned long  d_ino;
    unsigned long  d_offset;
    unsigned short d_namlen;
    char           d_name[256];
};

int linux_old_readdir(unsigned int fd, void *dirp, unsigned int count) {
    (void)count;   /* old readdir ignores count and returns a single entry */

    if (fd >= MAX_FD) return -EBADF;
    file_t *f = current_process->fds[fd];
    if (!f) return -EBADF;
    if (!dirp) return -EFAULT;

    struct dirent *d = readdir_fs((fs_node_t *)f->f_data, f->f_offset);
    if (!d) {
        return 0;   /* end of directory */
    }

    int name_len = 0;
    while (d->d_name[name_len] && name_len < 255) name_len++;

    struct old_linux_dirent kent;
    kent.d_ino = d->d_ino;
    kent.d_offset = (unsigned long)f->f_offset;
    kent.d_namlen = (unsigned short)name_len;
    for (int i = 0; i < name_len; i++) kent.d_name[i] = d->d_name[i];
    kent.d_name[name_len] = '\0';

    /* Only the used prefix is copied out: header (10 bytes on i386) + name. */
    size_t sz = sizeof(unsigned long) * 2 + sizeof(unsigned short) +
                (size_t)name_len + 1;
    if (copyout(&kent, dirp, sz) != 0) {
        return -EFAULT;
    }

    uint64_t cur_off = (uint64_t)f->f_offset;
    uint64_t next_off = (d->d_off > cur_off) ? d->d_off : cur_off + 1;
    f->f_offset = next_off;
    return 1;
}
