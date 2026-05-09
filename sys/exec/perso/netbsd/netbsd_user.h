#ifndef _NETBSD_USER_H
#define _NETBSD_USER_H

#include <stdint.h>
#include <stddef.h>

/* NetBSD i386 sigcontext */
struct netbsd_sigcontext {
    int32_t sc_gs;
    int32_t sc_fs;
    int32_t sc_es;
    int32_t sc_ds;
    int32_t sc_edi;
    int32_t sc_esi;
    int32_t sc_ebp;
    int32_t sc_ebx;
    int32_t sc_edx;
    int32_t sc_ecx;
    int32_t sc_eax;
    int32_t sc_trapno;
    int32_t sc_err;
    int32_t sc_eip;
    int32_t sc_cs;
    int32_t sc_eflags;
    int32_t sc_esp;
    int32_t sc_ss;
    int32_t sc_onstack;
    uint32_t sc_mask;
};

/* NetBSD i386 sigframe */
struct netbsd_sigframe {
    int32_t  sf_sig;
    int32_t  sf_code;
    uint32_t sf_scp;    /* struct sigcontext * */
    uint32_t sf_handler;
    struct netbsd_sigcontext sf_sc;
};

/* NetBSD signal translation functions */
void netbsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
int  netbsd_sys_sigreturn(void *regs);


/* NetBSD older stat structure (stat43) */
struct netbsd_stat43 {
    uint16_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint16_t st_rdev;
    int32_t  st_size;
    int32_t  st_atime;
    int32_t  st_spare1;
    int32_t  st_mtime;
    int32_t  st_spare2;
    int32_t  st_ctime;
    int32_t  st_spare3;
    int32_t  st_blksize;
    int32_t  st_blocks;
    uint32_t st_flags;
    uint32_t st_gen;
};

/* NetBSD standard stat structure */
struct netbsd_stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    int32_t  st_atime;
    int32_t  st_atimensec;
    int32_t  st_mtime;
    int32_t  st_mtimensec;
    int32_t  st_ctime;
    int32_t  st_ctimensec;
    int64_t  st_size;
    int64_t  st_blocks;
    uint32_t st_blksize;
    uint32_t st_flags;
    uint32_t st_gen;
    int64_t  st_qspare[2];
};

/* NetBSD 6+ "wide" struct stat — what sys_50_stat / sys_50_fstat /
 * sys_50_lstat (syscalls 439/440/441) write.  Differs from the
 * legacy struct above in: 64-bit ino_t, 64-bit dev_t / rdev_t,
 * 64-bit time_t (12-byte timespec on i386, no _ext padding), and
 * st_birthtim is present.  Layout taken from
 * netbsd/sys/sys/stat.h (POSIX 2008 path with embedded timespec). */
struct netbsd_timespec50 {
    int64_t tv_sec;
    int32_t tv_nsec;   /* `long` on i386 = 4 bytes */
};
struct netbsd_stat50 {
    uint64_t st_dev;          /* offset   0 */
    uint32_t st_mode;         /* offset   8 */
    uint64_t st_ino;          /* offset  12 */
    uint32_t st_nlink;        /* offset  20 */
    uint32_t st_uid;          /* offset  24 */
    uint32_t st_gid;          /* offset  28 */
    uint64_t st_rdev;         /* offset  32 */
    struct netbsd_timespec50 st_atim;     /* offset  40 (12 bytes) */
    struct netbsd_timespec50 st_mtim;     /* offset  52 */
    struct netbsd_timespec50 st_ctim;     /* offset  64 */
    struct netbsd_timespec50 st_birthtim; /* offset  76 */
    int64_t  st_size;         /* offset  88 */
    int64_t  st_blocks;       /* offset  96 */
    int32_t  st_blksize;      /* offset 104 */
    uint32_t st_flags;        /* offset 108 */
    uint32_t st_gen;          /* offset 112 */
    uint32_t st_spare[2];     /* offset 116 — total 124 bytes */
};

/* Translation functions for NetBSD standard stat */
int netbsd_sys_stat(const char *path, struct netbsd_stat *buf);
int netbsd_sys_lstat(const char *path, struct netbsd_stat *buf);
int netbsd_sys_fstat(int fd, struct netbsd_stat *buf);
int netbsd_sys_stat50(const char *path, struct netbsd_stat50 *buf);
int netbsd_sys_lstat50(const char *path, struct netbsd_stat50 *buf);
int netbsd_sys_fstat50(int fd, struct netbsd_stat50 *buf);

/* TLS install — wraps the i386 GSBASE primitive that ld.elf_so calls
 * on its very first syscall after exec to point %gs:0 at the TCB. */
int netbsd_sys_lwp_setprivate(uintptr_t tcb);

/* Translation functions for NetBSD compat stat (stat43) */
int netbsd_sys_compat_stat(const char *path, struct netbsd_stat43 *buf);
int netbsd_sys_compat_lstat(const char *path, struct netbsd_stat43 *buf);
int netbsd_sys_compat_fstat(int fd, struct netbsd_stat43 *buf);

/* chown/chmod family.  NetBSD's at-flag bit values match FreeBSD's
 * (AT_SYMLINK_NOFOLLOW=0x200, AT_REMOVEDIR=0x800 — same upstream
 * POSIX numbering), so flag translation is identical. */
int netbsd_sys_chown(const char *path, int uid, int gid);
int netbsd_sys_lchmod(const char *path, int mode);
int netbsd_sys_fchmodat(int dirfd, const char *path, int mode, int flag);
int netbsd_sys_fchownat(int dirfd, const char *path, int uid, int gid, int flag);

/* Slot 197: modern mmap with `long pad` between fd and pos. */
void *netbsd_sys_mmap(void *addr, size_t len, int prot, int flags,
                      int fd, long pad, uint64_t pos);

#endif /* _NETBSD_USER_H */
