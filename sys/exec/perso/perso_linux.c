#include "personality.h"
#include <stddef.h>
#include <sys/termios.h>
#include "../../arch/i386/syscall.h"

extern int sys_exit(int);
extern int sys_write(int, const char*, int);
extern int sys_read(int, char*, int);
extern int sys_open(const char*, int, int);
extern int sys_close(int);
extern int sys_lseek(int, int, int);
extern int sys_getuid(void);
extern int sys_getgid(void);
extern int sys_getppid(void);
extern int sys_geteuid(void);
extern int sys_getegid(void);
extern int sys_setuid(int);
extern int sys_setgid(int);
extern int sys_sigaction(int, const void *, void *);
extern int sys_sigprocmask(int, const void *, void *);
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
extern int sys_chdir(const char*);
extern int sys_fchdir(int);
extern int sys_brk(uint32_t);
extern int sys_ioctl(int, uint32_t, void*);
extern int sys_lstat(const char*, void*);
extern int sys_fstat(int, void*);
extern void *sys_mmap(void*, size_t, int, int, int, uint64_t);

extern int sys_set_thread_area(void*);
extern int sys_fcntl(int, int, int);
extern int sys_getpgid(int);
extern int sys_setpgid(int, int);
extern int sys_getpgrp(void);
extern int sys_unlink(const char*);
extern int sys_readlink(const char*, char*, size_t);
extern int sys_link(const char*, const char*);
extern int sys_sigprocmask(int, const void*, void*);
extern int sys_setsid(void);
extern int sys_waitpid(int, int*, int);
extern int sys_poll(void*, unsigned int, int);

/* Linux i386 termios structure - different from native Substrate termios */
#define LINUX_NCCS 19
struct linux_termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[LINUX_NCCS];
};

/* Linux ioctl numbers for termios */
#define LINUX_TCGETS    0x5401
#define LINUX_TCSETS    0x5402
#define LINUX_TCSETSW   0x5403
#define LINUX_TCSETSF   0x5404

/* Linux ioctl wrapper - translates termios between native and Linux format */
static int linux_sys_ioctl(int fd, uint32_t request, void *arg) {
    /* For termios ioctls, translate between native and Linux format */
    switch (request) {
        case LINUX_TCGETS: {
            /* Get native termios, translate to Linux format */
            struct termios native;
            int ret = sys_ioctl(fd, request, &native);
            if (ret == 0 && arg) {
                struct linux_termios *lt = (struct linux_termios *)arg;
                lt->c_iflag = native.c_iflag;
                lt->c_oflag = native.c_oflag;
                lt->c_cflag = native.c_cflag;
                lt->c_lflag = native.c_lflag;
                lt->c_line = native.c_line;
                /* Copy only LINUX_NCCS control chars */
                for (int i = 0; i < LINUX_NCCS; i++) {
                    lt->c_cc[i] = native.c_cc[i];
                }
                // Linux 'struct termios' for TCGETS does NOT have speed fields!
                // Speeds are only in termios2 (TCGETS2).
                // Writing them here overflows user stack by 8 bytes.
            }
            return ret;
        }
        case LINUX_TCSETS:
        case LINUX_TCSETSW:
        case LINUX_TCSETSF: {
            /* Translate Linux termios to native, then set */
            if (!arg) return -1;
            struct linux_termios *lt = (struct linux_termios *)arg;
            struct termios native;
            /* Zero the native struct to clear extra c_cc entries */
            extern void *memset(void*, int, size_t);
            memset(&native, 0, sizeof(native));
            native.c_iflag = lt->c_iflag;
            native.c_oflag = lt->c_oflag;
            native.c_cflag = lt->c_cflag;
            native.c_lflag = lt->c_lflag;
            native.c_line = lt->c_line;
            for (int i = 0; i < LINUX_NCCS; i++) {
                native.c_cc[i] = lt->c_cc[i];
            }
            // No speeds in TCSETS struct termios
            native.c_ispeed = 0;
            native.c_ospeed = 0;
            return sys_ioctl(fd, request, &native);
        }
        default:
            /* Pass through other ioctls unchanged */
            return sys_ioctl(fd, request, arg);
    }
}

static void *linux_syscalls[MAX_SYSCALLS] = {
    [1] = &sys_exit,
    [2] = &sys_fork,
    [3] = &sys_read,
    [4] = &sys_write,
    [5] = &sys_open,
    [6] = &sys_close,
    [7] = &sys_waitpid,  // waitpid
    [9] = &sys_link,
    [10] = &sys_unlink,
    [11] = &sys_execve,
    [12] = &sys_chdir,
    [13] = &sys_time,
    [14] = &sys_mknod,
    [18] = &sys_stat,
    [19] = &sys_lseek,
    [20] = &sys_getpid,
    [21] = &sys_mount,
    [22] = &sys_umount,
    [23] = &sys_setuid,
    [24] = &sys_getuid,
    [28] = &sys_fstat,
    [33] = &sys_access,
    [36] = &sys_sync,
    [37] = &sys_kill,
    [39] = &sys_mkdir,
    [40] = &sys_rmdir,
    [42] = &sys_pipe,
    [45] = &sys_brk,
    [46] = &sys_setgid,
    [47] = &sys_getgid,
    [48] = &sys_signal,
    [49] = &sys_geteuid,
    [50] = &sys_getegid,
    [51] = &sys_acct,
    [54] = &linux_sys_ioctl,
    [57] = &sys_setpgid,
    [63] = &sys_dup2,
    [64] = &sys_getppid,
    [65] = &sys_getpgrp,
    [66] = &sys_setsid,
    [84] = &sys_lstat,
    [85] = &sys_readlink,
    [90] = &sys_mmap,  // mmap
    [106] = &sys_stat,
    [107] = &sys_lstat,
    [108] = &sys_fstat,
    [120] = &sys_clone,
    [122] = &sys_uname,
    [132] = &sys_getpgid,
    [133] = &sys_fchdir,
    [141] = &sys_getdents,
    [162] = &sys_nanosleep,
    [168] = &sys_poll, // poll
    [174] = &sys_sigaction, // rt_sigaction
    [175] = &sys_sigprocmask, // rt_sigprocmask
    [183] = &sys_getcwd,
    [190] = &sys_vfork,
    [192] = &sys_mmap,  // mmap2 (same as mmap, offset already in bytes)
    [195] = &sys_stat,   // stat64
    [196] = &sys_lstat,  // lstat64
    [197] = &sys_fstat,  // fstat64
    [199] = &sys_getuid, 
    [200] = &sys_getgid,
    [201] = &sys_geteuid,
    [202] = &sys_getegid,
    [214] = &sys_setgid, // setfsgid32 (map to setgid for now)
    [220] = &sys_getdents, // getdents64
    [221] = &sys_fcntl,  // fcntl64
    [240] = &sys_futex,
    [243] = &sys_set_thread_area,
    [252] = &sys_exit,   // exit_group (map to exit for now)
};

