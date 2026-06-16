#ifndef _FREEBSD_USER_H
#define _FREEBSD_USER_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/*
 * FreeBSD 14.3 kinfo_proc layout for i386 stability.
 * Values and offsets match the standard FreeBSD 14.3 ABI.
 * All symbols prefixed with FREEBSD_ to avoid namespace pollution.
 */

#define FREEBSD_KI_NSPARE_INT   10
#define FREEBSD_KI_NSPARE_INT64 12
#define FREEBSD_KI_NSPARE_PTR   8
#define FREEBSD_KI_NGROUPS      16
#define FREEBSD_COMMLEN         19
#define FREEBSD_TDNAMLEN        16
#define FREEBSD_WMESGLEN        8
#define FREEBSD_LOGNAMELEN      17
#define FREEBSD_LOCKNAMELEN     8
#define FREEBSD_MAXCOMLEN       19
#define FREEBSD_KI_EMULNAMELEN  16
#define FREEBSD_LOGINCLASSLEN   17

typedef int32_t  freebsd_pid_t;
typedef uint32_t freebsd_uid_t;
typedef uint32_t freebsd_gid_t;
typedef uint32_t freebsd_vm_size_t;
typedef uint32_t freebsd_segsz_t;
typedef uint32_t freebsd_fixpt_t;
typedef uint32_t freebsd_sigset_t; // Simplified for layout

struct freebsd_timeval {
    int32_t tv_sec;
    int32_t tv_usec;
};

struct freebsd_timespec {
    int32_t tv_sec;
    int32_t tv_nsec;
};

/*
 * FreeBSD iovec (for readv/writev)
 */
struct freebsd_iovec {
    void   *iov_base;
    size_t  iov_len;
};

/*
 * FreeBSD stat structure.
 *
 * NOTE: FreeBSD 14 uses 64-bit inodes (st_ino).
 * The layout differs significantly between i386 and amd64.
 */

#if defined(__i386__)
struct freebsd_stat {
    uint32_t st_dev;
    uint64_t st_ino;        /* 64-bit inode on FreeBSD 12+ (i386) */
    uint16_t st_mode;
    uint16_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    struct freebsd_timespec st_atim;
    struct freebsd_timespec st_mtim;
    struct freebsd_timespec st_ctim;
    struct freebsd_timespec st_birthtim;
    int64_t  st_size;
    int64_t  st_blocks;
    uint32_t st_blksize;
    uint32_t st_flags;
    uint64_t st_gen;
    int32_t  st_lspare;     /* Padding/Spare */
    int64_t  st_qspare[2];  /* Spares */
};
#elif defined(__x86_64__)
struct freebsd_stat {
    uint32_t st_dev;
    uint32_t st_ino;        /* Legacy/padding? No, typically 32-bit hole or similar? 
                               Actually FreeBSD amd64 uses 'ino_t' which is 64-bit, 
                               but alignment might strictly follow 64-bit boundaries. 
                               Let's use the standard definition. */
    uint64_t st_nlink;
    uint16_t st_mode;
    int16_t  st_padding0;   /* Padding for alignment */
    uint32_t st_uid;
    uint32_t st_gid;
    int32_t  st_padding1;
    uint32_t st_rdev;
    int32_t  st_padding2;
    struct freebsd_timespec st_atim;
    struct freebsd_timespec st_mtim;
    struct freebsd_timespec st_ctim;
    struct freebsd_timespec st_birthtim;
    int64_t  st_size;
    int64_t  st_blocks;
    uint32_t st_blksize;
    uint32_t st_flags;
    uint64_t st_gen;
    int64_t  st_spare[10];
};
#else
#error "Unsupported architecture for FreeBSD personality"
#endif

/*
 * FreeBSD 11 or older stat structure (32-bit inodes)
 */
