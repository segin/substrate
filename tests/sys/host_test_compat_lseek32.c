/*
 * host_test_compat_lseek32 — focused host unit test for compat_lseek32().
 *
 * compat_lseek32() is the 32-bit lseek shim foreign personalities use: it
 * sign-extends a signed 32-bit offset to the native 64-bit lseek, then clamps
 * the result back into the signed-32-bit range (returning -1 / EOVERFLOW when
 * it does not fit).  This test compiles the real sys/exec/perso/compat.c and
 * drives compat_lseek32 directly.
 *
 * Build (see tests/sys/Makefile): -Imock_compat_include is searched FIRST and
 * supplies empty stubs for <sys/syscall_impl.h>, <sys/kern_syscalls.h>,
 * <sys/stat.h>, <exec/perso/compat.h> and the freebsd headers — so compat.c
 * gets NO real declarations for the helpers it calls, and this file must
 * provide every one as a mock.  We additionally block the host headers that
 * would otherwise collide (struct stat from <fcntl.h>, the host <sys/sysinfo.h>
 * lacking sys_procinfo, etc.) using the same kernel-first technique as
 * host_test_compat.c.
 */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

#include <sys/types.h>

/* tid_t lives in the kernel <sys/types.h>; the host one we just pulled lacks
 * it but kernel <sys/sysinfo.h> (pre-included below) needs it. */
typedef int32_t tid_t;

/* proc.h needs AC_COMM_LEN (from <sys/acct.h>, host version lacks it). */
#ifndef AC_COMM_LEN
#define AC_COMM_LEN 16
#endif

/* Native struct stat the compat translators copy from (suppress kernel one). */
#define _SYS_STAT_H
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

/*
 * Block the host <fcntl.h> (guard-less host <sys/fcntl.h> pulls it in and it
 * redefines struct stat + installs st_atime->st_atim.tv_sec macros); the
 * kernel <sys/fcntl.h> supplies the O_ and F_ constants compat.c needs.  Also
 * force the kernel <sys/sysinfo.h> (host one lacks struct sys_procinfo).
 */
#define _FCNTL_H
#include "../../sys/include/sys/fcntl.h"
#include "../../sys/include/sys/file.h"
#define _SYS_SYSINFO_H
#include "../../sys/include/sys/sysinfo.h"

#include <string.h>

/* ---- mocks for the helpers compat.c calls (none real under the mock dir) ---- */

static int      mock_sys_lseek_fd;
static uint32_t mock_sys_lseek_off_lo;
static uint32_t mock_sys_lseek_off_hi;
static int      mock_sys_lseek_whence;
static int64_t  mock_sys_lseek_return;

int64_t sys_lseek(int fd, uint32_t off_lo, uint32_t off_hi, int whence) {
    mock_sys_lseek_fd = fd;
    mock_sys_lseek_off_lo = off_lo;
    mock_sys_lseek_off_hi = off_hi;
    mock_sys_lseek_whence = whence;
    return mock_sys_lseek_return;
}

int sys_time(void *t) { (void)t; return 0; }
void *sys_mmap(void *a, size_t l, int p, int f, int fd, int o) {
    (void)a; (void)l; (void)p; (void)f; (void)fd; (void)o; return NULL;
}
int sys_execve(const char *p, char *const a[], char *const e[]) {
    (void)p; (void)a; (void)e; return 0;
}
int copyin(const void *s, void *d, size_t n) { if (s && d) memcpy(d, s, n); return 0; }
int copyout(const void *s, void *d, size_t n) { if (s && d) memcpy(d, s, n); return 0; }
int copyinstr(const void *u, void *k, size_t n, size_t *done) {
    if (u && k) { strncpy((char *)k, (const char *)u, n); if (done) *done = strlen((const char *)u) + 1; }
    return 0;
}
int kern_stat(const char *p, struct stat *st) { (void)p; (void)st; return 0; }
int kern_lstat(const char *p, struct stat *st) { (void)p; (void)st; return 0; }
int kern_fstat(int fd, struct stat *st) { (void)fd; (void)st; return 0; }

