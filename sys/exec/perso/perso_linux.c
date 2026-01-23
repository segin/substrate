#include <exec/perso/personality.h>
#include <stddef.h>
#include <stdint.h>
#include <arch/i386/syscall.h>
#include <sys/syscall_impl.h>
#include <sys/termios.h>
#include <exec/perso/compat.h>
#include <exec/perso/linux/linux_syscalls.h>

/* Native stat structure (from sys/stat.h) */
struct native_stat {
    uint32_t st_dev;
    uint32_t st_ino;      /* Native uses 32-bit but will become 64-bit */
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint32_t st_rdev;
    int64_t  st_size;     /* 64-bit */
    uint32_t st_blksize;
    uint32_t st_pad1;
    int64_t  st_blocks;   /* 64-bit */
    int64_t  st_atime;    /* 64-bit */
    uint32_t st_atime_nsec;
    uint32_t st_pad2;
    int64_t  st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_pad3;
    int64_t  st_ctime;
    uint32_t st_ctime_nsec;
    uint32_t st_pad4;
};

/*
 * Linux i386 oldstat structure (syscall 18) - 36 bytes
 * Used by very old binaries (pre-1993)
 */
struct linux_oldstat {
    uint16_t st_dev;
    uint16_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint16_t st_rdev;
    uint32_t st_size;
    uint32_t st_atime;
    uint32_t st_mtime;
    uint32_t st_ctime;
};

/*
 * Linux i386 stat structure (syscalls 106/107/108) - ~64 bytes
 * Standard 32-bit stat used by most Linux binaries
 */
struct linux_stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint32_t st_rdev;
    uint32_t st_size;     /* 32-bit, limited to 2GB files */
    uint32_t st_blksize;
    uint32_t st_blocks;
    uint32_t st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime;
    uint32_t st_ctime_nsec;
    uint32_t __unused4;
    uint32_t __unused5;
};

/*
 * Linux i386 stat64 structure (syscalls 195/196/197) - ~96 bytes
 * Large File Support stat for >2GB files
 */
struct linux_stat64 {
    uint64_t st_dev;
    uint32_t __pad1;
    uint32_t __st_ino;    /* Old 32-bit ino for compat */
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint32_t __pad2;
    int64_t  st_size;
    uint32_t st_blksize;
    uint64_t st_blocks;
    uint32_t st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime;
    uint32_t st_ctime_nsec;
    uint64_t st_ino;      /* Real 64-bit ino */
};

/* Linux stat translation: native -> linux_stat */
static int linux_sys_stat(const char *path, struct linux_stat *buf) {
    struct native_stat native;
    int ret = sys_stat(path, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->st_ino = native.st_ino;
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);  /* Truncate! */
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->__unused4 = 0;
    buf->__unused5 = 0;
    return 0;
}

