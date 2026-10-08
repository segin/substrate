#ifndef _SUNOS_USER_H
#define _SUNOS_USER_H

/*
 * What a SunOS 4.0 program on the Sun386i passes to and gets from the
 * kernel, where it differs from what substrate's own calls use.  SunOS 4
 * is 4.3BSD: struct stat is the 4.3BSD one (struct freebsd_ostat), the
 * signals are numbered as FreeBSD numbers them, and so are the errors up
 * to the end of 4.3BSD's list.
 */

#include <stdint.h>

/*
 * The structures and numbers here are the system's own: <sys/signal.h>,
 * <sys/fcntlcom.h>, <sys/mman.h>, <sys/dirent.h>, <sys/vfs.h>,
 * <sys/termios.h>, <sys/ttycom.h>, <sys/ttold.h> and <sys/filio.h> of the
 * Developer's Toolkit, read with the section 2 manual pages.
 */

/* open(2) flags (<sys/fcntlcom.h>). */
#define SUNOS_O_ACCMODE     0x0003
#define SUNOS_FNDELAY       0x0004      /* O_NDELAY, 4.2BSD's */
#define SUNOS_FAPPEND       0x0008
#define SUNOS_FASYNC        0x0040
#define SUNOS_FCREAT        0x0200
#define SUNOS_FTRUNC        0x0400
#define SUNOS_FEXCL         0x0800
#define SUNOS_FNBIO         0x1000      /* non-blocking, System V's */
#define SUNOS_FSYNC         0x2000

/* The longest path a call is given (MAXPATHLEN of <sys/param.h>), and
 * room for it under the personality's tree. */
#define SUNOS_PATH_MAX      1024
#define SUNOS_TREE_PATH_MAX (SUNOS_PATH_MAX + 64)

/* dup(2): with this bit in the descriptor, the second argument is the
 * descriptor wanted -- dup2() as Version 7 made the call. */
#define SUNOS_DUP_TO        0x40

/* fcntl(2) requests. */
#define SUNOS_F_DUPFD       0
#define SUNOS_F_GETFD       1
#define SUNOS_F_SETFD       2
#define SUNOS_F_GETFL       3
#define SUNOS_F_SETFL       4
#define SUNOS_F_GETOWN      5
#define SUNOS_F_SETOWN      6
#define SUNOS_F_GETLK       7
#define SUNOS_F_SETLK       8
#define SUNOS_F_SETLKW      9

/* mmap(2) flags (<sys/mman.h>).  _MAP_NEW is set by libc's mmap(): with
 * it the call returns the address, without it 0, as the first SunOS 4
 * interface did. */
#define SUNOS_MAP_SHARED    0x00000001
#define SUNOS_MAP_PRIVATE   0x00000002
#define SUNOS_MAP_TYPE      0x0000000f
#define SUNOS_MAP_FIXED     0x00000010
#define SUNOS_MAP_NEW       0x80000000U
/* Where a mapping goes when the program leaves the choice to the system:
 * from here up, clear of the program at the bottom of the address space
 * and the heap above it. */
#define SUNOS_MMAP_BASE     0x40000000U

/* wait4(2): pid 0 is "any child", which POSIX spells -1. */
#define SUNOS_WAIT_ANY      0

/* sigvec(2). */
struct sunos_sigvec {
    uint32_t sv_handler;
    int32_t  sv_mask;
    int32_t  sv_flags;
};
#define SUNOS_SV_ONSTACK    0x0001
#define SUNOS_SV_INTERRUPT  0x0002      /* do not restart the call */
#define SUNOS_SV_RESETHAND  0x0004

struct sunos_sigstack {
    uint32_t ss_sp;
    int32_t  ss_onstack;
};

/*
 * What a handler is given to return through.  libc installs its own
 * _sigtramp for every caught signal; it is entered with the signal's
 * number, a code, the address of this and a fault address on the stack
 * and no return address, calls the program's handler, and issues
 * sigcleanup with the stack pointing at the third of those.  It saves
 * %eax and %ecx itself, and the handler preserves what the C calling
 * convention has it preserve, which leaves %edx to be restored from here.
 */