struct freebsd11_stat {
    uint32_t st_dev;
    uint32_t st_ino;        /* 32-bit inode */
    uint16_t st_mode;
    uint16_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    struct freebsd_timespec st_atim;
    struct freebsd_timespec st_mtim;
    struct freebsd_timespec st_ctim;
    int64_t  st_size;
    int64_t  st_blocks;
    uint32_t st_blksize;
    uint32_t st_flags;
    uint32_t st_gen;
    int32_t  st_lspare;
    struct freebsd_timespec st_birthtim;
};


/*
 * FreeBSD 13+ stat structure (i386 ABI, FreeBSD 14.x layout)
 * Verified against FreeBSD 14.3/i386 /usr/include/sys/stat.h offsets:
 *   dev=0 ino=8 nlink=16 mode=24 bsdflags=26 uid=28 gid=32
 *   rdev=40 atim=48 size=96 blocks=104 total=208
 * dev_t, ino_t, nlink_t are all uint64_t on FreeBSD 14.
 * struct timespec on FreeBSD 14 i386: time_t (int64_t, 8B) + long (4B) = 12B.
 * Used by fstat_freebsd13 (551), lstat_freebsd13, stat_freebsd13 syscalls.
 */
struct freebsd13_timespec {
    int64_t  tv_sec;   /* 8 bytes (time_t is 64-bit even on i386 FreeBSD 14) */
    int32_t  tv_nsec;  /* 4 bytes */
};

#if defined(__i386__) || defined(__x86_64__)
struct freebsd13_stat {
    uint64_t st_dev;          /* offset   0 */
    uint64_t st_ino;          /* offset   8 */
    uint64_t st_nlink;        /* offset  16 */
    uint16_t st_mode;         /* offset  24 */
    int16_t  st_bsdflags;     /* offset  26 */
    uint32_t st_uid;          /* offset  28 */
    uint32_t st_gid;          /* offset  32 */
    int32_t  st_padding1;     /* offset  36 */
    uint64_t st_rdev;         /* offset  40 */
    /* FreeBSD 14.3-RELEASE i386 ships struct stat WITHOUT
     * __STAT_TIME_T_EXT — verified by inspecting libc.so.7's _fstat
     * which only writes 0xD0 = 208 bytes total to the caller's buffer
     * (it advances %edi by 0x88 then `rep stos` 0x12 dwords = 72 more
     * bytes).  HEAD/CURRENT defines the _ext fields, but the released
     * binaries don't see them.  Adding _ext fields here makes our
     * fstat write 224 bytes — overflowing libelf's 208-byte stack
     * buffer in _libelf_open_object and corrupting its stack canary,
     * crashing ldd with "stack overflow detected; terminated" at
     * function epilogue. */
    struct freebsd13_timespec st_atim;     /* offset  48 (12 bytes) */
    struct freebsd13_timespec st_mtim;     /* offset  60 */
    struct freebsd13_timespec st_ctim;     /* offset  72 */
    struct freebsd13_timespec st_birthtim; /* offset  84 */
    int64_t  st_size;         /* offset  96 */
    int64_t  st_blocks;       /* offset 104 */
    int32_t  st_blksize;      /* offset 112 */
    uint32_t st_flags;        /* offset 116 */
    uint64_t st_gen;          /* offset 120 */
    uint64_t st_filerev;      /* offset 128 */
    uint64_t st_spare[9];     /* offset 136 */
    /* total: 208 bytes (0xD0) */
};
#endif

/*
 * FreeBSD 14 dirent (64-bit ino_t/off_t).
 * Header is 24 bytes; record is padded to 8-byte alignment.
 * Used by getdirentries (syscall 554).
 */
struct freebsd_dirent {
    uint64_t  d_fileno;
    int64_t   d_off;
    uint16_t  d_reclen;
    uint8_t   d_type;
    uint8_t   d_pad0;
    uint16_t  d_namlen;
    uint16_t  d_pad1;
    char      d_name[256];
};

/*
 * FreeBSD 11 and earlier dirent (32-bit ino_t).  Used by COMPAT11
 * getdirentries (syscall 196).  Records are NOT 8-byte padded — the
 * old layout aligns each record only to the natural alignment of the
 * fields (record start matches a 4-byte boundary).
 */
