#ifndef _FREEBSD_USER_H
#define _FREEBSD_USER_H

#include <stdint.h>

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

struct freebsd_utsname {
    char sysname[256];
    char nodename[256];
    char release[256];
    char version[256];
    char machine[256];
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

#endif