ssize_t sys_read(int fd, char *b, size_t n) { (void)fd; (void)b; (void)n; return 0; }
ssize_t sys_write(int fd, const char *b, size_t n) { (void)fd; (void)b; (void)n; return 0; }
int sys_nanosleep(const void *r, void *m) { (void)r; (void)m; return 0; }
int sys_poll(void *f, unsigned int n, int t) { (void)f; (void)n; (void)t; return 0; }
int sys_waitpid(int p, int *s, int o) { (void)p; (void)s; (void)o; return 0; }
int sys_fsync(int fd) { (void)fd; return 0; }
int sys_cpu_count(void) { return 1; }
int vt_get_active(void) { return 0; }
int kern_ioctl(int fd, uint32_t r, void *a) { (void)fd; (void)r; (void)a; return 0; }
int kern_proc_info(pid_t p, sys_procinfo_t *i) { (void)p; (void)i; return -1; }
int kern_proc_list(pid_t *p, size_t c) { (void)p; (void)c; return 0; }
time_t kern_time(time_t *t) { if (t) *t = 0; return 0; }
int random_get_bytes_flags(void *b, size_t l, unsigned int f) { (void)b; (void)l; (void)f; return 0; }
void sched_sleep(void *c) { (void)c; }
int sys_accept(int fd, void *a, int *l) { (void)fd; (void)a; (void)l; return -1; }
int sys_dup2(int o, int n) { (void)o; return n; }
void *kmalloc(size_t s) { (void)s; return NULL; }
void kfree(void *p, size_t s) { (void)p; (void)s; }

/* Include the real compat.c under test. */
#include "../../sys/exec/perso/compat.c"

/* Globals + type-dependent stubs (kernel struct types now in scope). */
char kernel_hostname[256];
thread_t *current_thread;
process_t *current_process;
fs_node_t *fs_root;

int proc_fcntl(process_t *p, int fd, int cmd, int arg) {
    (void)p; (void)fd; (void)cmd; (void)arg; return 0;
}
int tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg) {
    (void)tty; (void)cmd; (void)arg; return 0;
}
fs_node_t *vfs_lookup(fs_node_t *root, const char *path) {
    (void)root; (void)path; return NULL;
}
int vm_map_protect(struct vm_map *map, uintptr_t start, uintptr_t end, uint8_t prot) {
    (void)map; (void)start; (void)end; (void)prot; return 0;
}

int main(void) {
    printf("Testing compat_lseek32()...\n");

    /* Basic forward seek: positive offset is zero-extended; result passthrough. */
    mock_sys_lseek_return = 1024;
    assert(compat_lseek32(5, 100, 0) == 1024);
    assert(mock_sys_lseek_fd == 5);
    assert(mock_sys_lseek_off_lo == 100);
    assert(mock_sys_lseek_off_hi == 0);
    assert(mock_sys_lseek_whence == 0);

    /* Negative offset is sign-extended (off_hi = 0xFFFFFFFF). */
    mock_sys_lseek_return = 512;
    assert(compat_lseek32(6, -100, 1) == 512);
    assert(mock_sys_lseek_fd == 6);
    assert(mock_sys_lseek_off_lo == (uint32_t)-100);
    assert(mock_sys_lseek_off_hi == 0xFFFFFFFFu);
    assert(mock_sys_lseek_whence == 1);

    /* Result above INT32_MAX -> EOVERFLOW (-1). */
    mock_sys_lseek_return = 0x80000000LL;
    assert(compat_lseek32(5, 0, 0) == -1);

    /* Exactly INT32_MAX is representable. */
    mock_sys_lseek_return = 0x7FFFFFFFLL;
    assert(compat_lseek32(5, 0, 0) == 0x7FFFFFFF);

    /* Below INT32_MIN -> EOVERFLOW (-1). */
    mock_sys_lseek_return = -0x80000001LL;
    assert(compat_lseek32(5, 0, 0) == -1);

    /* Exactly INT32_MIN is representable. */
    mock_sys_lseek_return = -0x80000000LL;
    assert(compat_lseek32(5, 0, 0) == (int32_t)-0x80000000LL);

    printf("All compat_lseek32 tests passed!\n");
    puts("host_test_compat_lseek32: ok");
    return 0;
}
