#ifndef _NETBSD_USER_H
#define _NETBSD_USER_H

#include <stdint.h>

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

/* Translation functions for NetBSD standard stat */
int netbsd_sys_stat(const char *path, struct netbsd_stat *buf);
int netbsd_sys_lstat(const char *path, struct netbsd_stat *buf);
int netbsd_sys_fstat(int fd, struct netbsd_stat *buf);

/* Translation functions for NetBSD compat stat (stat43) */
int netbsd_sys_compat_stat(const char *path, struct netbsd_stat43 *buf);
int netbsd_sys_compat_lstat(const char *path, struct netbsd_stat43 *buf);
int netbsd_sys_compat_fstat(int fd, struct netbsd_stat43 *buf);
#endif /* _NETBSD_USER_H */
