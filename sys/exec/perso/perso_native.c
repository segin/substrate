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
    [SYS_POLL] = &sys_poll,
    [SYS_READ] = &sys_read,
    [SYS_WRITE] = &sys_write,
    [SYS_OPEN] = &sys_open,
    [SYS_CLOSE] = &sys_close,
    [SYS_EXECVE] = &sys_execve,
    [SYS_UNLINK] = (void*)sys_unlink,
    [SYS_TIME] = (void*)sys_time,
    [SYS_SIGRETURN] = (void*)sys_sigreturn,
    [SYS_SIGALTSTACK] = (void*)sys_sigaltstack,
    [SYS_LSEEK] = &sys_lseek,
    [SYS_GETPID] = &sys_getpid,
    [SYS_MOUNT] = &sys_mount,
    [SYS_UMOUNT] = &sys_umount,
    [SYS_SETUID] = &sys_setuid,
    [SYS_GETUID] = &sys_getuid,
    [SYS_ACCESS] = &sys_access,
    [SYS_SYNC] = &sys_sync,
    [SYS_KILL] = &sys_kill,
    [SYS_MKDIR] = &sys_mkdir,
    [SYS_RMDIR] = &sys_rmdir,
    [SYS_PIPE] = &sys_pipe,
    [SYS_SETGID] = &sys_setgid,
    [SYS_IOCTL] = &sys_ioctl,
    [SYS_GETGID] = &sys_getgid,
    [SYS_SIGNAL] = &sys_signal,
    [SYS_GETEUID] = &sys_geteuid,
    [SYS_GETEGID] = &sys_getegid,
    [SYS_ACCT] = &sys_acct,
    [SYS_DUP2] = &sys_dup2,
    [SYS_STAT] = &sys_stat,
    [SYS_LSTAT] = &sys_lstat,
    [SYS_FSTAT] = &sys_fstat,
    [SYS_UNAME] = &sys_uname,
    [SYS_READLINK] = &sys_readlink,
    [SYS_GETDENTS] = &sys_getdents, 
    [SYS_MSYNC] = &sys_msync,
    [SYS_NANOSLEEP] = &sys_nanosleep,
    [SYS_GETCWD] = &sys_getcwd,
    [SYS_PMAP_STATS] = &sys_pmap_stats,
    [SYS_THR_NEW] = &sys_thr_new,
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
    [SYS_UNLINK] = "unlink",
    [SYS_TIME] = "time",
    [SYS_LSEEK] = "lseek",
    [SYS_GETPID] = "getpid",
    [SYS_MOUNT] = "mount",
    [SYS_UMOUNT] = "umount",
    [SYS_SETUID] = "setuid",
    [SYS_GETUID] = "getuid",
    [SYS_ACCESS] = "access",
    [SYS_SYNC] = "sync",
    [SYS_KILL] = "kill",
    [SYS_MKDIR] = "mkdir",
    [SYS_RMDIR] = "rmdir",
    [SYS_PIPE] = "pipe",
    [SYS_SETGID] = "setgid",
    [SYS_IOCTL] = "ioctl",
    [SYS_GETGID] = "getgid",
    [SYS_SIGNAL] = "signal",
    [SYS_GETEUID] = "geteuid",
    [SYS_GETEGID] = "getegid",
    [SYS_ACCT] = "acct",
    [SYS_DUP2] = "dup2",
    [SYS_READLINK] = "readlink",
    [SYS_STAT] = "stat",
    [SYS_LSTAT] = "lstat",
    [SYS_FSTAT] = "fstat",
    [SYS_SIGRETURN] = "sigreturn",
    [SYS_UNAME] = "uname",
    [SYS_GETDENTS] = "getdents",
    [SYS_MSYNC] = "msync",
    [SYS_NANOSLEEP] = "nanosleep",
    [SYS_GETCWD] = "getcwd",
    [SYS_SIGALTSTACK] = "sigaltstack",
    [SYS_PMAP_STATS] = "pmap_stats",
    [SYS_THR_NEW] = "thr_new",
};

static struct syscall_fmt native_fmts[MAX_SYSCALLS] = {
    [SYS_EXIT] = { 1, { ARG_INT } },
    [SYS_READ] = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [SYS_WRITE] = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [SYS_OPEN] = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [SYS_CLOSE] = { 1, { ARG_INT } },
    [SYS_EXECVE] = { 3, { ARG_STR, ARG_PTR, ARG_PTR } },
    [SYS_UNLINK] = { 1, { ARG_STR } },  // unlink
    [SYS_LSEEK] = { 3, { ARG_INT, ARG_INT, ARG_INT } },  // lseek
    [SYS_ACCESS] = { 2, { ARG_STR, ARG_HEX } },  // access
    [SYS_KILL] = { 2, { ARG_INT, ARG_INT } },  // kill
    [SYS_MKDIR] = { 2, { ARG_STR, ARG_HEX } },  // mkdir
    [SYS_PIPE] = { 1, { ARG_PTR } },  // pipe
    [SYS_IOCTL] = { 3, { ARG_INT, ARG_HEX, ARG_HEX } },
    [SYS_READLINK] = { 3, { ARG_STR, ARG_PTR, ARG_INT } },  // readlink
    [SYS_STAT] = { 2, { ARG_STR, ARG_PTR } },  // stat
    [SYS_UNAME] = { 1, { ARG_PTR } },  // uname
    [SYS_GETDENTS] = { 3, { ARG_INT, ARG_PTR, ARG_INT } },  // getdents
    [SYS_GETCWD] = { 2, { ARG_PTR, ARG_INT } },  // getcwd
};

struct personality personality_native = {
    .name = "substrate",
    .syscall_table = native_syscalls,
    .syscall_names = native_names,
    .syscall_fmts = native_fmts,
    .syscall_count = MAX_SYSCALLS
};