#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

#include <sys/types.h>

/*
 * The kernel <sys/sysinfo.h> (pre-included below) references tid_t, which
 * lives in the kernel <sys/types.h>; the host <sys/types.h> we just pulled
 * lacks it.  Supply it directly (matches sys/include/sys/types.h).
 */
typedef int32_t tid_t;

/*
 * proc.h (pulled in transitively by compat.c) declares `char comm[AC_COMM_LEN]`
 * and gets AC_COMM_LEN from <sys/acct.h>.  Under the host include ordering the
 * host <sys/acct.h> wins (and lacks AC_COMM_LEN), so define it here.
 */
#ifndef AC_COMM_LEN
#define AC_COMM_LEN 16
#endif

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
#include <string.h>

static int mock_copyinstr_ret = 0;
int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done) {
    if (mock_copyinstr_ret != 0) return mock_copyinstr_ret;
    if (uaddr && kaddr) {
        strncpy((char *)kaddr, (const char *)uaddr, len);
        if (done) *done = strlen((const char *)uaddr) + 1;
    }
    return 0;
}

static int mock_copyout_ret = 0;
int copyout(const void *kaddr, void *uaddr, size_t len) {
    if (mock_copyout_ret != 0) return mock_copyout_ret;
    if (kaddr && uaddr) {
        memcpy(uaddr, kaddr, len);
    }
    return 0;
}

static int mock_kern_stat_ret = 0;
static struct stat mock_kern_stat_buf;
static char mock_kern_stat_path[256];

int kern_stat(const char *path, struct stat *st) {
    if (path) strncpy(mock_kern_stat_path, path, sizeof(mock_kern_stat_path));
    if (st) *st = mock_kern_stat_buf;
    return mock_kern_stat_ret;
}

static int mock_kern_lstat_ret = 0;
static struct stat mock_kern_lstat_buf;
static char mock_kern_lstat_path[256];

int kern_lstat(const char *path, struct stat *st) {
    if (path) strncpy(mock_kern_lstat_path, path, sizeof(mock_kern_lstat_path));
    if (st) *st = mock_kern_lstat_buf;
    return mock_kern_lstat_ret;
}

static int mock_kern_fstat_ret = 0;
static struct stat mock_kern_fstat_buf;
static int mock_kern_fstat_fd = -1;

int kern_fstat(int fd, struct stat *st) {
    mock_kern_fstat_fd = fd;
    if (st) *st = mock_kern_fstat_buf;
    return mock_kern_fstat_ret;
}

/*
 * compat.c includes <sys/file.h> and <sys/fcntl.h>.  Under the host include
 * ordering those resolve to /usr/include/sys/{file,fcntl}.h, which drag in
 * <fcntl.h> -> <bits/struct_stat.h> and define a SECOND `struct stat` that
 * clashes with the test's mock above.  Pre-include the kernel versions
 * (which only forward-declare struct stat) by their direct path so their
 * include guards fire and the host headers are skipped.
 */
/*
 * The host <sys/fcntl.h> is guard-less and unconditionally pulls in
 * <fcntl.h> -> <bits/struct_stat.h>, which both redefines `struct stat`
 * AND installs `#define st_atime st_atim.tv_sec` macros that break the
 * test's struct-stat field accesses.  Block host <fcntl.h> outright; the
 * kernel <sys/fcntl.h> (pre-included below) supplies the O_ and F_
 * constants compat.c needs.
 */
#define _FCNTL_H
#include "../../sys/include/sys/fcntl.h"
#include "../../sys/include/sys/file.h"

/*
 * compat.c needs the kernel <sys/sysinfo.h> (defines struct sys_procinfo /
 * sys_procinfo_t).  Host <sys/sysinfo.h> (guard _SYS_SYSINFO_H) would win
 * under the include ordering and lacks it, leaving sys_procinfo_t an
 * incomplete type.  Block the host one and pre-include the kernel header.
 */
#define _SYS_SYSINFO_H
#include "../../sys/include/sys/sysinfo.h"

/* New cross-subsystem calls compat.c gained; the tested entry points
 * (compat_lseek32, compat_time32, sys_nice) never reach them, so trivial
 * stubs satisfy the linker / -Werror=implicit-function-declaration. */
