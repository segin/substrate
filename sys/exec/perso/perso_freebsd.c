#include "personality.h"
#include "../../arch/i386/syscall.h"
#include <stddef.h>
#include <sys/syscall_impl.h>

/* FreeBSD-specific */
struct freebsd_utsname;
int sys_freebsd_uname(struct freebsd_utsname*);

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
    [76] = NULL,        // FreeBSD: mincore (not vfork!)
    [66] = &sys_vfork,  // FreeBSD: vfork
    [136] = &sys_mkdir,
    [137] = &sys_rmdir,
    [164] = &sys_freebsd_uname,
    [188] = &sys_stat,   // FreeBSD: stat
    [189] = &sys_fstat,  // FreeBSD: fstat
    [190] = &sys_lstat,  // FreeBSD: lstat
    [209] = &sys_poll,   // FreeBSD: poll
    [326] = &sys_getcwd,
};

/* FreeBSD syscall names */
static const char *freebsd_names[MAX_SYSCALLS] = {
    [1] = "exit",
    [2] = "fork",
    [3] = "read",
    [4] = "write",
    [5] = "open",
    [6] = "close",
    [9] = "link",
    [10] = "unlink",
    [12] = "chdir",
    [13] = "fchdir",
    [17] = "break",
    [20] = "getpid",
    [21] = "mount",
    [22] = "umount",
    [23] = "setuid",
    [24] = "getuid",
    [25] = "geteuid",
    [33] = "access",
    [36] = "sync",
    [37] = "kill",
    [38] = "stat", /* FreeBSD 4.x stat (legacy 32-bit), we map to native 64-bit stat! Mismatch! */
    [39] = "getppid",
    [40] = "lstat", /* FreeBSD 4.x lstat */
    [41] = "dup2",
    [42] = "pipe",
    [43] = "getegid",
    [46] = "setgid",
    [47] = "getgid",
    [54] = "ioctl",
    [59] = "execve",
    [66] = "vfork",
    [136] = "mkdir",
    [137] = "rmdir",
    [164] = "uname",
    [189] = "fstat", /* FreeBSD 4.x fstat */
    [209] = "poll",
    [326] = "getcwd",
    /* Todo: Wrap legacy 32-bit stat calls to convert to 64-bit native struct */
};

/* FreeBSD syscall formats */
static struct syscall_fmt freebsd_fmts[MAX_SYSCALLS] = {
    [1] = { 1, { ARG_INT } }, // exit
    [3] = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // read
    [4] = { 3, { ARG_INT, ARG_STR, ARG_INT } }, // write
    [5] = { 3, { ARG_STR, ARG_HEX, ARG_HEX } }, // open
    [6] = { 1, { ARG_INT } }, // close
    [9] = { 2, { ARG_STR, ARG_STR } }, // link
    [10] = { 1, { ARG_STR } }, // unlink
    [12] = { 1, { ARG_STR } }, // chdir
    [13] = { 1, { ARG_INT } }, // fchdir
    [21] = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } }, // mount
    [22] = { 1, { ARG_STR } }, // umount
    [23] = { 1, { ARG_INT } }, // setuid
    [33] = { 2, { ARG_STR, ARG_HEX } }, // access
    [37] = { 2, { ARG_INT, ARG_INT } }, // kill
    [38] = { 2, { ARG_STR, ARG_PTR } }, // stat (legacy)
    [40] = { 2, { ARG_STR, ARG_PTR } }, // lstat (legacy)
    [41] = { 2, { ARG_INT, ARG_INT } }, // dup2
    [42] = { 1, { ARG_PTR } }, // pipe
    [46] = { 1, { ARG_INT } }, // setgid
    [54] = { 3, { ARG_INT, ARG_HEX, ARG_HEX } }, // ioctl
    [59] = { 3, { ARG_STR, ARG_PTR, ARG_PTR } }, // execve
    [136] = { 2, { ARG_STR, ARG_HEX } }, // mkdir
    [137] = { 1, { ARG_STR } }, // rmdir
    [164] = { 1, { ARG_PTR } }, // uname
    [189] = { 2, { ARG_INT, ARG_PTR } }, // fstat (legacy)
    [209] = { 3, { ARG_PTR, ARG_INT, ARG_INT } }, // poll
    [326] = { 2, { ARG_PTR, ARG_INT } }, // getcwd
};

struct freebsd_utsname {
    char sysname[256];
    char nodename[256];
    char release[256];
    char version[256];
    char machine[256];
};

extern char *strncpy(char *dest, const char *src, size_t n);
extern void *memset(void *s, int c, size_t n);

int sys_freebsd_uname(struct freebsd_utsname *buf) {
    if (!buf) return -1;
    extern char kernel_hostname[65];
    memset(buf, 0, sizeof(struct freebsd_utsname));
    strncpy(buf->sysname, "FreeBSD", 256);
    strncpy(buf->nodename, kernel_hostname, 256);
    strncpy(buf->release, "14.3-RELEASE-p5", 256);
    strncpy(buf->version, "FreeBSD 14.3-RELEASE-p5 GENERIC", 256);
    strncpy(buf->machine, "i386", 256);
    return 0;
}

struct personality personality_freebsd = {
    .name = "FreeBSD",
    .syscall_table = freebsd_syscalls,
    .syscall_names = freebsd_names,
    .syscall_fmts = freebsd_fmts,
    .syscall_count = MAX_SYSCALLS
};