static const char *linux_names[MAX_SYSCALLS] = {
    [1] = "exit",
    [2] = "fork",
    [3] = "read",
    [4] = "write",
    [5] = "open",
    [6] = "close",
    [7] = "waitpid",
    [9] = "link",
    [10] = "unlink",
    [11] = "execve",
    [12] = "chdir",
    [13] = "time",
    [14] = "mknod",
    [18] = "stat",
    [19] = "lseek",
    [20] = "getpid",
    [21] = "mount",
    [22] = "umount",
    [23] = "setuid",
    [24] = "getuid",
    [28] = "fstat",
    [33] = "access",
    [36] = "sync",
    [37] = "kill",
    [39] = "mkdir",
    [40] = "rmdir",
    [41] = "dup",
    [42] = "pipe",
    [45] = "brk",
    [46] = "setgid",
    [47] = "getgid",
    [48] = "signal",
    [49] = "geteuid",
    [50] = "getegid",
    [51] = "acct",
    [54] = "ioctl",
    [57] = "setpgid",
    [63] = "dup2",
    [64] = "getppid",
    [65] = "getpgrp",
    [66] = "setsid",
    [84] = "lstat",
    [85] = "readlink",
    [90] = "mmap",
    [106] = "stat",
    [107] = "lstat",
    [108] = "fstat",
    [120] = "clone",
    [122] = "uname",
    [132] = "getpgid",
    [133] = "fchdir",
    [141] = "getdents",
    [162] = "nanosleep",
    [168] = "poll",
    [174] = "rt_sigaction",
    [175] = "rt_sigprocmask",
    [183] = "getcwd",
    [190] = "vfork",
    [192] = "mmap2",
    [195] = "stat64",
    [196] = "lstat64",
    [197] = "fstat64",
    [199] = "getuid32",
    [200] = "getgid32",
    [201] = "geteuid32",
    [202] = "getegid32",
    [214] = "setgid32",
    [220] = "getdents64",
    [221] = "fcntl64",
    [240] = "futex",
    [243] = "set_thread_area",
    [252] = "exit_group",
};

static struct syscall_fmt linux_fmts[MAX_SYSCALLS] = {
    [1] = { 1, { ARG_INT } }, // exit
    [3] = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // read
    [4] = { 3, { ARG_INT, ARG_STR, ARG_INT } }, // write
    [5] = { 3, { ARG_STR, ARG_HEX, ARG_HEX } }, // open
    [6] = { 1, { ARG_INT } }, // close
    [11] = { 3, { ARG_STR, ARG_PTR, ARG_PTR } }, // execve
    [12] = { 1, { ARG_STR } }, // chdir
    [19] = { 3, { ARG_INT, ARG_INT, ARG_INT } }, // lseek
    [33] = { 2, { ARG_STR, ARG_HEX } }, // access
    [37] = { 2, { ARG_INT, ARG_INT } }, // kill
    [39] = { 2, { ARG_STR, ARG_HEX } }, // mkdir
    [42] = { 1, { ARG_PTR } }, // pipe
    [45] = { 1, { ARG_HEX } }, // brk
    [54] = { 3, { ARG_INT, ARG_HEX, ARG_HEX } }, // ioctl
    [57] = { 2, { ARG_INT, ARG_INT } }, // setpgid
    [63] = { 2, { ARG_INT, ARG_INT } }, // dup2
    [106] = { 2, { ARG_STR, ARG_PTR } }, // stat
    [108] = { 2, { ARG_INT, ARG_PTR } }, // fstat
    [122] = { 1, { ARG_PTR } }, // uname
    [141] = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // getdents
    [174] = { 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } }, // rt_sigaction
    [175] = { 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } }, // rt_sigprocmask
    [183] = { 2, { ARG_PTR, ARG_INT } }, // getcwd
    [192] = { 6, { ARG_PTR, ARG_INT, ARG_HEX, ARG_HEX, ARG_INT, ARG_HEX } }, // mmap2
    [195] = { 2, { ARG_STR, ARG_PTR } }, // stat64
    [197] = { 2, { ARG_INT, ARG_PTR } }, // fstat64
    [220] = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // getdents64
};

struct personality personality_linux = {
    .name = "Linux",
    .syscall_table = linux_syscalls,
    .syscall_names = linux_names,
    .syscall_fmts = linux_fmts,
    .syscall_count = MAX_SYSCALLS
};
