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
extern int sys_lstat(const char*, void*);
extern int sys_fstat(int, void*);
extern int sys_nanosleep(void*, void*);
extern int sys_sync(void);
extern int sys_kill(int, int);
extern int sys_signal(int, void*);
extern int sys_pipe(int*);
extern int sys_dup2(int, int);
extern int sys_uname(void*);
extern int sys_getdents(unsigned int, void*, unsigned int);
struct thr_param;
extern int sys_thr_new(struct thr_param*, int);
extern int sys_acct(const char*);
extern int sys_time(uint32_t*);
extern int sys_getpid(void);
extern int sys_getcwd(char*, size_t);
extern int sys_execve(const char*, char**, char**);
extern int sys_fork(void);
extern int sys_vfork(void);
extern int sys_link(const char*, const char*);
struct pmap_stats;
extern int sys_pmap_stats(struct pmap_stats*);
extern int sys_ioctl(int, uint32_t, void*);
extern int sys_msync(void*, size_t, int);
extern int sys_readlink(const char*, char*, size_t);
extern int sys_unlink(const char*);
extern int sys_sigreturn(void*);
extern int sys_sigaltstack(const void*, void*);

extern int sys_poll(void*, unsigned int, int);

static void *native_syscalls[MAX_SYSCALLS] = {
    [SYS_EXIT] = &sys_exit,
    [SYS_FORK] = &sys_fork,
    // ...
    [SYS_POLL] = &sys_poll,
    [SYS_READ] = &sys_read,
    [SYS_WRITE] = &sys_write,
    [SYS_OPEN] = &sys_open,
    [SYS_CLOSE] = &sys_close,
    [SYS_EXECVE] = &sys_execve,
    [10] = (void*)sys_unlink,
    // [11] handled by SYS_EXECVE above
    [13] = (void*)sys_time,
    [119] = (void*)sys_sigreturn,
    [186] = (void*)sys_sigaltstack,
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
    [SYS_IOCTL] = &sys_ioctl,
    [47] = &sys_getgid,
    [48] = &sys_signal,
    [49] = &sys_geteuid,
    [50] = &sys_getegid,
    [51] = &sys_acct,
    [63] = &sys_dup2,
    [106] = &sys_stat,
    [SYS_LSTAT] = &sys_lstat,
    [SYS_FSTAT] = &sys_fstat,
    [122] = &sys_uname,
    [85] = &sys_readlink,
    [141] = &sys_getdents, 
    [SYS_MSYNC] = &sys_msync,
    [162] = &sys_nanosleep,
    [183] = &sys_getcwd,
    [241] = &sys_pmap_stats,
    [455] = &sys_thr_new,
};

static const char *native_names[MAX_SYSCALLS] = {
    [SYS_EXIT] = "exit",
    [SYS_FORK] = "fork",
    [SYS_POLL] = "poll",
    [SYS_READ] = "read",
    [SYS_WRITE] = "write",
    [SYS_OPEN] = "open",
    [SYS_CLOSE] = "close",
    [SYS_EXECVE] = "execve",
    [10] = "unlink",
    [13] = "time",
    [19] = "lseek",
    [20] = "getpid",
    [21] = "mount",
    [22] = "umount",
    [23] = "setuid",
    [24] = "getuid",
    [33] = "access",
    [36] = "sync",
    [37] = "kill",
    [39] = "mkdir",
    [40] = "rmdir",
    [42] = "pipe",
    [46] = "setgid",
    [SYS_IOCTL] = "ioctl",
    [47] = "getgid",
    [48] = "signal",
    [49] = "geteuid",
    [50] = "getegid",
    [51] = "acct",
    [63] = "dup2",
    [85] = "readlink",
    [106] = "stat",
    [SYS_LSTAT] = "lstat",
    [SYS_FSTAT] = "fstat",
    [119] = "sigreturn",
    [122] = "uname",
    [141] = "getdents",
    [SYS_MSYNC] = "msync",
    [162] = "nanosleep",
    [183] = "getcwd",
    [186] = "sigaltstack",
    [241] = "pmap_stats",
    [455] = "thr_new",
};

static struct syscall_fmt native_fmts[MAX_SYSCALLS] = {
    [SYS_EXIT] = { 1, { ARG_INT } },
    [SYS_READ] = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [SYS_WRITE] = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [SYS_OPEN] = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [SYS_CLOSE] = { 1, { ARG_INT } },
    [SYS_EXECVE] = { 3, { ARG_STR, ARG_PTR, ARG_PTR } },
    [10] = { 1, { ARG_STR } },  // unlink
    [19] = { 3, { ARG_INT, ARG_INT, ARG_INT } },  // lseek
    [33] = { 2, { ARG_STR, ARG_HEX } },  // access
    [37] = { 2, { ARG_INT, ARG_INT } },  // kill
    [39] = { 2, { ARG_STR, ARG_HEX } },  // mkdir
    [42] = { 1, { ARG_PTR } },  // pipe
    [SYS_IOCTL] = { 3, { ARG_INT, ARG_HEX, ARG_HEX } },
    [85] = { 3, { ARG_STR, ARG_PTR, ARG_INT } },  // readlink
    [106] = { 2, { ARG_STR, ARG_PTR } },  // stat
    [122] = { 1, { ARG_PTR } },  // uname
    [141] = { 3, { ARG_INT, ARG_PTR, ARG_INT } },  // getdents
    [183] = { 2, { ARG_PTR, ARG_INT } },  // getcwd
};

struct personality personality_native = {
    .name = "substrate",
    .syscall_table = native_syscalls,
    .syscall_names = native_names,
    .syscall_fmts = native_fmts,
    .syscall_count = MAX_SYSCALLS
};