struct freebsd11_dirent {
    uint32_t  d_fileno;
    uint16_t  d_reclen;
    uint8_t   d_type;
    uint8_t   d_namlen;
    char      d_name[256];
};

/*
 * FreeBSD 4.x and earlier ostat — used by syscalls 38 (stat),
 * 40 (lstat), 62 (fstat).  Almost no extant binary issues these,
 * but we declare the layout for completeness.  Note tightly-packed
 * mix of 16- and 32-bit fields; FreeBSD's ABI here matches GCC's
 * default layout on i386 (4-byte alignment for uint32, 2-byte for
 * uint16).
 */
struct freebsd_ostat {
    uint16_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint16_t st_rdev;
    int32_t  st_size;
    int32_t  st_atim_sec;
    int32_t  st_atim_nsec;
    int32_t  st_mtim_sec;
    int32_t  st_mtim_nsec;
    int32_t  st_ctim_sec;
    int32_t  st_ctim_nsec;
    int32_t  st_blksize;
    int32_t  st_blocks;
    uint32_t st_flags;
    uint32_t st_gen;
};

struct freebsd_utsname {
    char sysname[256];
    char nodename[256];
    char release[256];
    char version[256];
    char machine[256];
};

struct freebsd4_utsname {
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[32];
    char machine[32];
};



struct freebsd_kinfo_proc {
    int ki_structsize;      /* size of this structure */
    int ki_layout;          /* reserved: layout identifier */
    void *ki_args;          /* address of command arguments */
    void *ki_paddr;         /* address of proc */
    void *ki_addr;          /* kernel virtual addr of u-area */
    void *ki_tracep;        /* pointer to trace file */
    void *ki_textvp;        /* pointer to executable file */
    void *ki_fd;            /* pointer to open file info */
    void *ki_vmspace;       /* pointer to kernel vmspace struct */
    const char *__deprecated_kiflag_names;
    const char *ki_wchan;   /* sleep address */
    freebsd_pid_t ki_pid;         /* Process identifier */
    freebsd_pid_t ki_ppid;        /* parent process id */
    freebsd_pid_t ki_pgid;        /* process group id */
    freebsd_pid_t ki_tpgid;       /* tty process group id */
    freebsd_pid_t ki_sid;         /* Process session ID */
    freebsd_pid_t ki_tsid;        /* Terminal session ID */
    short ki_jobc;          /* job control counter */
    short ki_spare_short1;
    uint32_t ki_tdev_freebsd11;
    freebsd_sigset_t ki_siglist;
    freebsd_sigset_t ki_sigmask;
    freebsd_sigset_t ki_sigignore;
    freebsd_sigset_t ki_sigcatch;
    freebsd_uid_t ki_uid;
    freebsd_uid_t ki_ruid;
    freebsd_uid_t ki_svuid;
    freebsd_gid_t ki_rgid;
    freebsd_gid_t ki_svgid;
    short ki_ngroups;
    short ki_spare_short2;
    freebsd_gid_t ki_groups[FREEBSD_KI_NGROUPS];
    freebsd_vm_size_t ki_size;
    freebsd_segsz_t ki_rssize;
    freebsd_segsz_t ki_swrss;
    freebsd_segsz_t ki_tsize;
    freebsd_segsz_t ki_dsize;
    freebsd_segsz_t ki_ssize;
    uint16_t ki_xstat;
    uint16_t ki_acflag;
    freebsd_fixpt_t ki_pctcpu;
    uint32_t ki_estcpu;
    uint32_t ki_slptime;
    uint32_t ki_swtime;
    uint32_t ki_cow;
    uint64_t ki_runtime;
    struct freebsd_timeval ki_start;
    struct freebsd_timeval ki_childtime;
    long ki_flag;
    long ki_kiflag;
    int ki_traceflag;
    char ki_stat;
    signed char ki_nice;
    char ki_lock;
    char ki_rqindex;
    uint8_t ki_oncpu_old;
    uint8_t ki_lastcpu_old;
    char ki_tdname[FREEBSD_TDNAMLEN+1];
    char ki_wmesg[FREEBSD_WMESGLEN+1];
    char ki_login[FREEBSD_LOGNAMELEN+1];
    char ki_lockname[FREEBSD_LOCKNAMELEN+1];
    char ki_comm[FREEBSD_COMMLEN+1];
    char ki_emul[FREEBSD_KI_EMULNAMELEN+1];
    char ki_loginclass[FREEBSD_LOGINCLASSLEN+1];
    char ki_moretdname[FREEBSD_MAXCOMLEN-FREEBSD_TDNAMLEN+1];
    char ki_sparestrings[100];
    int ki_spareints[FREEBSD_KI_NSPARE_INT];
    int64_t ki_spareint64s[FREEBSD_KI_NSPARE_INT64];
    void *ki_spareptrs[FREEBSD_KI_NSPARE_PTR];
    void *ki_filedesc;
    void *ki_vmentry;
    void *ki_cred;
    void *ki_rlimit;
    void *ki_args_info;
    void *ki_env;
    void *ki_auxv;
    void *ki_groups_info;
    void *ki_cwd;
    void *ki_vm_layout;
    void *ki_kqueue;
    void *ki_rlimit_usage;
    void *ki_ps_strings;
    void *ki_sigfastblk;
    void *ki_siginfo;
    void *ki_sigtramp;
    void *ki_pcb;
    void *ki_kstack;
    void *ki_udata;
    void *ki_tdaddr;
    void *ki_sflag;
    void *ki_tdflags;
    void *ki_cr_flags;
    void *ki_jid;
    void *ki_numthreads;
    void *ki_tid;
    void *ki_pri;
    void *ki_rusage;
    void *ki_rusage_ch;
};

