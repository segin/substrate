#include "personality.h"
#include <stddef.h>
#include "../../arch/i386/syscall.h"
#include <sys/syscall_impl.h>

/* SVR3-specific externs not in syscall_impl.h */
extern int sys_lchown(const char*, int, int);
extern int sys_stime(uint32_t*);
extern int sys_ptrace(int, int, int, int);
extern int sys_alarm(unsigned int);
extern int sys_pause(void);
extern int sys_utime(const char*, void*);
extern int sys_nice(int);
extern int sys_statfs(const char*, void*);
extern int sys_fstatfs(int, void*);
extern int sys_pgrpsys(int, int, int, int);
extern int sys_sigsys(int, void*);
extern int sys_msgsys(int, int, int, int, int, int);
extern int sys_sysi86(int, int, int, int);
extern int sys_shmsys(int, int, int, int);
extern int sys_semsys(int, int, int, int, int);
extern int sys_uadmin(int, int, int);
extern int sys_utssys(void*, int, int);
extern int sys_ulimit(int, long);
extern int sys_prof(void*, size_t, unsigned long, unsigned int);

static void *svr3_syscalls[MAX_SYSCALLS] = {
    [1] = &sys_exit,
    [2] = &sys_fork,
    [3] = &sys_read,
    [4] = &sys_write,
    [5] = &sys_open,
    [6] = &sys_close,
    [7] = &sys_waitpid,
    [9] = &sys_link,
    [10] = &sys_unlink,
    [12] = &sys_chdir,
    [13] = (void*)sys_time,
    [14] = &sys_mknod,
    [15] = &sys_chmod,
    [16] = &sys_lchown,
    [18] = &sys_stat,
    [19] = (void*)sys_lseek,
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
    [183] = &sys_getcwd,
};

static const char *svr3_names[MAX_SYSCALLS] = {
    [1] = "exit",
    [2] = "fork",
    [3] = "read",
    [4] = "write",
    [5] = "open",
    [6] = "close",
    [7] = "wait",
    [9] = "link",
    [10] = "unlink",
    [12] = "chdir",
    [13] = "time",
    [14] = "mknod",
    [15] = "chmod",
    [16] = "chown",
    [18] = "stat",
    [19] = "lseek",
    [20] = "getpid",
    [21] = "mount",
    [22] = "umount",
    [23] = "setuid",
    [24] = "getuid",
    [33] = "access",
    [34] = "nice",
    [36] = "sync",
    [37] = "kill",
    [41] = "dup",
    [42] = "pipe",
    [46] = "setgid",
    [47] = "getgid",
    [51] = "acct",
    [54] = "ioctl",
    [59] = "exece",
    [61] = "chroot",
    [62] = "fcntl",
    [63] = "ulimit",
    [79] = "rmdir",
    [80] = "mkdir",
    [81] = "getdents",
    [183] = "getcwd",
};

static struct syscall_fmt svr3_fmts[MAX_SYSCALLS] = {
    [1] = { 1, { ARG_INT } },
    [3] = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [4] = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [5] = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [6] = { 1, { ARG_INT } },
    [7] = { 0, { 0 } },
    [9] = { 2, { ARG_STR, ARG_STR } },
    [10] = { 1, { ARG_STR } },
    [12] = { 1, { ARG_STR } },
    [13] = { 1, { ARG_PTR } },
    [14] = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [15] = { 2, { ARG_STR, ARG_HEX } },
    [16] = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [18] = { 2, { ARG_STR, ARG_PTR } },
    [19] = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [21] = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } },
    [22] = { 1, { ARG_STR } },
    [23] = { 1, { ARG_INT } },
    [33] = { 2, { ARG_STR, ARG_HEX } },
    [34] = { 1, { ARG_INT } },
    [37] = { 2, { ARG_INT, ARG_INT } },
    [41] = { 1, { ARG_INT } },
    [42] = { 1, { ARG_PTR } },
    [46] = { 1, { ARG_INT } },
    [51] = { 1, { ARG_STR } },
    [54] = { 3, { ARG_INT, ARG_HEX, ARG_HEX } },
    [59] = { 3, { ARG_STR, ARG_PTR, ARG_PTR } },
    [61] = { 1, { ARG_STR } },
    [62] = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [63] = { 2, { ARG_INT, ARG_INT } },
    [79] = { 1, { ARG_STR } },
    [80] = { 2, { ARG_STR, ARG_HEX } },
    [81] = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [183] = { 2, { ARG_PTR, ARG_INT } },
};

struct personality personality_svr3 = {
    .name = "AT&T UNIX SVR3",
    .syscall_table = svr3_syscalls,
    .syscall_names = svr3_names,
    .syscall_fmts = svr3_fmts,
    .syscall_count = MAX_SYSCALLS
};
