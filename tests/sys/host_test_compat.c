#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

#include <sys/types.h>

struct stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    uint32_t st_size;
    uint32_t st_blksize;
    uint32_t st_blocks;
    uint32_t st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime;
    uint32_t st_ctime_nsec;
};

/* Mock out sys/syscall_impl.h */
#define _SYS_SYSCALL_IMPL_H
/* Mock out sys/stat.h */
#define _SYS_STAT_H

#include <exec/perso/freebsd/freebsd_user.h>

/* Global mock variables for testing sys_freebsd_stat */
int mock_copyinstr_ret = 0;
int mock_kern_stat_ret = 0;
int mock_copyout_ret = 0;
struct stat mock_stat_buf;
struct freebsd_stat mock_freebsd_stat_buf;

/* Mock external functions that compat.c calls */
int64_t sys_lseek(int fd, uint32_t off_lo, uint32_t off_hi, int whence) { return 0; }
int sys_time(void *t) { return 0; }
void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, int offset) { return NULL; }
int sys_execve(const char *path, char *const argv[], char *const envp[]) { return 0; }

int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done) {
    if (mock_copyinstr_ret == 0 && kaddr && uaddr) {
        // Just null terminate to pretend we copied a string
        if (len > 0) ((char*)kaddr)[0] = '\0';
    }
    return mock_copyinstr_ret;
}

int copyout(const void *kaddr, void *uaddr, size_t len) {
    if (mock_copyout_ret == 0 && uaddr && kaddr) {
        // Save the copied data so we can verify it
        if (len == sizeof(struct freebsd_stat)) {
            struct freebsd_stat *dst = (struct freebsd_stat *)uaddr;
            struct freebsd_stat *src = (struct freebsd_stat *)kaddr;
            *dst = *src;
            mock_freebsd_stat_buf = *src;
        }
    }
    return mock_copyout_ret;
}

int kern_stat(const char *path, struct stat *st) {
    if (mock_kern_stat_ret == 0 && st) {
        *st = mock_stat_buf;
    }
    return mock_kern_stat_ret;
}

int kern_lstat(const char *path, struct stat *st) { return 0; }
int kern_fstat(int fd, struct stat *st) { return 0; }

/* Now we can include compat.c directly! */
#include "../../sys/exec/perso/compat.c"

void test_sys_freebsd_stat() {
    printf("Testing sys_freebsd_stat()...\n");

    struct freebsd_stat user_buf;
    int ret;

    // Test 1: Happy path
    mock_copyinstr_ret = 0;
    mock_kern_stat_ret = 0;
    mock_copyout_ret = 0;

    mock_stat_buf.st_dev = 1;
    mock_stat_buf.st_ino = 2;
    mock_stat_buf.st_mode = 0755;
    mock_stat_buf.st_nlink = 3;
    mock_stat_buf.st_uid = 1000;
    mock_stat_buf.st_gid = 1000;
    mock_stat_buf.st_rdev = 4;
    mock_stat_buf.st_size = 1024;
    mock_stat_buf.st_blksize = 512;
    mock_stat_buf.st_blocks = 2;
    mock_stat_buf.st_atime = 10000;
    mock_stat_buf.st_atime_nsec = 10;
    mock_stat_buf.st_mtime = 20000;
    mock_stat_buf.st_mtime_nsec = 20;
    mock_stat_buf.st_ctime = 30000;
    mock_stat_buf.st_ctime_nsec = 30;

    ret = sys_freebsd_stat("/dummy", &user_buf);

    assert(ret == 0);
    assert(mock_freebsd_stat_buf.st_dev == mock_stat_buf.st_dev);
    assert(mock_freebsd_stat_buf.st_ino == mock_stat_buf.st_ino);
    assert(mock_freebsd_stat_buf.st_mode == mock_stat_buf.st_mode);
    assert(mock_freebsd_stat_buf.st_nlink == mock_stat_buf.st_nlink);
    assert(mock_freebsd_stat_buf.st_uid == mock_stat_buf.st_uid);
    assert(mock_freebsd_stat_buf.st_gid == mock_stat_buf.st_gid);
    assert(mock_freebsd_stat_buf.st_rdev == mock_stat_buf.st_rdev);
    assert(mock_freebsd_stat_buf.st_size == mock_stat_buf.st_size);
    assert(mock_freebsd_stat_buf.st_blksize == mock_stat_buf.st_blksize);
    assert(mock_freebsd_stat_buf.st_blocks == mock_stat_buf.st_blocks);
    assert(mock_freebsd_stat_buf.st_atim.tv_sec == mock_stat_buf.st_atime);
    assert(mock_freebsd_stat_buf.st_atim.tv_nsec == mock_stat_buf.st_atime_nsec);
    assert(mock_freebsd_stat_buf.st_mtim.tv_sec == mock_stat_buf.st_mtime);
    assert(mock_freebsd_stat_buf.st_mtim.tv_nsec == mock_stat_buf.st_mtime_nsec);
    assert(mock_freebsd_stat_buf.st_ctim.tv_sec == mock_stat_buf.st_ctime);
    assert(mock_freebsd_stat_buf.st_ctim.tv_nsec == mock_stat_buf.st_ctime_nsec);

    // Test 2: copyinstr fails
    mock_copyinstr_ret = -1;
    ret = sys_freebsd_stat("/dummy", &user_buf);
    assert(ret == -14); // Expected error code from sys_freebsd_stat when copyinstr fails

    // Test 3: kern_stat fails
    mock_copyinstr_ret = 0;
    mock_kern_stat_ret = -ENOENT;
    ret = sys_freebsd_stat("/dummy", &user_buf);
    assert(ret == -ENOENT);

    // Test 4: copyout fails
    mock_copyinstr_ret = 0;
    mock_kern_stat_ret = 0;
    mock_copyout_ret = -1;
    ret = sys_freebsd_stat("/dummy", &user_buf);
    assert(ret == -14); // Expected error code from sys_freebsd_stat when copyout fails

    printf("All sys_freebsd_stat tests passed!\n");
}

int main() {
    printf("Testing sys_nice()...\n");

    // Assert sys_nice returns -ENOSYS
    assert(sys_nice(0) == -ENOSYS);
    assert(sys_nice(1) == -ENOSYS);
    assert(sys_nice(-1) == -ENOSYS);
    assert(sys_nice(100) == -ENOSYS);

    printf("All sys_nice tests passed!\n");

    test_sys_freebsd_stat();

    return 0;
}
