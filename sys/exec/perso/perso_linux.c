#include "personality.h"
#include <stddef.h>
#include "../../arch/i386/syscall.h"

extern int sys_exit(int);
extern int sys_write(int, const char*, int);
extern int sys_read(int, char*, int);
extern int sys_open(const char*, int, int);
extern int sys_close(int);
extern int sys_lseek(int, int, int);
extern int sys_getuid(void);
extern int sys_getgid(void);
extern int sys_geteuid(void);
extern int sys_getegid(void);
extern int sys_setuid(int);
extern int sys_setgid(int);
extern int sys_mkdir(const char*, int);
extern int sys_rmdir(const char*);
extern int sys_mknod(const char*, int, int);
extern int sys_mount(const char*, const char*, const char*, unsigned long, void*);
extern int sys_umount(const char*);
extern int sys_access(const char*, int);
extern int sys_stat(const char*, void*);
extern int sys_nanosleep(void*, void*);
extern int sys_sync(void);
extern int sys_kill(int, int);
extern int sys_signal(int, void*);
extern int sys_pipe(int*);
extern int sys_dup2(int, int);
extern int sys_uname(void*);
extern int sys_getdents(unsigned int, void*, unsigned int);
extern int sys_acct(const char*);
extern int sys_time(uint32_t*);
extern int sys_getpid(void);
extern int sys_getcwd(char*, size_t);
extern int sys_clone(uint32_t, void*, int*, void*, int*);
extern int sys_futex(int*, int, int, void*, int*, int);
extern int sys_fork(void);
extern int sys_vfork(void);
extern int sys_execve(const char*, char**, char**);

static void *linux_syscalls[MAX_SYSCALLS] = {
    [1] = &sys_exit,
    [2] = &sys_fork,
    [3] = &sys_read,
    [4] = &sys_write,
    [5] = &sys_open,
    [6] = &sys_close,
    [11] = &sys_execve,
    [13] = &sys_time,
    [14] = &sys_mknod,
    [19] = &sys_lseek,
    [20] = &sys_getpid,
    [21] = &sys_mount,
    [22] = &sys_umount,
    [23] = &sys_setuid,
    [24] = &sys_getuid,
    [33] = &sys_access,
    [36] = &sys_sync,
    [37] = &sys_kill,
    [39] = &sys_mkdir,
    [40] = &sys_rmdir,
    [42] = &sys_pipe,
    [46] = &sys_setgid,
    [47] = &sys_getgid,
    [48] = &sys_signal,
    [49] = &sys_geteuid,
    [50] = &sys_getegid,
    [51] = &sys_acct,
    [63] = &sys_dup2,
    [106] = &sys_stat,
    [120] = &sys_clone,
    [122] = &sys_uname,
    [141] = &sys_getdents,
    [162] = &sys_nanosleep,
    [183] = &sys_getcwd,
    [190] = &sys_vfork,
    [240] = &sys_futex,
};

struct personality personality_linux = {
    .name = "Linux",
    .syscall_table = linux_syscalls,
    .syscall_count = MAX_SYSCALLS
};
