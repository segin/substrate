/*
 * linux_user.c - Linux personality ABI translation layer
 */

#include <exec/perso/linux/linux_user.h>
#include <sys/syscall_impl.h>

/* Linux stat translation: native -> linux_stat */
int linux_sys_stat(const char *path, struct linux_stat *buf) {
    struct native_stat native;
    int ret = sys_stat(path, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->st_ino = native.st_ino;
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);  /* Truncate! */
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->__unused4 = 0;
    buf->__unused5 = 0;
    return 0;
}

int linux_sys_lstat(const char *path, struct linux_stat *buf) {
    struct native_stat native;
    int ret = sys_lstat(path, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->st_ino = native.st_ino;
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->__unused4 = 0;
    buf->__unused5 = 0;
    return 0;
}

int linux_sys_fstat(int fd, struct linux_stat *buf) {
    struct native_stat native;
    int ret = sys_fstat(fd, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->st_ino = native.st_ino;
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->__unused4 = 0;
    buf->__unused5 = 0;
    return 0;
}

/* Linux stat64 translation: native -> linux_stat64 */
int linux_sys_stat64(const char *path, struct linux_stat64 *buf) {
    struct native_stat native;
    int ret = sys_stat(path, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->__pad1 = 0;
    buf->__st_ino = native.st_ino;  /* 32-bit compat ino */
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->__pad2 = 0;
    buf->st_size = native.st_size;  /* Full 64-bit */
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = native.st_blocks;
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->st_ino = native.st_ino;  /* Full 64-bit ino */
    return 0;
}

int linux_sys_lstat64(const char *path, struct linux_stat64 *buf) {
    struct native_stat native;
    int ret = sys_lstat(path, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->__pad1 = 0;
    buf->__st_ino = native.st_ino;
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->__pad2 = 0;
    buf->st_size = native.st_size;
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = native.st_blocks;
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->st_ino = native.st_ino;
    return 0;
}

int linux_sys_fstat64(int fd, struct linux_stat64 *buf) {
    struct native_stat native;
    int ret = sys_fstat(fd, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->__pad1 = 0;
    buf->__st_ino = native.st_ino;
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->__pad2 = 0;
    buf->st_size = native.st_size;
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = native.st_blocks;
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->st_ino = native.st_ino;
    return 0;
}
