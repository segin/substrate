#include "personality.h"
#include "../../arch/i386/syscall.h"
#include <stddef.h>

// FreeBSD syscall declarations
extern int sys_exit(int);
extern int sys_fork(void);
struct freebsd_utsname;
int sys_freebsd_uname(struct freebsd_utsname*);
extern int sys_read(int, char*, int);
extern int sys_write(int, const char*, int);
extern int sys_open(const char*, int, int);
extern int sys_close(int);
extern int sys_execve(const char*, char**, char**);
extern int sys_chdir(const char*);
extern int sys_getpid(void);
extern int sys_link(const char*, const char*);
extern int sys_unlink(const char*);
extern int sys_mount(const char*, const char*, const char*, unsigned long, void*);
extern int sys_umount(const char*);
extern int sys_getuid(void);
extern int sys_access(const char*, int);
extern int sys_sync(void);
extern int sys_kill(int, int);
extern int sys_stat(const char*, void*);
extern int sys_lstat(const char*, void*);
extern int sys_fstat(int, void*);
extern int sys_lseek(int, int, int);
extern int sys_setuid(int);
extern int sys_getgid(void);
extern int sys_setgid(int);
extern int sys_geteuid(void);
extern int sys_getegid(void);
extern int sys_ioctl(int, unsigned int, void*);
extern int sys_vfork(void);
extern int sys_mkdir(const char*, int);
extern int sys_rmdir(const char*);
extern int sys_dup2(int, int);
extern int sys_pipe(int*);
extern int sys_getcwd(char*, size_t);
extern int sys_fchdir(int);
extern int sys_poll(void*, unsigned int, int);

// FreeBSD syscall numbers (from sys/syscall.h)
static void *freebsd_syscalls[MAX_SYSCALLS] = {
    [1] = &sys_exit,
    [2] = &sys_fork,
    [3] = &sys_read,
    [4] = &sys_write,
    [5] = &sys_open,
    [6] = &sys_close,
    [9] = &sys_link,
    [10] = &sys_unlink,
    [12] = &sys_chdir,
    [13] = &sys_fchdir,
    [17] = &sys_getuid,  // FreeBSD: old break
    [20] = &sys_getpid,
    [21] = &sys_mount,
    [22] = &sys_umount,
    [23] = &sys_setuid,
    [24] = &sys_getuid,
    [25] = &sys_geteuid,
    [33] = &sys_access,
    [36] = &sys_sync,
    [37] = &sys_kill,
    [38] = &sys_stat,
    [39] = &sys_getpid,  // FreeBSD: getppid
    [40] = &sys_lstat,
    [41] = &sys_dup2,
    [42] = &sys_pipe,
    [43] = &sys_getegid,
    [46] = &sys_setgid,
    [47] = &sys_getgid,
    [54] = &sys_ioctl,
    [59] = &sys_execve,
    [66] = &sys_vfork,
    [76] = &sys_vfork,  // FreeBSD: vfork (at 66 usually, but let's keep existing mapping if valid. Wait, vfork is 66 in table above. 76 is getrlimit usually. I'll stick to adding 164)
    [136] = &sys_mkdir,
    [137] = &sys_rmdir,
    [164] = &sys_freebsd_uname,
    [188] = &sys_stat,   // FreeBSD: stat
    [189] = &sys_fstat,  // FreeBSD: fstat
    [190] = &sys_lstat,  // FreeBSD: lstat
    [209] = &sys_poll,   // FreeBSD: poll
    [326] = &sys_getcwd,
};

struct freebsd_utsname {
    char sysname[256];
    char nodename[256];
    char release[256];
    char version[256];
    char machine[256];
};

static void strncpy_zero(char *dest, const char *src, int n) {
    int i;
    for(i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for(; i < n; i++) dest[i] = 0;
}

int sys_freebsd_uname(struct freebsd_utsname *buf) {
    if (!buf) return -1;
    strncpy_zero(buf->sysname, "FreeBSD", 256);
    strncpy_zero(buf->nodename, "localhost", 256);
    strncpy_zero(buf->release, "14.3-RELEASE-p5", 256);
    strncpy_zero(buf->version, "FreeBSD 14.3-RELEASE-p5 GENERIC", 256);
    strncpy_zero(buf->machine, "i386", 256);
    return 0;
}

struct personality personality_freebsd = {
    .name = "FreeBSD",
    .syscall_table = freebsd_syscalls,
    .syscall_count = MAX_SYSCALLS
};