static int linux_sys_lstat(const char *path, struct linux_stat *buf) {
    struct native_stat native;
    int ret = sys_lstat(path, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->st_ino = native.st_ino;
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->__unused4 = 0;
    buf->__unused5 = 0;
    return 0;
}

static int linux_sys_fstat(int fd, struct linux_stat *buf) {
    struct native_stat native;
    int ret = sys_fstat(fd, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->st_ino = native.st_ino;
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->st_size = (uint32_t)(native.st_size & 0xFFFFFFFF);
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = (uint32_t)(native.st_blocks & 0xFFFFFFFF);
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->__unused4 = 0;
    buf->__unused5 = 0;
    return 0;
}

/* Linux stat64 translation: native -> linux_stat64 */
static int linux_sys_stat64(const char *path, struct linux_stat64 *buf) {
    struct native_stat native;
    int ret = sys_stat(path, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->__pad1 = 0;
    buf->__st_ino = native.st_ino;  /* 32-bit compat ino */
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->__pad2 = 0;
    buf->st_size = native.st_size;  /* Full 64-bit */
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = native.st_blocks;
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->st_ino = native.st_ino;  /* Full 64-bit ino */
    return 0;
}

static int linux_sys_lstat64(const char *path, struct linux_stat64 *buf) {
    struct native_stat native;
    int ret = sys_lstat(path, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->__pad1 = 0;
    buf->__st_ino = native.st_ino;
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->__pad2 = 0;
    buf->st_size = native.st_size;
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = native.st_blocks;
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->st_ino = native.st_ino;
    return 0;
}

static int linux_sys_fstat64(int fd, struct linux_stat64 *buf) {
    struct native_stat native;
    int ret = sys_fstat(fd, &native);
    if (ret < 0) return ret;
    
    buf->st_dev = native.st_dev;
    buf->__pad1 = 0;
    buf->__st_ino = native.st_ino;
    buf->st_mode = native.st_mode;
    buf->st_nlink = native.st_nlink;
    buf->st_uid = native.st_uid;
    buf->st_gid = native.st_gid;
    buf->st_rdev = native.st_rdev;
    buf->__pad2 = 0;
    buf->st_size = native.st_size;
    buf->st_blksize = native.st_blksize;
    buf->st_blocks = native.st_blocks;
    buf->st_atime = (uint32_t)(native.st_atime & 0xFFFFFFFF);
    buf->st_atime_nsec = native.st_atime_nsec;
    buf->st_mtime = (uint32_t)(native.st_mtime & 0xFFFFFFFF);
    buf->st_mtime_nsec = native.st_mtime_nsec;
    buf->st_ctime = (uint32_t)(native.st_ctime & 0xFFFFFFFF);
    buf->st_ctime_nsec = native.st_ctime_nsec;
    buf->st_ino = native.st_ino;
    return 0;
}

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
/* Linux winsize structure - same as native (4x unsigned short) so we can map directly */
struct linux_winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

/* Linux TTY ioctl handler - 0x5400-0x54FF range */
static int linux_ioctl_tty(int fd, uint32_t request, void *arg) {
    /* Handle TIOCGWINSZ / TIOCSWINSZ explicitly */
    if (request == 0x5413) { // TIOCGWINSZ
        struct winsize native;
        int ret = sys_ioctl(fd, request, &native);
        if (ret == 0 && arg) {
            /* Copy to Linux layout (compatible) */
            struct linux_winsize *lw = (struct linux_winsize *)arg;
            lw->ws_row = native.ws_row;
            lw->ws_col = native.ws_col;
            lw->ws_xpixel = native.ws_xpixel;
            lw->ws_ypixel = native.ws_ypixel;
        }
        return ret;
    }
    
    if (request == 0x5414) { // TIOCSWINSZ
        if (!arg) return -1;
        struct linux_winsize *lw = (struct linux_winsize *)arg;
        struct winsize native;
        native.ws_row = lw->ws_row;
        native.ws_col = lw->ws_col;
        native.ws_xpixel = lw->ws_xpixel;
        native.ws_ypixel = lw->ws_ypixel;
        return sys_ioctl(fd, request, &native);
    }

    /* Termios Translation */
    switch (request) {
        case LINUX_TCGETS: {
            /* Get native termios, translate to Linux format */
            struct termios native;
            extern void *memset(void*, int, size_t);
            memset(&native, 0, sizeof(native));
            
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
            native.c_ispeed = 0;
            native.c_ospeed = 0;
            return sys_ioctl(fd, request, &native);
        }
    }
    
    /* Fallback for other TTY ioctls (TIOCSCTTY, TIOCGPGRP, etc.) */
    /* Most basic TTY ioctls share ABI (int/void arguments) */
    return sys_ioctl(fd, request, arg);
}

/* Linux Block Device ioctl handler - 0x1200 range (stub) */
static int linux_ioctl_blk(int fd, uint32_t request, void *arg) {
    (void)fd; (void)request; (void)arg;
    // kprint("Linux BLK ioctl %x not implemented\n", request);
    return -1; // EINVAL
}

/* Dispatch ioctls based on type/magic */
static int linux_sys_ioctl(int fd, uint32_t request, void *arg) {
    /* 0x54XX = 'T' << 8 (TTY) */
    if ((request & 0xFF00) == 0x5400) {
        return linux_ioctl_tty(fd, request, arg);
    }
    
    /* 0x12XX = Block (BLK*) */
    if ((request & 0xFF00) == 0x1200) {
        return linux_ioctl_blk(fd, request, arg);
    }
    
    /* Other/Unknown - try native pass-through */
    return sys_ioctl(fd, request, arg);
}

static void *linux_syscalls[MAX_SYSCALLS] = {
    [LINUX_SYS_exit]           = &sys_exit,
    [LINUX_SYS_fork]           = &sys_fork,
    [LINUX_SYS_read]           = &sys_read,
    [LINUX_SYS_write]          = &sys_write,
    [LINUX_SYS_open]           = &sys_open,
    [LINUX_SYS_close]          = &sys_close,
    [LINUX_SYS_waitpid]        = &sys_waitpid,
    [LINUX_SYS_link]           = &sys_link,
    [LINUX_SYS_unlink]         = &sys_unlink,
    [LINUX_SYS_execve]         = &sys_execve,
    [LINUX_SYS_chdir]          = &sys_chdir,
    [LINUX_SYS_time]           = &sys_time,
    [LINUX_SYS_mknod]          = &sys_mknod,
    [LINUX_SYS_stat]           = (void*)linux_sys_stat,
    [LINUX_SYS_lseek]          = &sys_lseek,
    [LINUX_SYS_getpid]         = &sys_getpid,
    [LINUX_SYS_mount]          = &sys_mount,
    [LINUX_SYS_umount]         = &sys_umount,
    [LINUX_SYS_setuid]         = &sys_setuid,
    [LINUX_SYS_getuid]         = &sys_getuid,
    [LINUX_SYS_fstat]          = (void*)linux_sys_fstat,
    [LINUX_SYS_access]         = &sys_access,
    [LINUX_SYS_sync]           = &sys_sync,
    [LINUX_SYS_kill]           = &sys_kill,
    [LINUX_SYS_mkdir]          = &sys_mkdir,
    [LINUX_SYS_rmdir]          = &sys_rmdir,
    [LINUX_SYS_pipe]           = &sys_pipe,
    [LINUX_SYS_brk]            = &sys_brk,
    [LINUX_SYS_setgid]         = &sys_setgid,
    [LINUX_SYS_getgid]         = &sys_getgid,
    [LINUX_SYS_signal]         = &sys_signal,
    [LINUX_SYS_geteuid]        = &sys_geteuid,
    [LINUX_SYS_getegid]        = &sys_getegid,
    [LINUX_SYS_acct]           = &sys_acct,
    [LINUX_SYS_ioctl]          = &linux_sys_ioctl,
    [LINUX_SYS_setpgid]        = &sys_setpgid,
    [LINUX_SYS_dup2]           = &sys_dup2,
    [LINUX_SYS_getppid]        = &sys_getppid,
    [LINUX_SYS_getpgrp]        = &sys_getpgrp,
    [LINUX_SYS_setsid]         = &sys_setsid,
    [LINUX_SYS_lstat]          = (void*)linux_sys_lstat,
    [LINUX_SYS_readlink]       = &sys_readlink,
    [LINUX_SYS_mmap]           = &sys_mmap,
    [LINUX_SYS_stat_new]       = (void*)linux_sys_stat,
    [LINUX_SYS_lstat_new]      = (void*)linux_sys_lstat,
    [LINUX_SYS_fstat_new]      = (void*)linux_sys_fstat,
    [LINUX_SYS_clone]          = &sys_clone,
    [LINUX_SYS_uname]          = &sys_uname,
    [LINUX_SYS_getpgid]        = &sys_getpgid,
    [LINUX_SYS_fchdir]         = &sys_fchdir,
    [LINUX_SYS_getdents]       = &sys_getdents,
    [LINUX_SYS_nanosleep]      = &sys_nanosleep,
    [LINUX_SYS_poll]           = &sys_poll,
    [LINUX_SYS_rt_sigaction]   = &sys_sigaction,
    [LINUX_SYS_rt_sigprocmask] = &sys_sigprocmask,
    [LINUX_SYS_getcwd]         = &sys_getcwd,
    [LINUX_SYS_vfork]          = &sys_vfork,
    [LINUX_SYS_mmap2]          = &sys_mmap,
    [LINUX_SYS_stat64]         = (void*)linux_sys_stat64,
    [LINUX_SYS_lstat64]        = (void*)linux_sys_lstat64,
    [LINUX_SYS_fstat64]        = (void*)linux_sys_fstat64,
    [LINUX_SYS_getuid32]       = &sys_getuid,
    [LINUX_SYS_getgid32]       = &sys_getgid,
    [LINUX_SYS_geteuid32]      = &sys_geteuid,
    [LINUX_SYS_getegid32]      = &sys_getegid,
    [LINUX_SYS_setgid32]       = &sys_setgid,
    [LINUX_SYS_getdents64]     = &sys_getdents,
    [LINUX_SYS_fcntl64]        = &sys_fcntl,
    [LINUX_SYS_futex]          = &sys_futex,
    [LINUX_SYS_set_thread_area] = &sys_set_thread_area,
    [LINUX_SYS_exit_group]     = &sys_exit,
};

static const char *linux_names[MAX_SYSCALLS] = {
    [LINUX_SYS_exit]           = "exit",
    [LINUX_SYS_fork]           = "fork",
    [LINUX_SYS_read]           = "read",
    [LINUX_SYS_write]          = "write",
    [LINUX_SYS_open]           = "open",
    [LINUX_SYS_close]          = "close",
    [LINUX_SYS_waitpid]        = "waitpid",
    [LINUX_SYS_link]           = "link",
    [LINUX_SYS_unlink]         = "unlink",
    [LINUX_SYS_execve]         = "execve",
    [LINUX_SYS_chdir]          = "chdir",
    [LINUX_SYS_time]           = "time",
    [LINUX_SYS_mknod]          = "mknod",
    [LINUX_SYS_stat]           = "stat",
    [LINUX_SYS_lseek]          = "lseek",
    [LINUX_SYS_getpid]         = "getpid",
    [LINUX_SYS_mount]          = "mount",
    [LINUX_SYS_umount]         = "umount",
    [LINUX_SYS_setuid]         = "setuid",
    [LINUX_SYS_getuid]         = "getuid",
    [LINUX_SYS_fstat]          = "fstat",
    [LINUX_SYS_access]         = "access",
    [LINUX_SYS_sync]           = "sync",
    [LINUX_SYS_kill]           = "kill",
    [LINUX_SYS_mkdir]          = "mkdir",
    [LINUX_SYS_rmdir]          = "rmdir",
    [LINUX_SYS_dup]            = "dup",
    [LINUX_SYS_pipe]           = "pipe",
    [LINUX_SYS_brk]            = "brk",
    [LINUX_SYS_setgid]         = "setgid",
    [LINUX_SYS_getgid]         = "getgid",
    [LINUX_SYS_signal]         = "signal",
    [LINUX_SYS_geteuid]        = "geteuid",
    [LINUX_SYS_getegid]        = "getegid",
    [LINUX_SYS_acct]           = "acct",
    [LINUX_SYS_ioctl]          = "ioctl",
    [LINUX_SYS_setpgid]        = "setpgid",
    [LINUX_SYS_dup2]           = "dup2",
    [LINUX_SYS_getppid]        = "getppid",
    [LINUX_SYS_getpgrp]        = "getpgrp",
    [LINUX_SYS_setsid]         = "setsid",
    [LINUX_SYS_lstat]          = "lstat",
    [LINUX_SYS_readlink]       = "readlink",
    [LINUX_SYS_mmap]           = "mmap",
    [LINUX_SYS_stat_new]       = "stat",
    [LINUX_SYS_lstat_new]      = "lstat",
    [LINUX_SYS_fstat_new]      = "fstat",
    [LINUX_SYS_clone]          = "clone",
    [LINUX_SYS_uname]          = "uname",
    [LINUX_SYS_getpgid]        = "getpgid",
    [LINUX_SYS_fchdir]         = "fchdir",
    [LINUX_SYS_getdents]       = "getdents",
    [LINUX_SYS_nanosleep]      = "nanosleep",
    [LINUX_SYS_poll]           = "poll",
    [LINUX_SYS_rt_sigaction]   = "rt_sigaction",
    [LINUX_SYS_rt_sigprocmask] = "rt_sigprocmask",
    [LINUX_SYS_getcwd]         = "getcwd",
    [LINUX_SYS_vfork]          = "vfork",
    [LINUX_SYS_mmap2]          = "mmap2",
    [LINUX_SYS_stat64]         = "stat64",
    [LINUX_SYS_lstat64]        = "lstat64",
    [LINUX_SYS_fstat64]        = "fstat64",
    [LINUX_SYS_getuid32]       = "getuid32",
    [LINUX_SYS_getgid32]       = "getgid32",
    [LINUX_SYS_geteuid32]      = "geteuid32",
    [LINUX_SYS_getegid32]      = "getegid32",
    [LINUX_SYS_setgid32]       = "setgid32",
    [LINUX_SYS_getdents64]     = "getdents64",
    [LINUX_SYS_fcntl64]        = "fcntl64",
    [LINUX_SYS_futex]          = "futex",
    [LINUX_SYS_set_thread_area] = "set_thread_area",
    [LINUX_SYS_exit_group]     = "exit_group",
};

static struct syscall_fmt linux_fmts[MAX_SYSCALLS] = {
    [LINUX_SYS_exit]           = { 1, { ARG_INT } }, // exit
    [LINUX_SYS_fork]           = { 0, { 0 } }, // fork
    [LINUX_SYS_read]           = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // read
    [LINUX_SYS_write]          = { 3, { ARG_INT, ARG_STR, ARG_INT } }, // write
    [LINUX_SYS_open]           = { 3, { ARG_STR, ARG_HEX, ARG_HEX } }, // open
    [LINUX_SYS_close]          = { 1, { ARG_INT } }, // close
    [LINUX_SYS_waitpid]        = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // waitpid
    [LINUX_SYS_link]           = { 2, { ARG_STR, ARG_STR } }, // link
    [LINUX_SYS_unlink]         = { 1, { ARG_STR } }, // unlink
    [LINUX_SYS_execve]         = { 3, { ARG_STR, ARG_PTR, ARG_PTR } }, // execve
    [LINUX_SYS_chdir]          = { 1, { ARG_STR } }, // chdir
    [LINUX_SYS_time]           = { 1, { ARG_PTR } }, // time
    [LINUX_SYS_mknod]          = { 3, { ARG_STR, ARG_HEX, ARG_HEX } }, // mknod
    [LINUX_SYS_stat]           = { 2, { ARG_STR, ARG_PTR } }, // stat
    [LINUX_SYS_lseek]          = { 3, { ARG_INT, ARG_INT, ARG_INT } }, // lseek
    [LINUX_SYS_getpid]         = { 0, { 0 } }, // getpid
    [LINUX_SYS_mount]          = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } }, // mount
    [LINUX_SYS_umount]         = { 1, { ARG_STR } }, // umount
    [LINUX_SYS_setuid]         = { 1, { ARG_INT } }, // setuid
    [LINUX_SYS_getuid]         = { 0, { 0 } }, // getuid
    [LINUX_SYS_fstat]          = { 2, { ARG_INT, ARG_PTR } }, // fstat
    [LINUX_SYS_access]         = { 2, { ARG_STR, ARG_HEX } }, // access
    [LINUX_SYS_sync]           = { 0, { 0 } }, // sync
    [LINUX_SYS_kill]           = { 2, { ARG_INT, ARG_INT } }, // kill
    [LINUX_SYS_mkdir]          = { 2, { ARG_STR, ARG_HEX } }, // mkdir
    [LINUX_SYS_rmdir]          = { 1, { ARG_STR } }, // rmdir
    [LINUX_SYS_dup]            = { 1, { ARG_INT } }, // dup
    [LINUX_SYS_pipe]           = { 1, { ARG_PTR } }, // pipe
    [LINUX_SYS_brk]            = { 1, { ARG_HEX } }, // brk
    [LINUX_SYS_setgid]         = { 1, { ARG_INT } }, // setgid
    [LINUX_SYS_getgid]         = { 0, { 0 } }, // getgid
    [LINUX_SYS_signal]         = { 2, { ARG_INT, ARG_PTR } }, // signal
    [LINUX_SYS_geteuid]        = { 0, { 0 } }, // geteuid
    [LINUX_SYS_getegid]        = { 0, { 0 } }, // getegid
    [LINUX_SYS_acct]           = { 1, { ARG_STR } }, // acct
    [LINUX_SYS_ioctl]          = { 3, { ARG_INT, ARG_HEX, ARG_HEX } }, // ioctl
    [LINUX_SYS_setpgid]        = { 2, { ARG_INT, ARG_INT } }, // setpgid
    [LINUX_SYS_dup2]           = { 2, { ARG_INT, ARG_INT } }, // dup2
    [LINUX_SYS_getppid]        = { 0, { 0 } }, // getppid
    [LINUX_SYS_getpgrp]        = { 0, { 0 } }, // getpgrp
    [LINUX_SYS_setsid]         = { 0, { 0 } }, // setsid
    [LINUX_SYS_lstat]          = { 2, { ARG_STR, ARG_PTR } }, // lstat
    [LINUX_SYS_readlink]       = { 3, { ARG_STR, ARG_PTR, ARG_INT } }, // readlink
    [LINUX_SYS_mmap]           = { 6, { ARG_PTR, ARG_INT, ARG_HEX, ARG_HEX, ARG_INT, ARG_HEX } }, // mmap
    [LINUX_SYS_stat_new]       = { 2, { ARG_STR, ARG_PTR } }, // stat
    [LINUX_SYS_lstat_new]      = { 2, { ARG_STR, ARG_PTR } }, // lstat
    [LINUX_SYS_fstat_new]      = { 2, { ARG_INT, ARG_PTR } }, // fstat
    [LINUX_SYS_clone]          = { 5, { ARG_HEX, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR } }, // clone
    [LINUX_SYS_uname]          = { 1, { ARG_PTR } }, // uname
    [LINUX_SYS_getpgid]        = { 1, { ARG_INT } }, // getpgid
    [LINUX_SYS_fchdir]         = { 1, { ARG_INT } }, // fchdir
    [LINUX_SYS_getdents]       = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // getdents
    [LINUX_SYS_nanosleep]      = { 2, { ARG_PTR, ARG_PTR } }, // nanosleep
    [LINUX_SYS_poll]           = { 3, { ARG_PTR, ARG_INT, ARG_INT } }, // poll
    [LINUX_SYS_rt_sigaction]   = { 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } }, // rt_sigaction
    [LINUX_SYS_rt_sigprocmask] = { 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } }, // rt_sigprocmask
    [LINUX_SYS_getcwd]         = { 2, { ARG_PTR, ARG_INT } }, // getcwd
    [LINUX_SYS_vfork]          = { 0, { 0 } }, // vfork
    [LINUX_SYS_mmap2]          = { 6, { ARG_PTR, ARG_INT, ARG_HEX, ARG_HEX, ARG_INT, ARG_HEX } }, // mmap2
    [LINUX_SYS_stat64]         = { 2, { ARG_STR, ARG_PTR } }, // stat64
    [LINUX_SYS_lstat64]        = { 2, { ARG_STR, ARG_PTR } }, // lstat64
    [LINUX_SYS_fstat64]        = { 2, { ARG_INT, ARG_PTR } }, // fstat64
    [LINUX_SYS_getuid32]       = { 0, { 0 } }, // getuid32
    [LINUX_SYS_getgid32]       = { 0, { 0 } }, // getgid32
    [LINUX_SYS_geteuid32]      = { 0, { 0 } }, // geteuid32
    [LINUX_SYS_getegid32]      = { 0, { 0 } }, // getegid32
    [LINUX_SYS_setgid32]       = { 1, { ARG_INT } }, // setgid32
    [LINUX_SYS_getdents64]     = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // getdents64
    [LINUX_SYS_fcntl64]        = { 3, { ARG_INT, ARG_INT, ARG_INT } }, // fcntl64
    [LINUX_SYS_futex]          = { 6, { ARG_PTR, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } }, // futex
    [LINUX_SYS_set_thread_area] = { 1, { ARG_PTR } }, // set_thread_area
    [LINUX_SYS_exit_group]     = { 1, { ARG_INT } }, // exit_group
};

struct personality personality_linux = {
    .name = "Linux",
    .syscall_table = linux_syscalls,
    .syscall_names = linux_names,
    .syscall_fmts = linux_fmts,
    .syscall_count = MAX_SYSCALLS
};
