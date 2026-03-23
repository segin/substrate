#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

#define _SYS_SYSCALL_IMPL_H
#define _SYS_STAT_H
#define _EXEC_PERSO_H
#define _SYS_KERN_SYSCALLS_H
#define _SYS_COPY_H

struct stat {
    uint32_t st_dev;
    uint64_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint32_t st_rdev;
    int64_t  st_size;
    uint32_t st_blksize;
    uint32_t st_pad1;
    int64_t  st_blocks;
    int64_t  st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_pad2;
    int64_t  st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_pad3;
    int64_t  st_ctime;
    uint32_t st_ctime_nsec;
    uint32_t st_pad4;
};

/* Provide the mock global for copyout */
struct stat mock_native_stat;
char copyout_buffer[4096];

/* Mock kern_stat/lstat/fstat */
int kern_stat(const char *path, struct stat *st) {
    (void)path;
    memcpy(st, &mock_native_stat, sizeof(struct stat));
    return 0;
}

int kern_lstat(const char *path, struct stat *st) {
    (void)path;
    memcpy(st, &mock_native_stat, sizeof(struct stat));
    return 0;
}

int kern_fstat(int fd, struct stat *st) {
    (void)fd;
    memcpy(st, &mock_native_stat, sizeof(struct stat));
    return 0;
}

/* Mock copyinstr */
int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done) {
    (void)done;
    strncpy(kaddr, uaddr, len);
    return 0;
}

/* Mock copyout */
int copyout(const void *kaddr, void *uaddr, size_t len) {
    (void)uaddr;
    memcpy(copyout_buffer, kaddr, len);
    return 0;
}

/* Mock sys_lseek / sys_mmap / sys_time / sys_execve to link compat.c */
int64_t sys_lseek(int fd, uint32_t off_lo, uint32_t off_hi, int whence) {
    (void)fd; (void)off_lo; (void)off_hi; (void)whence; return 0;
}

void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, uint64_t offset) {
    (void)addr; (void)len; (void)prot; (void)flags; (void)fd; (void)offset; return NULL;
}

int64_t sys_time(int64_t *tloc) {
    if (tloc) *tloc = 123456789;
    return 123456789;
}

int sys_execve(const char *path, char **argv, char **envp) {
    (void)path; (void)argv; (void)envp; return 0;
}

void *kmalloc(size_t size) {
    (void)size; return NULL;
}

void kfree(void *ptr, size_t size) {
    (void)ptr; (void)size;
}

int kern_getcwd(char *buf, size_t size) {
    (void)buf; (void)size; return 0;
}

/* Include compat files directly */
#undef _SYS_SIGNAL_H
#define _SYS_SIGNAL_H /* Prevent host signal header conflicts with linux sa_handler */

#include "../../sys/exec/perso/compat.c"
#include "../../sys/exec/perso/linux/linux_user.c"
#include "../../sys/exec/perso/netbsd/netbsd_user.c"
#include "../../sys/exec/perso/sunos/sunos_user.c"

static void setup_mock_stat(void) {
    memset(&mock_native_stat, 0, sizeof(mock_native_stat));
    mock_native_stat.st_dev = 0x12345678;
    mock_native_stat.st_ino = 0x87654321;
    mock_native_stat.st_mode = 0644;
    mock_native_stat.st_nlink = 2;
    mock_native_stat.st_uid = 1000;
    mock_native_stat.st_gid = 1000;
    mock_native_stat.st_rdev = 0;
    mock_native_stat.st_size = 1024 * 1024;
    mock_native_stat.st_blksize = 4096;
    mock_native_stat.st_blocks = 2048;
    mock_native_stat.st_atime = 100;
    mock_native_stat.st_atime_nsec = 10;
    mock_native_stat.st_mtime = 200;
    mock_native_stat.st_mtime_nsec = 20;
    mock_native_stat.st_ctime = 300;
    mock_native_stat.st_ctime_nsec = 30;
}

static void test_freebsd_stat(void) {
    printf("Testing FreeBSD stat translation...\n");
    setup_mock_stat();
    memset(copyout_buffer, 0, sizeof(copyout_buffer));

    int ret = sys_freebsd_stat("/test", NULL);
    assert(ret == 0);

    struct freebsd_stat *fbsd = (struct freebsd_stat *)copyout_buffer;
    assert(fbsd->st_dev == mock_native_stat.st_dev);
    assert(fbsd->st_ino == mock_native_stat.st_ino);
    assert(fbsd->st_mode == mock_native_stat.st_mode);
    assert(fbsd->st_size == mock_native_stat.st_size);
    assert(fbsd->st_atim.tv_sec == mock_native_stat.st_atime);
    assert(fbsd->st_mtim.tv_sec == mock_native_stat.st_mtime);
    assert(fbsd->st_ctim.tv_sec == mock_native_stat.st_ctime);
}

static void test_linux_stat(void) {
    printf("Testing Linux stat translation...\n");
    setup_mock_stat();
    memset(copyout_buffer, 0, sizeof(copyout_buffer));

    int ret = linux_sys_stat("/test", NULL);
    assert(ret == 0);

    struct linux_stat *lstat = (struct linux_stat *)copyout_buffer;
    assert(lstat->st_dev == mock_native_stat.st_dev);
    assert(lstat->st_ino == mock_native_stat.st_ino);
    assert(lstat->st_mode == mock_native_stat.st_mode);
    assert(lstat->st_size == (uint32_t)mock_native_stat.st_size);
    assert(lstat->st_atime == (uint32_t)mock_native_stat.st_atime);
}

static void test_netbsd_stat(void) {
    printf("Testing NetBSD stat translation...\n");
    setup_mock_stat();
    memset(copyout_buffer, 0, sizeof(copyout_buffer));

    int ret = netbsd_sys_stat("/test", NULL);
    assert(ret == 0);

    struct netbsd_stat *nbsd = (struct netbsd_stat *)copyout_buffer;
    assert(nbsd->st_dev == mock_native_stat.st_dev);
    assert(nbsd->st_ino == mock_native_stat.st_ino);
    assert(nbsd->st_mode == mock_native_stat.st_mode);
    assert(nbsd->st_size == mock_native_stat.st_size);
    assert(nbsd->st_atime == mock_native_stat.st_atime);
}

static void test_sunos_stat(void) {
    printf("Testing SunOS stat translation...\n");
    setup_mock_stat();
    memset(copyout_buffer, 0, sizeof(copyout_buffer));

    int ret = sunos_sys_stat("/test", NULL);
    assert(ret == 0);

    struct sunos_stat *sunos = (struct sunos_stat *)copyout_buffer;
    assert(sunos->st_dev == (uint16_t)mock_native_stat.st_dev);
    assert(sunos->st_ino == (uint16_t)mock_native_stat.st_ino);
    assert(sunos->st_mode == mock_native_stat.st_mode);
    assert(sunos->st_size == mock_native_stat.st_size);
    assert(sunos->st_atime == mock_native_stat.st_atime);
}

int main(void) {
    test_freebsd_stat();
    test_linux_stat();
    test_netbsd_stat();
    test_sunos_stat();
    printf("All stat translation tests passed!\n");
    return 0;
}
