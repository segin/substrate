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

/* Mock external functions that compat.c calls */
static int mock_sys_lseek_fd;
static uint32_t mock_sys_lseek_off_lo;
static uint32_t mock_sys_lseek_off_hi;
static int mock_sys_lseek_whence;
static int64_t mock_sys_lseek_return;

int64_t sys_lseek(int fd, uint32_t off_lo, uint32_t off_hi, int whence) {
    mock_sys_lseek_fd = fd;
    mock_sys_lseek_off_lo = off_lo;
    mock_sys_lseek_off_hi = off_hi;
    mock_sys_lseek_whence = whence;
    return mock_sys_lseek_return;
}

int64_t g_mock_sys_time_result = 0;
int64_t g_mock_sys_time_out_val = 0;
int sys_time(void *t) {
    if (t && g_mock_sys_time_result >= 0) {
        *(int64_t *)t = g_mock_sys_time_out_val;
    }
    return g_mock_sys_time_result;
}

void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, int offset) {
    (void)addr; (void)len; (void)prot; (void)flags; (void)fd; (void)offset;
    return NULL;
}
int sys_execve(const char *path, char *const argv[], char *const envp[]) {
    (void)path; (void)argv; (void)envp;
    return 0;
}
int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done) {
    (void)uaddr; (void)kaddr; (void)len; (void)done;
    return 0;
}
int copyout(const void *kaddr, void *uaddr, size_t len) {
    (void)kaddr; (void)uaddr; (void)len;
    return 0;
}

int kern_stat(const char *path, struct stat *st) {
    (void)path; (void)st;
    return 0;
}
int kern_lstat(const char *path, struct stat *st) {
    (void)path; (void)st;
    return 0;
}
int kern_fstat(int fd, struct stat *st) {
    (void)fd; (void)st;
    return 0;
}

/* Now we can include compat.c directly! */
#include "../../sys/exec/perso/compat.c"

static void test_compat_lseek32(void) {
    printf("Testing compat_lseek32()...\n");

    mock_sys_lseek_return = 1024;
    assert(compat_lseek32(5, 100, 0) == 1024);
    assert(mock_sys_lseek_fd == 5);
    assert(mock_sys_lseek_off_lo == 100);
    assert(mock_sys_lseek_off_hi == 0);
    assert(mock_sys_lseek_whence == 0);

    mock_sys_lseek_return = 512;
    assert(compat_lseek32(6, -100, 1) == 512);
    assert(mock_sys_lseek_fd == 6);
    assert(mock_sys_lseek_off_lo == (uint32_t)-100);
    assert(mock_sys_lseek_off_hi == 0xFFFFFFFFu);
    assert(mock_sys_lseek_whence == 1);

    mock_sys_lseek_return = 0x80000000LL;
    assert(compat_lseek32(5, 100, 0) == -1);

    mock_sys_lseek_return = 0x7FFFFFFFLL;
    assert(compat_lseek32(5, 100, 0) == 0x7FFFFFFF);

    mock_sys_lseek_return = -0x80000001LL;
    assert(compat_lseek32(5, 100, 0) == -1);

    mock_sys_lseek_return = -0x80000000LL;
    assert(compat_lseek32(5, 100, 0) == (int32_t)-0x80000000LL);

    printf("All compat_lseek32 tests passed!\n");
}

int main() {
    printf("Testing sys_nice()...\n");

    // Assert sys_nice returns -ENOSYS
    assert(sys_nice(0) == -ENOSYS);
    assert(sys_nice(1) == -ENOSYS);
    assert(sys_nice(-1) == -ENOSYS);
    assert(sys_nice(100) == -ENOSYS);

    printf("All sys_nice tests passed!\n");

    printf("Testing compat_time32()...\n");
    int32_t tloc_val = 0;
    int32_t ret = 0;

    // Test Case 1: Normal case - Value within 32-bit range
    g_mock_sys_time_result = 0;
    g_mock_sys_time_out_val = 0x12345678;
    tloc_val = 0;
    ret = compat_time32(&tloc_val);
    assert(ret == 0x12345678);
    assert(tloc_val == 0x12345678);

    // Test Case 2: Truncation case - Value larger than 32-bit range
    g_mock_sys_time_result = 0;
    g_mock_sys_time_out_val = 0x1122334455667788LL;
    tloc_val = 0;
    ret = compat_time32(&tloc_val);
    assert(ret == 0x55667788);
    assert(tloc_val == 0x55667788);

    // Test Case 3: Negative return from sys_time
    g_mock_sys_time_result = -EINVAL;
    g_mock_sys_time_out_val = 0x12345678; // Should not be written
    tloc_val = (int32_t)0x99999999;
    ret = compat_time32(&tloc_val);
    assert(ret == -EINVAL);
    assert(tloc_val == (int32_t)0x99999999); // Should remain unchanged

    // Test Case 4: tloc == NULL
    g_mock_sys_time_result = 0;
    g_mock_sys_time_out_val = 0x87654321;
    ret = compat_time32(NULL);
    assert(ret == (int32_t)0x87654321);

    // Test Case 5: Exact Y2038 boundary (0x80000000)
    g_mock_sys_time_result = 0;
    g_mock_sys_time_out_val = 0x0000000080000000LL;
    tloc_val = 0;
    ret = compat_time32(&tloc_val);
    assert(ret == (int32_t)0x80000000);
    assert(tloc_val == (int32_t)0x80000000);

    // Test Case 6: Exact 32-bit boundary overflow (0x100000000LL)
    g_mock_sys_time_result = 0;
    g_mock_sys_time_out_val = 0x0000000100000000LL;
    tloc_val = -1;
    ret = compat_time32(&tloc_val);
    assert(ret == 0);
    assert(tloc_val == 0);

    // Test Case 7: High bits set, low bits zero (0xFFFFFFFF00000000LL)
    g_mock_sys_time_result = 0;
    g_mock_sys_time_out_val = 0xFFFFFFFF00000000LL;
    tloc_val = -1;
    ret = compat_time32(&tloc_val);
    assert(ret == 0);
    assert(tloc_val == 0);

    // Test Case 8: All bits set (0xFFFFFFFFFFFFFFFFLL)
    g_mock_sys_time_result = 0;
    g_mock_sys_time_out_val = 0xFFFFFFFFFFFFFFFFLL;
    tloc_val = 0;
    ret = compat_time32(&tloc_val);
    assert(ret == -1);
    assert(tloc_val == -1);

    printf("All compat_time32 tests passed!\n");
    test_compat_lseek32();

    return 0;
}