ssize_t sys_read(int fd, char *buf, size_t n) { (void)fd; (void)buf; (void)n; return 0; }
ssize_t sys_write(int fd, const char *buf, size_t n) { (void)fd; (void)buf; (void)n; return 0; }
int sys_nanosleep(const void *req, void *rem) { (void)req; (void)rem; return 0; }
int sys_poll(void *fds, unsigned int nfds, int timeout) { (void)fds; (void)nfds; (void)timeout; return 0; }
int sys_waitpid(int pid, int *status, int options) { (void)pid; (void)status; (void)options; return 0; }
int sys_fsync(int fd) { (void)fd; return 0; }
int sys_cpu_count(void) { return 1; }
int vt_get_active(void) { return 0; }
int copyin(const void *src, void *dst, size_t size) { if (src && dst) memcpy(dst, src, size); return 0; }
int kern_ioctl(int fd, uint32_t request, void *arg) { (void)fd; (void)request; (void)arg; return 0; }
int kern_proc_info(pid_t pid, sys_procinfo_t *info) { (void)pid; (void)info; return -1; }
int kern_proc_list(pid_t *pids, size_t count) { (void)pids; (void)count; return 0; }
time_t kern_time(time_t *tloc) { if (tloc) *tloc = 0; return 0; }
int random_get_bytes_flags(void *buf, size_t len, unsigned int flags) { (void)buf; (void)len; (void)flags; return 0; }
void sched_sleep(void *chan) { (void)chan; }
int sys_accept(int fd, void *addr, int *addrlen) { (void)fd; (void)addr; (void)addrlen; return -1; }
int sys_dup2(int oldfd, int newfd) { (void)oldfd; (void)newfd; return newfd; }
void *kmalloc(size_t size) { (void)size; return 0; }
void kfree(void *ptr, size_t size) { (void)ptr; (void)size; }

/* Now we can include compat.c directly! */
#include "../../sys/exec/perso/compat.c"

/* Globals + stubs that reference kernel struct types / constants defined by
 * compat.c's own includes; placed after the include so the full
 * definitions are in scope.  None are reached by the tested entry points. */
char kernel_hostname[256]; /* MAXHOSTNAMELEN; not reached by tested paths */
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
    (void)root; (void)path; return 0;
}
int vm_map_protect(struct vm_map *map, uintptr_t start, uintptr_t end, uint8_t prot) {
    (void)map; (void)start; (void)end; (void)prot; return 0;
}

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

/*
 * NOTE: the former test_sys_freebsd_stat() was removed.  The FreeBSD stat
 * translation it exercised (sys_freebsd_stat / struct freebsd_stat copyout)
 * was refactored OUT of compat.c in commit 6eb0146d ("Refactor FreeBSD stat
 * translations out of compat.c") and now lives in
 * sys/exec/perso/perso_freebsd.c (freebsd_sys_{stat,lstat,fstat}).  That
 * code is covered by host_test_compat_stat, which links the perso user.c
 * files.  compat.c no longer defines sys_freebsd_stat, so the test was DEAD.
 */

int main() {
    printf("Testing sys_nice()...\n");

    /*
     * sys_nice() is no longer an -ENOSYS stub: it now implements POSIX
     * nice(2) against current_thread->base_priority / current_process->euid
     * (compat.c r6eb0146d-era).  Drive the real implementation with a fake
     * thread/process and assert the clamp + root-only-raise-priority rules.
     */
    static thread_t nice_thread;
    static process_t nice_proc;
    memset(&nice_thread, 0, sizeof(nice_thread));
    memset(&nice_proc, 0, sizeof(nice_proc));
    current_thread = &nice_thread;
    current_process = &nice_proc;

    /* Non-root: lowering priority (raising nice) is allowed. */
    nice_proc.euid = 1000;
    nice_thread.base_priority = 0;
    assert(sys_nice(5) == 5);
    assert(nice_thread.base_priority == 5);
    assert(nice_thread.priority == 5);

    /* Non-root: raising priority (decreasing nice) is denied with EPERM. */
    nice_thread.base_priority = 5;
    assert(sys_nice(-1) == -EPERM);
    assert(nice_thread.base_priority == 5); /* unchanged */

    /* Clamp at the upper bound (19). */
    nice_thread.base_priority = 0;
    assert(sys_nice(100) == 19);
    assert(nice_thread.base_priority == 19);

    /* Root may raise priority (decrease nice); clamp at the lower bound. */
    nice_proc.euid = 0;
    nice_thread.base_priority = 0;
    assert(sys_nice(-100) == -20);
    assert(nice_thread.base_priority == -20);

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

    printf("All compat_time32 tests passed!\n");
    test_compat_lseek32();

    puts("host_test_compat: ok");
    return 0;
}
