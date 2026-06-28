#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

/*
 * Suppress only the kernel <sys/stat.h>: the test defines its own native
 * `struct stat` below (the compat translators copy from it).  We deliberately
 * DO NOT suppress <sys/kern_syscalls.h> / <sys/syscall_impl.h> / <sys/copy.h>
 * / <exec/perso/personality.h> anymore — compat.c + the perso user.c files
 * grew to call many sys_ and kern_ helpers and reference PERS_ constants, so
 * we need their real declarations (kern_stat et al. resolve against the
 * test's struct stat, which matches the mocks).
 */
#define _SYS_STAT_H
/*
 * The kernel <sys/stat.h> is suppressed (we supply our own native struct stat
 * below), but the perso translators reference its S_IFIFO mode bit; define it
 * here (matches sys/include/sys/stat.h).
 */
#define S_IFIFO 0010000

/*
 * <sys/proc.h> (pulled in by the compat sources) includes several headers
 * that, under this rule's include ordering, resolve to the host versions and
 * break the build:
 *
 *  - <sys/acct.h>: host's `extern int acct(...) __THROW;` references the
 *    undefined-here __THROW macro, derailing the parser and cascading into
 *    bogus "storage class specified for parameter" errors in sys/lock.h and
 *    sys/resource.h.  The kernel header also provides AC_COMM_LEN.
 *  - <sys/signal.h>: host's is guard-less and pulls in <signal.h>, which
 *    lacks NSIG/stack_t that proc.h needs.
 *
 * Force the kernel versions (distinct _SUBSTRATE_* guards) by blocking the
 * host headers and pre-including the kernel ones directly.
 */
#define _SYS_ACCT_H          /* block host /usr/include/sys/acct.h */
#define _SIGNAL_H            /* block host <signal.h> (pulled via host sys/signal.h) */
#define _SYS_SYSINFO_H       /* block host /usr/include/sys/sysinfo.h */
/*
 * Kernel <sys/uio.h> shares the _SYS_UIO_H guard with the host header but the
 * host one lacks `enum uio_seg` (needed by <sys/namei.h>'s nameidata).  We
 * therefore can't pre-define the guard to block the host one; instead include
 * the KERNEL uio.h first by its direct path so it wins the guard, after which
 * any later <sys/uio.h> (host or userland) is a no-op.
 */
#include "../../sys/include/sys/uio.h"
#include "../../sys/include/sys/acct.h"
#include "../../sys/include/sys/signal.h"
/*
 * Kernel <sys/sysinfo.h> defines the full `struct sys_procinfo` (with fields
 * like is_kernel) that the perso user.c files use; the host header would win
 * under the include ordering and lacks it.  Block + pre-include the kernel one.
 */
#include "../../sys/include/sys/sysinfo.h"

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

/* compat.c gained a /dev/tty path that queries the active VT; not reached. */
int vt_get_active(void) { return 0; }

/* Include compat files directly */
#include "../../sys/exec/perso/compat.c"
#include "../../sys/exec/perso/linux/linux_user.c"
#include "../../sys/exec/perso/netbsd/netbsd_user.c"
#include "../../sys/exec/perso/sunos/sunos_user.c"

/*
 * Cross-subsystem helpers referenced by compat.c and the perso user.c files
 * but not by the stat-translation entry points under test.  Trivial stubs
 * satisfy the linker.  Placed after the includes so the kernel struct/enum
 * types they reference are in scope.
 */