struct sunos_sigcontext {
    int32_t  sc_onstack;
    int32_t  sc_mask;
    uint32_t sc_sp;
    uint32_t sc_pc;
    uint32_t sc_ps;
    uint32_t sc_eax;
    uint32_t sc_edx;
};

struct sunos_sigframe {
    int32_t  sf_signum;
    int32_t  sf_code;
    uint32_t sf_scp;
    uint32_t sf_addr;
    struct sunos_sigcontext sf_sc;
};

/* The flag bits of %eflags a program may change through sigcleanup. */
#define SUNOS_PS_USER       0x00000dd5U

/* getdents(2) (<sys/dirent.h>). */
struct sunos_dirent {
    int32_t  d_off;
    uint32_t d_fileno;
    uint16_t d_reclen;
    uint16_t d_namlen;
    char     d_name[256];
};
#define SUNOS_DIRENT_HDR    12U

/* statfs(2) (<sys/vfs.h>). */
struct sunos_statfs {
    int32_t f_type;
    int32_t f_bsize;
    int32_t f_blocks;
    int32_t f_bfree;
    int32_t f_bavail;
    int32_t f_files;
    int32_t f_ffree;
    int32_t f_fsid[2];
    int32_t f_spare[7];
};

/* getrlimit(2): 4.3BSD's six resources, CPU to RSS, numbered as
 * substrate numbers them; "no limit" is the largest positive long. */
#define SUNOS_RLIM_NLIMITS  6
#define SUNOS_RLIM_INFINITY 0x7fffffff

/*
 * ioctl(2).  A request is 4.3BSD's: a direction in the top three bits,
 * the size of the argument, a group letter and a number.  The group and
 * number are what identify it.
 */
#define SUNOS_IOC_GROUP(r)  (((r) >> 8) & 0xffU)
#define SUNOS_IOC_NUM(r)    ((r) & 0xffU)
#define SUNOS_IOC_CODE(g, n) ((uint32_t)(((uint32_t)(g) << 8) | (n)))
#define SUNOS_IOC_KEY(r)    ((r) & 0xffffU)

#define SUNOS_TIOCSPGRP     SUNOS_IOC_CODE('t', 118)
#define SUNOS_TIOCGPGRP     SUNOS_IOC_CODE('t', 119)
#define SUNOS_TIOCSWINSZ    SUNOS_IOC_CODE('t', 103)
#define SUNOS_TIOCGWINSZ    SUNOS_IOC_CODE('t', 104)
#define SUNOS_FIOCLEX       SUNOS_IOC_CODE('f', 1)
#define SUNOS_FIONCLEX      SUNOS_IOC_CODE('f', 2)
#define SUNOS_FIONBIO       SUNOS_IOC_CODE('f', 126)
#define SUNOS_FIONREAD      SUNOS_IOC_CODE('f', 127)
#define SUNOS_TCGETA        SUNOS_IOC_CODE('T', 1)
#define SUNOS_TCSETA        SUNOS_IOC_CODE('T', 2)
#define SUNOS_TCSETAW       SUNOS_IOC_CODE('T', 3)
#define SUNOS_TCSETAF       SUNOS_IOC_CODE('T', 4)
#define SUNOS_TCGETS        SUNOS_IOC_CODE('T', 8)
#define SUNOS_TCSETS        SUNOS_IOC_CODE('T', 9)
#define SUNOS_TCSETSW       SUNOS_IOC_CODE('T', 10)
#define SUNOS_TCSETSF       SUNOS_IOC_CODE('T', 11)

