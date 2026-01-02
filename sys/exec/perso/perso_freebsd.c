#include "personality.h"
#include "../../arch/i386/syscall.h"
#include <stddef.h>

// FreeBSD syscall declarations
extern int sys_exit(int);
extern int sys_fork(void);
extern int sys_read(int, char*, int);
extern int sys_write(int, const char*, int);
extern int sys_open(const char*, int, int);
extern int sys_close(int);
extern int sys_execve(const char*, char**, char**);
extern int sys_chdir(const char*);
extern int sys_getpid(void);
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

// FreeBSD syscall numbers (from sys/syscall.h)
static void *freebsd_syscalls[MAX_SYSCALLS] = {
    [1] = &sys_exit,
    [2] = &sys_fork,
    [3] = &sys_read,
    [4] = &sys_write,
    [5] = &sys_open,
    [6] = &sys_close,
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
    [136] = &sys_mkdir,
    [137] = &sys_rmdir,
    [188] = &sys_stat,   // FreeBSD: stat
    [189] = &sys_fstat,  // FreeBSD: fstat
    [190] = &sys_lstat,  // FreeBSD: lstat
    [326] = &sys_getcwd,
};

struct personality personality_freebsd = {
    .name = "FreeBSD",
    .syscall_table = freebsd_syscalls,
    .syscall_count = MAX_SYSCALLS
};