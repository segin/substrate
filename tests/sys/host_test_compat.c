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

#include <string.h>

/* Mock external functions that compat.c calls */
int64_t sys_lseek(int fd, uint32_t off_lo, uint32_t off_hi, int whence) { return 0; }
int sys_time(void *t) { return 0; }
void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, int offset) { return NULL; }
int sys_execve(const char *path, char *const argv[], char *const envp[]) { return 0; }
int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done) { return 0; }

int mock_copyout_ret = 0;
int copyout(const void *kaddr, void *uaddr, size_t len) {
    if (mock_copyout_ret != 0) return mock_copyout_ret;
    memcpy(uaddr, kaddr, len);
    return 0;
}

int kern_stat(const char *path, struct stat *st) { return 0; }
int kern_lstat(const char *path, struct stat *st) { return 0; }

int mock_kern_fstat_ret = 0;
int kern_fstat(int fd, struct stat *st) {
    if (mock_kern_fstat_ret != 0) return mock_kern_fstat_ret;
    /* Dummy values to verify copy behavior */
    memset(st, 0, sizeof(struct stat));
    st->st_ino = 12345;
    st->st_size = 67890;
    return 0;
}

/* Now we can include compat.c directly! */
#include "../../sys/exec/perso/compat.c"

void test_compat_fstat_stub_success() {
    printf("Testing compat_fstat_stub() success...\n");
    struct stat ubuf;
    memset(&ubuf, 0, sizeof(struct stat));

    mock_kern_fstat_ret = 0;
    mock_copyout_ret = 0;

    int ret = compat_fstat_stub(3, &ubuf);

    assert(ret == 0);
    assert(ubuf.st_ino == 12345);
    assert(ubuf.st_size == 67890);
}

void test_compat_fstat_stub_kern_fstat_fails() {
    printf("Testing compat_fstat_stub() with kern_fstat failure...\n");
    struct stat ubuf;
    memset(&ubuf, 0, sizeof(struct stat));

    mock_kern_fstat_ret = -ENOENT;
    mock_copyout_ret = 0;

    int ret = compat_fstat_stub(3, &ubuf);

    assert(ret == -ENOENT);
    // Buffer should not be modified
    assert(ubuf.st_ino == 0);
    assert(ubuf.st_size == 0);
}

void test_compat_fstat_stub_copyout_fails() {
    printf("Testing compat_fstat_stub() with copyout failure...\n");
    struct stat ubuf;
    memset(&ubuf, 0, sizeof(struct stat));

    mock_kern_fstat_ret = 0;
    mock_copyout_ret = -14; // EFAULT

    int ret = compat_fstat_stub(3, &ubuf);

    assert(ret == -14);
}

int main() {
    printf("Testing sys_nice()...\n");

    // Assert sys_nice returns -ENOSYS
    assert(sys_nice(0) == -ENOSYS);
    assert(sys_nice(1) == -ENOSYS);
    assert(sys_nice(-1) == -ENOSYS);
    assert(sys_nice(100) == -ENOSYS);

    printf("All sys_nice tests passed!\n");

    test_compat_fstat_stub_success();
    test_compat_fstat_stub_kern_fstat_fails();
    test_compat_fstat_stub_copyout_fails();

    printf("All tests passed!\n");
    return 0;
}