/* FreeBSD i386 sigcontext (legacy/traditional) */
struct freebsd_sigcontext {
    int32_t sc_onstack;
    int32_t sc_mask;
    int32_t sc_esp;
    int32_t sc_ebp;
    int32_t sc_isp;
    int32_t sc_eip;
    int32_t sc_efl;
    int32_t sc_es;
    int32_t sc_ds;
    int32_t sc_cs;
    int32_t sc_ss;
    int32_t sc_edi;
    int32_t sc_esi;
    int32_t sc_ebx;
    int32_t sc_edx;
    int32_t sc_ecx;
    int32_t sc_eax;
    int32_t sc_gs;
    int32_t sc_fs;
    int32_t sc_trapno;
    int32_t sc_err;
};

/* FreeBSD i386 sigframe (traditional) */
struct freebsd_sigframe {
    uint32_t sf_ra;     /* return address -> sigreturn trampoline */
    int32_t  sf_sig;
    int32_t  sf_code;
    uint32_t sf_scp;    /* struct sigcontext * */
    uint32_t sf_handler; /* void (*)(int) */
    struct freebsd_sigcontext sf_sc;
};

/* FreeBSD signal translation functions */
void freebsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
int  freebsd_sys_sigreturn(void *regs);

/* Additional FreeBSD syscall wrappers (freebsd_user.c). */
int     freebsd_sys_zero(void);
int     freebsd_sys_mkfifo(const char *path, int mode);
int64_t freebsd_sys_pread(int fd, void *buf, size_t nbyte,
                          uint32_t off_lo, uint32_t off_hi);
int64_t freebsd_sys_pwrite(int fd, const void *buf, size_t nbyte,
                           uint32_t off_lo, uint32_t off_hi);
int     freebsd_sys_fpathconf(int fd, int name);
int     freebsd_sys_sched_prio_max(int policy);
int     freebsd_sys_sched_prio_min(int policy);
int     freebsd_sys_kenv(int what, const char *name, char *value, int len);
int     freebsd_sys_nmount(const struct freebsd_iovec *iov, unsigned int niov,
                           int flags);

