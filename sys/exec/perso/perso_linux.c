#include <exec/perso/personality.h>
#include <stddef.h>
#include <sys/termios.h>
#include <arch/i386/syscall.h>
#include <sys/syscall_impl.h>
#include <exec/perso/compat_syscalls.h>

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
    [18] = (void*)linux_sys_stat,  /* oldstat - use Linux wrapper */
    [19] = &sys_lseek,
    [20] = &sys_getpid,
    [21] = &sys_mount,
    [22] = &sys_umount,
    [23] = &sys_setuid,
    [24] = &sys_getuid,
    [28] = (void*)linux_sys_fstat, /* oldfstat */
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
    [84] = (void*)linux_sys_lstat, /* oldlstat */
    [85] = &sys_readlink,
    [90] = &sys_mmap,  // mmap
    [106] = (void*)linux_sys_stat,  /* stat - Linux 32-bit struct */
    [107] = (void*)linux_sys_lstat, /* lstat - Linux 32-bit struct */
    [108] = (void*)linux_sys_fstat, /* fstat - Linux 32-bit struct */
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
    [195] = (void*)linux_sys_stat64,  /* stat64 - Linux LFS struct */
    [196] = (void*)linux_sys_lstat64, /* lstat64 - Linux LFS struct */
    [197] = (void*)linux_sys_fstat64, /* fstat64 - Linux LFS struct */
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
    [2] = { 0, { 0 } }, // fork
    [3] = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // read
    [4] = { 3, { ARG_INT, ARG_STR, ARG_INT } }, // write
    [5] = { 3, { ARG_STR, ARG_HEX, ARG_HEX } }, // open
    [6] = { 1, { ARG_INT } }, // close
    [7] = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // waitpid
    [9] = { 2, { ARG_STR, ARG_STR } }, // link
    [10] = { 1, { ARG_STR } }, // unlink
    [11] = { 3, { ARG_STR, ARG_PTR, ARG_PTR } }, // execve
    [12] = { 1, { ARG_STR } }, // chdir
    [13] = { 1, { ARG_PTR } }, // time
    [14] = { 3, { ARG_STR, ARG_HEX, ARG_HEX } }, // mknod
    [18] = { 2, { ARG_STR, ARG_PTR } }, // stat
    [19] = { 3, { ARG_INT, ARG_INT, ARG_INT } }, // lseek
    [20] = { 0, { 0 } }, // getpid
    [21] = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } }, // mount
    [22] = { 1, { ARG_STR } }, // umount
    [23] = { 1, { ARG_INT } }, // setuid
    [24] = { 0, { 0 } }, // getuid
    [28] = { 2, { ARG_INT, ARG_PTR } }, // fstat
    [33] = { 2, { ARG_STR, ARG_HEX } }, // access
    [36] = { 0, { 0 } }, // sync
    [37] = { 2, { ARG_INT, ARG_INT } }, // kill
    [39] = { 2, { ARG_STR, ARG_HEX } }, // mkdir
    [40] = { 1, { ARG_STR } }, // rmdir
    [41] = { 1, { ARG_INT } }, // dup
    [42] = { 1, { ARG_PTR } }, // pipe
    [45] = { 1, { ARG_HEX } }, // brk
    [46] = { 1, { ARG_INT } }, // setgid
    [47] = { 0, { 0 } }, // getgid
    [48] = { 2, { ARG_INT, ARG_PTR } }, // signal
    [49] = { 0, { 0 } }, // geteuid
    [50] = { 0, { 0 } }, // getegid
    [51] = { 1, { ARG_STR } }, // acct
    [54] = { 3, { ARG_INT, ARG_HEX, ARG_HEX } }, // ioctl
    [57] = { 2, { ARG_INT, ARG_INT } }, // setpgid
    [63] = { 2, { ARG_INT, ARG_INT } }, // dup2
    [64] = { 0, { 0 } }, // getppid
    [65] = { 0, { 0 } }, // getpgrp
    [66] = { 0, { 0 } }, // setsid
    [84] = { 2, { ARG_STR, ARG_PTR } }, // lstat
    [85] = { 3, { ARG_STR, ARG_PTR, ARG_INT } }, // readlink
    [90] = { 6, { ARG_PTR, ARG_INT, ARG_HEX, ARG_HEX, ARG_INT, ARG_HEX } }, // mmap
    [106] = { 2, { ARG_STR, ARG_PTR } }, // stat
    [107] = { 2, { ARG_STR, ARG_PTR } }, // lstat
    [108] = { 2, { ARG_INT, ARG_PTR } }, // fstat
    [120] = { 5, { ARG_HEX, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR } }, // clone
    [122] = { 1, { ARG_PTR } }, // uname
    [132] = { 1, { ARG_INT } }, // getpgid
    [133] = { 1, { ARG_INT } }, // fchdir
    [141] = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // getdents
    [162] = { 2, { ARG_PTR, ARG_PTR } }, // nanosleep
    [168] = { 3, { ARG_PTR, ARG_INT, ARG_INT } }, // poll
    [174] = { 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } }, // rt_sigaction
    [175] = { 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } }, // rt_sigprocmask
    [183] = { 2, { ARG_PTR, ARG_INT } }, // getcwd
    [190] = { 0, { 0 } }, // vfork
    [192] = { 6, { ARG_PTR, ARG_INT, ARG_HEX, ARG_HEX, ARG_INT, ARG_HEX } }, // mmap2
    [195] = { 2, { ARG_STR, ARG_PTR } }, // stat64
    [196] = { 2, { ARG_STR, ARG_PTR } }, // lstat64
    [197] = { 2, { ARG_INT, ARG_PTR } }, // fstat64
    [199] = { 0, { 0 } }, // getuid32
    [200] = { 0, { 0 } }, // getgid32
    [201] = { 0, { 0 } }, // geteuid32
    [202] = { 0, { 0 } }, // getegid32
    [214] = { 1, { ARG_INT } }, // setgid32
    [220] = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // getdents64
    [221] = { 3, { ARG_INT, ARG_INT, ARG_INT } }, // fcntl64
    [240] = { 6, { ARG_PTR, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } }, // futex
    [243] = { 1, { ARG_PTR } }, // set_thread_area
    [252] = { 1, { ARG_INT } }, // exit_group
};

struct personality personality_linux = {
    .name = "Linux",
    .syscall_table = linux_syscalls,
    .syscall_names = linux_names,
    .syscall_fmts = linux_fmts,
    .syscall_count = MAX_SYSCALLS
};
