/*
 * host_test_compat_stat - host unit test for the stat(2) translations of
 * the Linux, NetBSD and SunOS personalities.
 *
 * The personalities' user.c files are compiled whole against the kernel
 * headers; the build rule uses -ffunction-sections and --gc-sections so
 * only what the stat entry points reach has to link.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../sys/exec/perso/linux/linux_user.c"
#include "../../sys/exec/perso/netbsd/netbsd_user.c"
#include "../../sys/exec/perso/sunos/sunos_user.c"

thread_t *current_thread;
process_t *current_process;

/* The native stat every kern_*stat mock reports, and the copyout sink. */
static struct stat mock_native_stat;
static char copyout_buffer[4096];

int kern_stat(const char *path, struct stat *st) {
    (void)path;
    memcpy(st, &mock_native_stat, sizeof(*st));
    return 0;
}

int kern_lstat(const char *path, struct stat *st) {
    (void)path;
    memcpy(st, &mock_native_stat, sizeof(*st));
    return 0;
}

int kern_fstat(int fd, struct stat *st) {
    (void)fd;
    memcpy(st, &mock_native_stat, sizeof(*st));
    return 0;
}

int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done) {
    size_t n = strnlen((const char *)uaddr, len);

    if (n >= len) return -ENAMETOOLONG;
    memcpy(kaddr, uaddr, n + 1);
    if (done) *done = n + 1;
    return 0;
}

int copyout(const void *kaddr, void *uaddr, size_t len) {
    (void)uaddr;
    assert(len <= sizeof(copyout_buffer));
    memcpy(copyout_buffer, kaddr, len);
    return 0;
}

void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

/* No personality tree: the SunOS path is looked up as given. */
int perso_tree_path(const char *path, char *out, size_t size) {
    (void)path; (void)out; (void)size;
    return 0;
}

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

/* SunOS 4 stat(2) has 4.3BSD's layout, struct freebsd_ostat. */
static void test_sunos_stat(void) {
    printf("Testing SunOS stat translation...\n");
    setup_mock_stat();
    memset(copyout_buffer, 0, sizeof(copyout_buffer));

    int ret = sunos_sys_stat("/test", NULL);
    assert(ret == 0);

    struct freebsd_ostat *sunos = (struct freebsd_ostat *)copyout_buffer;
    assert(sunos->st_dev == (uint16_t)mock_native_stat.st_dev);
    assert(sunos->st_ino == (uint32_t)mock_native_stat.st_ino);
    assert(sunos->st_mode == mock_native_stat.st_mode);
    assert(sunos->st_size == mock_native_stat.st_size);
    assert(sunos->st_atim_sec == mock_native_stat.st_atime);
    assert(sunos->st_blocks == mock_native_stat.st_blocks);
}

int main(void) {
    test_linux_stat();
    test_netbsd_stat();
    test_sunos_stat();
    printf("All stat translation tests passed!\n");
    return 0;
}