int freebsd_sys_open(const char *path, int flags, int mode);
int freebsd_sys_openat(int dirfd, const char *path, int flags, int mode);
int freebsd_sys_stat(const char *path, struct freebsd_stat *buf);
int freebsd_sys_lstat(const char *path, struct freebsd_stat *buf);
int freebsd_sys_fstat(int fd, struct freebsd_stat *buf);
int freebsd_sys_stat_v11(const char *path, struct freebsd11_stat *buf);
int freebsd_sys_lstat_v11(const char *path, struct freebsd11_stat *buf);
int freebsd_sys_fstat_v11(int fd, struct freebsd11_stat *buf);
int freebsd_sys_fstatat_v13(int dirfd, const char *path, struct freebsd13_stat *buf, int flags);
int freebsd_sys_fstatat(int dirfd, const char *path, struct freebsd13_stat *buf, int flags);
int freebsd_sys_fstatat_v11(int dirfd, const char *path, struct freebsd11_stat *buf, int flags);
ssize_t freebsd_sys_getdirentries(int fd, char *buf, size_t nbytes, int64_t *basep);
ssize_t freebsd_sys_getdirentries_v11(int fd, char *buf, unsigned int nbytes, int32_t *basep);

/* Pre-FreeBSD-5 ostat family.  Almost no extant binary uses these. */
int freebsd_sys_ostat(const char *path, struct freebsd_ostat *buf);
int freebsd_sys_olstat(const char *path, struct freebsd_ostat *buf);
int freebsd_sys_ofstat(int fd, struct freebsd_ostat *buf);

/* FreeBSD at-family wrappers (translate at-flag bits + path copyin). */
int freebsd_sys_faccessat(int dirfd, const char *path, int amode, int flag);
int freebsd_sys_fchmodat(int dirfd, const char *path, int mode, int flag);
int freebsd_sys_fchownat(int dirfd, const char *path, int uid, int gid, int flag);
int freebsd_sys_linkat(int olddir, const char *oldpath, int newdir, const char *newpath, int flag);
int freebsd_sys_mkdirat(int dirfd, const char *path, int mode);
int freebsd_sys_readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz);
int freebsd_sys_renameat(int olddir, const char *oldpath, int newdir, const char *newpath);
int freebsd_sys_symlinkat(const char *target, int newdir, const char *newpath);
int freebsd_sys_unlinkat(int dirfd, const char *path, int flag);

/* chown/chmod family.  Substrate native chown does not follow symlinks
 * but FreeBSD's chown does; lchmod has no native equivalent. */
int freebsd_sys_chown(const char *path, int uid, int gid);
int freebsd_sys_lchmod(const char *path, int mode);

/*
 * FreeBSD 12+/13 struct statfs (the layout used by the statfs/fstatfs/
 * getfsstat _freebsd13 syscalls, 555/556/557).  Width-fixed fields only,
 * so the layout is identical on i386 and amd64.  sizeof == 2344.
 */
#define FREEBSD_MFSNAMELEN  16
#define FREEBSD_MNAMELEN    1024
#define FREEBSD_STATFS_VERSION 0x20140518

struct freebsd_statfs {
    uint32_t f_version;
    uint32_t f_type;
    uint64_t f_flags;
    uint64_t f_bsize;
    uint64_t f_iosize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    int64_t  f_bavail;
    uint64_t f_files;
    int64_t  f_ffree;
    uint64_t f_syncwrites;
    uint64_t f_asyncwrites;
    uint64_t f_syncreads;
    uint64_t f_asyncreads;
    uint64_t f_spare[10];
    uint32_t f_namemax;
    uint32_t f_owner;
    int32_t  f_fsid[2];
    char     f_charspare[80];
    char     f_fstypename[FREEBSD_MFSNAMELEN];
    char     f_mntfromname[FREEBSD_MNAMELEN];
    char     f_mntonname[FREEBSD_MNAMELEN];
};

int freebsd_sys_statfs(const char *path, struct freebsd_statfs *buf);
int freebsd_sys_fstatfs(int fd, struct freebsd_statfs *buf);
int freebsd_sys_getfsstat(struct freebsd_statfs *buf, long bufsize, int mode);

#endif /* _FREEBSD_USER_H */