int copyin(const void *src, void *dst, size_t size) { if (src && dst) memcpy(dst, src, size); return 0; }
ssize_t sys_read(int fd, char *buf, size_t n) { (void)fd; (void)buf; (void)n; return 0; }
ssize_t sys_write(int fd, const char *buf, size_t n) { (void)fd; (void)buf; (void)n; return 0; }
int sys_nanosleep(void *req, void *rem) { (void)req; (void)rem; return 0; }
int sys_poll(void *fds, unsigned int nfds, int timeout) { (void)fds; (void)nfds; (void)timeout; return 0; }
int sys_waitpid(int pid, int *status, int options) { (void)pid; (void)status; (void)options; return 0; }
int sys_fsync(int fd) { (void)fd; return 0; }
int sys_cpu_count(void) { return 1; }
int sys_accept(int fd, void *addr, int *addrlen) { (void)fd; (void)addr; (void)addrlen; return -1; }
int sys_dup2(int oldfd, int newfd) { (void)oldfd; (void)newfd; return newfd; }
int sys_fchownat(int dfd, const char *p, int uid, int gid, int flags) { (void)dfd; (void)p; (void)uid; (void)gid; (void)flags; return 0; }
int sys_ftruncate(int fd, uint32_t lo, uint32_t hi) { (void)fd; (void)lo; (void)hi; return 0; }
int sys_truncate(const char *p, uint32_t lo, uint32_t hi) { (void)p; (void)lo; (void)hi; return 0; }
int sys_mknod(const char *p, int mode, int dev) { (void)p; (void)mode; (void)dev; return 0; }
int sys_reboot(int how) { (void)how; return 0; }
int sys_thr_kill(long id, int sig) { (void)id; (void)sig; return 0; }
int kern_ioctl(int fd, uint32_t request, void *arg) { (void)fd; (void)request; (void)arg; return 0; }
int kern_fstatat(int dirfd, const char *path, struct stat *buf, int flags) { (void)dirfd; (void)path; (void)buf; (void)flags; return 0; }
int kern_chmodat(int dirfd, const char *path, int mode, int flags) { (void)dirfd; (void)path; (void)mode; (void)flags; return 0; }
int kern_hostname(char *buf, size_t len) { (void)buf; (void)len; return 0; }
int kern_proc_argv(pid_t pid, char *buf, size_t buflen, int *nargv) { (void)pid; (void)buf; (void)buflen; (void)nargv; return 0; }
int kern_proc_info(pid_t pid, sys_procinfo_t *info) { (void)pid; (void)info; return -1; }
int kern_proc_list(pid_t *pids, size_t count) { (void)pids; (void)count; return 0; }
time_t kern_time(time_t *tloc) { if (tloc) *tloc = 0; return 0; }
clock_t kern_times(struct tms *buf) { (void)buf; return 0; }
int random_get_bytes_flags(void *buf, size_t len, unsigned int flags) { (void)buf; (void)len; (void)flags; return 0; }
void sched_sleep(void *chan) { (void)chan; }
int i386_set_gsbase(uint32_t base) { (void)base; return 0; }
int netbsd_to_native_signo(int sig) { return sig; }
uint32_t pmm_get_total_memory(void) { return 0; }
fs_node_t *vfs_lookup(fs_node_t *root, const char *path) { (void)root; (void)path; return 0; }
int proc_fcntl(process_t *p, int fd, int cmd, int arg) { (void)p; (void)fd; (void)cmd; (void)arg; return 0; }
int tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg) { (void)tty; (void)cmd; (void)arg; return 0; }
int vm_map_protect(struct vm_map *map, uintptr_t start, uintptr_t end, uint8_t prot) { (void)map; (void)start; (void)end; (void)prot; return 0; }

char kernel_hostname[256];
thread_t *current_thread;
process_t *current_process;
fs_node_t *fs_root;

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

/*
 * NOTE: the former test_freebsd_stat() was removed.  sys_freebsd_stat was
 * refactored out of compat.c into freebsd_sys_stat in
 * sys/exec/perso/freebsd/freebsd_user.c (commit 6eb0146d).  Unlike the
 * linux/netbsd/sunos user.c files, freebsd_user.c grew heavy dependencies
 * (struct file / file_t fstat helpers, struct statfs) that cannot be
 * satisfied in this lightweight host harness, so it cannot be #included
 * here.  The stat-translation pattern it shares with the other three
 * personalities remains covered by test_{linux,netbsd,sunos}_stat below.
 */

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
    test_linux_stat();
    test_netbsd_stat();
    test_sunos_stat();
    printf("All stat translation tests passed!\n");
    return 0;
}
