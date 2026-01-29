/*
 * linux_user.h - Linux i386 userspace ABI structures
 *
 * Contains Linux-specific structure definitions and translation functions
 * for Linux personality emulation.
 */

#ifndef _LINUX_USER_H
#define _LINUX_USER_H



#include <stdint.h>
#include <sys/signal.h>

/* Forward declarations */
struct native_stat;

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

/* Linux winsize structure - same as native (4x unsigned short) so we can map directly */
struct linux_winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

/* Linux stat translation functions: native -> linux_stat */
int linux_sys_stat(const char *path, struct linux_stat *buf);
int linux_sys_lstat(const char *path, struct linux_stat *buf);
int linux_sys_fstat(int fd, struct linux_stat *buf);

/* Linux stat64 translation functions: native -> linux_stat64 */
int linux_sys_stat64(const char *path, struct linux_stat64 *buf);
int linux_sys_lstat64(const char *path, struct linux_stat64 *buf);
int linux_sys_fstat64(int fd, struct linux_stat64 *buf);

/* Linux i386 sigcontext */
struct linux_sigcontext {
    uint16_t gs, __gsh;
    uint16_t fs, __fsh;
    uint16_t es, __esh;
    uint16_t ds, __dsh;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t trapno;
    uint32_t err;
    uint32_t eip;
    uint16_t cs, __csh;
    uint32_t eflags;
    uint32_t esp_at_signal;
    uint16_t ss, __ssh;
    uint32_t fpstate;  /* struct _fpstate * */
    uint32_t oldmask;
    uint32_t cr2;
};

/* Linux sigset_t (64 bits) */
typedef struct {
    uint32_t sig[2];
} linux_sigset_t;

/* Linux stack_t */
struct linux_stack {
    uint32_t ss_sp;
    int32_t  ss_flags;
    uint32_t ss_size;
};

/* Linux ucontext */
struct linux_ucontext {
    uint32_t uc_flags;
    uint32_t uc_link;
    struct linux_stack uc_stack;
    struct linux_sigcontext uc_mcontext;
    linux_sigset_t uc_sigmask;
};

/* Linux sigframe (traditional) */
struct linux_sigframe {
    uint32_t pretcode;
    int32_t  sig;
    struct linux_sigcontext sc;
    uint32_t fpstate_unused;
    uint32_t extramask[1];
    char     retcode[8];
};

/* Linux rt_sigframe (real-time signals) */
struct linux_rt_sigframe {
    uint32_t pretcode;
    int32_t  sig;
    uint32_t pinfo;
    uint32_t puc;
    siginfo_t info; // We'll need to translate siginfo_t
    struct linux_ucontext uc;
    char     retcode[8];
};

/* Linux signal translation functions */
void linux_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
int  linux_sys_sigreturn(void *regs);
int  linux_sys_rt_sigreturn(void *regs);

#endif /* _LINUX_USER_H */
