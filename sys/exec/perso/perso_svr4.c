#include "personality.h"
#include <stddef.h>
#include "../../arch/i386/syscall.h"

extern int sys_exit(int);
extern int sys_fork(void);
extern int sys_read(int, char*, int);
extern int sys_write(int, const char*, int);
extern int sys_open(const char*, int, int);
extern int sys_close(int);
extern int sys_waitpid(int, int*, int);
extern int sys_creat(const char*, int);
extern int sys_link(const char*, const char*);
extern int sys_unlink(const char*);
extern int sys_execve(const char*, char**, char**);
extern int sys_chdir(const char*);
extern int sys_time(uint32_t*);
extern int sys_mknod(const char*, int, int);
extern int sys_chmod(const char*, int);
extern int sys_lchown(const char*, int, int);
extern int sys_stat(const char*, void*);
extern int sys_lseek(int, int, int);
extern int sys_getpid(void);
extern int sys_mount(const char*, const char*, const char*, unsigned long, void*);
extern int sys_umount(const char*);
extern int sys_setuid(int);
extern int sys_getuid(void);
extern int sys_stime(uint32_t*);
extern int sys_ptrace(int, int, int, int);
extern int sys_alarm(unsigned int);
extern int sys_fstat(int, void*);
extern int sys_pause(void);
extern int sys_utime(const char*, void*);
extern int sys_access(const char*, int);
extern int sys_nice(int);
extern int sys_statfs(const char*, void*);
extern int sys_sync(void);
extern int sys_kill(int, int);
extern int sys_fstatfs(int, void*);
extern int sys_pgrpsys(int, int, int, int);
extern int sys_dup(int);
extern int sys_dup2(int, int);
extern int sys_pipe(int*);
extern int sys_times(void*);
extern int sys_prof(void*, size_t, unsigned long, unsigned int);
extern int sys_setgid(int);
extern int sys_getgid(void);
extern int sys_sigsys(int, void*);
extern int sys_msgsys(int, int, int, int, int, int);
extern int sys_sysi86(int, int, int, int);
extern int sys_acct(const char*);
extern int sys_shmsys(int, int, int, int);
extern int sys_semsys(int, int, int, int, int);
extern int sys_ioctl(int, int, int);
extern int sys_uadmin(int, int, int);
extern int sys_utssys(void*, int, int);
extern int sys_fsync(int);
extern int sys_umask(int);
extern int sys_chroot(const char*);
extern int sys_fcntl(int, int, int);
extern int sys_ulimit(int, long);
extern int sys_rmdir(const char*);
extern int sys_mkdir(const char*, int);
extern int sys_getdents(unsigned int, void*, unsigned int);
extern int sys_getcwd(char*, size_t);
extern int sys_uname(void*);
extern void *sys_mmap(void*, size_t, int, int, int, uint64_t);
extern int sys_munmap(void*, size_t);
extern int sys_mprotect(void*, size_t, int);
extern int sys_sigaction(int, void*, void*);
extern int sys_sigpending(int, void*);
extern int sys_sigprocmask(int, void*, void*);
extern int sys_sigsuspend(void*);
extern int sys_sigret(void);

static void *svr4_syscalls[MAX_SYSCALLS] = {
    [1] = &sys_exit,
    [2] = &sys_fork,
    [3] = &sys_read,
    [4] = &sys_write,
    [5] = &sys_open,
    [6] = &sys_close,
    [7] = &sys_waitpid,
    [8] = &sys_creat,
    [9] = &sys_link,
    [10] = &sys_unlink,
    [11] = &sys_execve,
    [12] = &sys_chdir,
    [13] = &sys_time,
    [14] = &sys_mknod,
    [15] = &sys_chmod,
    [16] = &sys_lchown,
    [18] = &sys_stat,
    [19] = &sys_lseek,
    [20] = &sys_getpid,
    [21] = &sys_mount,
    [22] = &sys_umount,
    [23] = &sys_setuid,
    [24] = &sys_getuid,
    [33] = &sys_access,
    [34] = &sys_nice,
    [36] = &sys_sync,
    [37] = &sys_kill,
    [41] = &sys_dup,
    [42] = &sys_pipe,
    [46] = &sys_setgid,
    [47] = &sys_getgid,
    [51] = &sys_acct,
    [54] = &sys_ioctl,
    [59] = &sys_execve,
    [61] = &sys_chroot,
    [62] = &sys_fcntl,
    [63] = &sys_dup2,
    [79] = &sys_rmdir,
    [80] = &sys_mkdir,
    [81] = &sys_getdents,
    [91] = &sys_mmap,
    [92] = &sys_munmap,
    [93] = &sys_mprotect,
    [105] = &sys_sigaction,
    [106] = &sys_sigpending,
    [107] = &sys_sigprocmask,
    [108] = &sys_sigsuspend,
    [109] = &sys_sigret,
    [183] = &sys_getcwd,
};

struct personality personality_svr4 = {
    .name = "AT&T UNIX SVR4",
    .syscall_table = svr4_syscalls,
    .syscall_count = MAX_SYSCALLS
};
