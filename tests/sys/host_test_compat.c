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

int mock_copyinstr_ret = 0;
int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done) {
    if (mock_copyinstr_ret == 0) {
        strncpy((char *)kaddr, (const char *)uaddr, len);
        if (done) *done = strlen((const char *)uaddr) + 1;
    }
    return mock_copyinstr_ret;
}

int mock_copyout_ret = 0;
int copyout(const void *kaddr, void *uaddr, size_t len) {
    if (mock_copyout_ret == 0) {
        memcpy(uaddr, kaddr, len);
    }
    return mock_copyout_ret;
}

int mock_kern_stat_ret = 0;
struct stat mock_stat_data;
char mock_kpath[256];
int kern_stat(const char *path, struct stat *st) {
    if (mock_kern_stat_ret == 0) {
        strncpy(mock_kpath, path, sizeof(mock_kpath));
        memcpy(st, &mock_stat_data, sizeof(struct stat));
    }
    return mock_kern_stat_ret;
}

int kern_lstat(const char *path, struct stat *st) { return 0; }
int kern_fstat(int fd, struct stat *st) { return 0; }

/* Now we can include compat.c directly! */
#include "../../sys/exec/perso/compat.c"

void test_compat_stat_stub() {
    printf("Testing compat_stat_stub()...\n");

    struct stat out_buf;
    int ret;
    const char *test_path = "/usr/bin/test";

    // 1. Happy path: everything succeeds
    mock_copyinstr_ret = 0;
    mock_kern_stat_ret = 0;
    mock_copyout_ret = 0;

    memset(&mock_stat_data, 0, sizeof(mock_stat_data));
    mock_stat_data.st_ino = 12345;
    mock_stat_data.st_size = 4096;
    mock_stat_data.st_mode = 0755;

    memset(&out_buf, 0, sizeof(out_buf));
    memset(mock_kpath, 0, sizeof(mock_kpath));

    ret = compat_stat_stub(test_path, &out_buf);
    assert(ret == 0);
    assert(strcmp(mock_kpath, test_path) == 0);
    assert(out_buf.st_ino == 12345);
    assert(out_buf.st_size == 4096);
    assert(out_buf.st_mode == 0755);

    // 2. copyinstr fails
    mock_copyinstr_ret = -1; // Any non-zero triggers error
    ret = compat_stat_stub(test_path, &out_buf);
    assert(ret == -14); // compat_stat_stub returns -14 on copyinstr error

    // 3. kern_stat fails
    mock_copyinstr_ret = 0;
    mock_kern_stat_ret = -2; // ENOENT
    ret = compat_stat_stub(test_path, &out_buf);
    assert(ret == -2); // returns underlying error

    // 4. copyout fails
    mock_kern_stat_ret = 0;
    mock_copyout_ret = -1; // Any non-zero triggers error
    ret = compat_stat_stub(test_path, &out_buf);
    assert(ret == -14); // compat_stat_stub returns -14 on copyout error

    printf("All compat_stat_stub tests passed!\n");
}

int main() {
    printf("Testing sys_nice()...\n");

    // Assert sys_nice returns -ENOSYS
    assert(sys_nice(0) == -ENOSYS);
    assert(sys_nice(1) == -ENOSYS);
    assert(sys_nice(-1) == -ENOSYS);
    assert(sys_nice(100) == -ENOSYS);

    printf("All sys_nice tests passed!\n");

    test_compat_stat_stub();

    return 0;
}
