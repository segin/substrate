#include <exec/perso/personality.h>
#include <exec/perso/elks_syscall_table.h>
#include <sys/syscall.h>
#include <sys/errno.h>
#include <kern/console.h>
#include <sys/proc.h>
#include <string.h>

/* Forward declarations for ELKS-specific handlers if needed */
// static void elks_sys_exit(void *regs);
// ...

/* ELKS Syscall Table */
static void *elks_syscall_table[ELKS_SYS_MAX] = {
    [ELKS_SYS_exit]    = (void*)SYS_EXIT,
    [ELKS_SYS_fork]    = (void*)SYS_FORK,
    [ELKS_SYS_read]    = (void*)SYS_READ,
    [ELKS_SYS_write]   = (void*)SYS_WRITE,
    [ELKS_SYS_open]    = (void*)SYS_OPEN,
    [ELKS_SYS_close]   = (void*)SYS_CLOSE,
    [ELKS_SYS_waitpid] = (void*)SYS_WAITPID,
    [ELKS_SYS_creat]   = (void*)SYS_CREAT,
    [ELKS_SYS_link]    = (void*)SYS_LINK,
    [ELKS_SYS_unlink]  = (void*)SYS_UNLINK,
    [ELKS_SYS_execve]  = (void*)SYS_EXECVE,
    [ELKS_SYS_chdir]   = (void*)SYS_CHDIR,
    [ELKS_SYS_time]    = (void*)SYS_TIME,
    [ELKS_SYS_mknod]   = (void*)SYS_MKNOD,
    [ELKS_SYS_chmod]   = (void*)SYS_CHMOD,
    [ELKS_SYS_lchown]  = (void*)SYS_LCHOWN,
    [ELKS_SYS_lseek]   = (void*)SYS_LSEEK,
    [ELKS_SYS_getpid]  = (void*)SYS_GETPID,
    [ELKS_SYS_mount]   = (void*)SYS_MOUNT,
    [ELKS_SYS_umount]  = (void*)SYS_UMOUNT,
    [ELKS_SYS_setuid]  = (void*)SYS_SETUID,
    [ELKS_SYS_getuid]  = (void*)SYS_GETUID,
    [ELKS_SYS_stime]   = (void*)SYS_STIME,
    [ELKS_SYS_access]  = (void*)SYS_ACCESS,
    [ELKS_SYS_kill]    = (void*)SYS_KILL,
    [ELKS_SYS_rename]  = (void*)SYS_RENAME,
    [ELKS_SYS_mkdir]   = (void*)SYS_MKDIR,
    [ELKS_SYS_rmdir]   = (void*)SYS_RMDIR,
    [ELKS_SYS_dup]     = (void*)SYS_DUP,
    [ELKS_SYS_pipe]    = (void*)SYS_PIPE,
    [ELKS_SYS_times]   = (void*)SYS_TIMES,
    [ELKS_SYS_brk]     = (void*)SYS_BRK,
    [ELKS_SYS_setgid]  = (void*)SYS_SETGID,
    [ELKS_SYS_getgid]  = (void*)SYS_GETGID,
    [ELKS_SYS_signal]  = (void*)SYS_SIGNAL,
    [ELKS_SYS_geteuid] = (void*)SYS_GETEUID,
    [ELKS_SYS_getegid] = (void*)SYS_GETEGID,
    [ELKS_SYS_ioctl]   = (void*)SYS_IOCTL,
    [ELKS_SYS_umask]   = (void*)SYS_UMASK,
    [ELKS_SYS_stat]    = (void*)SYS_STAT,
    [ELKS_SYS_fstat]   = (void*)SYS_FSTAT,
};

static const char *elks_syscall_names[ELKS_SYS_MAX] = {
    [ELKS_SYS_exit]    = "exit",
    [ELKS_SYS_fork]    = "fork",
    // ... complete list could be long, adding essentials for trace
    [ELKS_SYS_read]    = "read",
    [ELKS_SYS_write]   = "write",
    [ELKS_SYS_open]    = "open",
    [ELKS_SYS_close]   = "close",
};

struct personality personality_elks = {
    .name = "ELKS",
    .id = PERS_ELKS,
    .syscall_table = elks_syscall_table,
    .syscall_names = elks_syscall_names,
    .syscall_count = ELKS_SYS_MAX,
    .sendsig = NULL, // To be implemented
    .sigreturn = NULL,
    .rt_sigreturn = NULL
};