/* <sys/termios.h> and <sys/termio.h>.  The flag words are System V's. */
#define SUNOS_NCCS          17
#define SUNOS_NCC           8
struct sunos_termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[SUNOS_NCCS];
};
struct sunos_termio {
    uint16_t c_iflag;
    uint16_t c_oflag;
    uint16_t c_cflag;
    uint16_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[SUNOS_NCC];
};
#define SUNOS_VINTR         0
#define SUNOS_VQUIT         1
#define SUNOS_VERASE        2
#define SUNOS_VKILL         3
#define SUNOS_VEOF          4       /* VMIN when not canonical */
#define SUNOS_VEOL          5       /* VTIME when not canonical */
#define SUNOS_VEOL2         6
#define SUNOS_VSTART        8
#define SUNOS_VSTOP         9
#define SUNOS_VSUSP         10
#define SUNOS_VREPRINT      12
#define SUNOS_VDISCARD      13
#define SUNOS_VWERASE       14
#define SUNOS_VLNEXT        15
/* The flag bits that mean the same here and in substrate's termios. */
#define SUNOS_TERMIOS_FLAGS 0x0000ffffU
/* c_cflag: the speed (<sys/ttydev.h> numbers them), and 9600 baud. */
#define SUNOS_CBAUD         0x0000000fU
#define SUNOS_B9600         13U

/* The calls (sunos_user.c, sunos_sig.c).  Arguments arrive as the 32-bit
 * words the program pushed. */
int64_t sunos_sys_fork(void);
int64_t sunos_sys_vfork(void);
int64_t sunos_sys_getpid(void);
int64_t sunos_sys_getuid(void);
int64_t sunos_sys_getgid(void);
struct freebsd_ostat;
int sunos_sys_open(const char *path, int flags, int mode);
int sunos_sys_creat(const char *path, int mode);
int sunos_sys_link(const char *from, const char *to);
int sunos_sys_rename(const char *from, const char *to);
int sunos_sys_unlink(const char *path);
int sunos_sys_chdir(const char *path);
int sunos_sys_mkdir(const char *path, int mode);
int sunos_sys_rmdir(const char *path);
int sunos_sys_mknod(const char *path, int mode, int dev);
int sunos_sys_chmod(const char *path, int mode);
int sunos_sys_chown(const char *path, int uid, int gid);
int sunos_sys_access(const char *path, int mode);
int sunos_sys_symlink(const char *text, const char *path);
int sunos_sys_readlink(const char *path, char *buf, int size);
int sunos_sys_stat(const char *path, struct freebsd_ostat *buf);
int sunos_sys_lstat(const char *path, struct freebsd_ostat *buf);
int sunos_sys_dup(int fd, int to);
int sunos_sys_wait4(int pid, int *status, int options, void *rusage);
int sunos_sys_brk(void *addr);
int sunos_sys_getpagesize(void);
int64_t sunos_sys_mmap(void *addr, uint32_t len, int prot, uint32_t flags,
                       int fd, int32_t off);
int sunos_sys_getpgrp(int pid);
int sunos_sys_gethostname(char *name, int len);
int sunos_sys_fcntl(int fd, int cmd, int arg);
int sunos_sys_ioctl(int fd, uint32_t request, void *arg);
int sunos_sys_getrlimit(int resource, void *rlp);
int sunos_sys_setrlimit(int resource, const void *rlp);
int sunos_sys_statfs(const char *path, struct sunos_statfs *buf);
int sunos_sys_fstatfs(int fd, struct sunos_statfs *buf);
int sunos_sys_getdents(int fd, char *buf, uint32_t nbytes);
int sunos_sys_utimes(const char *path, const void *tvp);
int sunos_sys_getdomainname(char *name, int len);
int sunos_sys_truncate(const char *path, int32_t length);
int sunos_sys_ftruncate(int fd, int32_t length);
int sunos_sys_killpg(int pgrp, int sig);
int sunos_sys_zero(void);

int sunos_sys_sigvec(int sig, const struct sunos_sigvec *vec,
                     struct sunos_sigvec *ovec);
int sunos_sys_sigblock(int mask);
int sunos_sys_sigsetmask(int mask);
int sunos_sys_sigpause(int mask);
int sunos_sys_sigstack(const struct sunos_sigstack *ss,
                       struct sunos_sigstack *oss);
int sunos_sys_sigcleanup(void);
void sunos_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags,
                   void *regs);

#endif /* _SUNOS_USER_H */
