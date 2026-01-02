#ifndef _FREEBSD_USER_H
#define _FREEBSD_USER_H

#include <stdint.h>

/*
 * FreeBSD 14.3 kinfo_proc layout for i386 stability.
 * Values and offsets match the standard FreeBSD 14.3 ABI.
 */

#define KI_NSPARE_INT   10
#define KI_NSPARE_INT64 12
#define KI_NSPARE_PTR   8
#define KI_NGROUPS      16
#define COMMLEN         19
#define TDNAMLEN        16
#define WMESGLEN        8
#define LOGNAMELEN      17
#define LOCKNAMELEN     8
#define MAXCOMLEN       19
#define KI_EMULNAMELEN  16
#define LOGINCLASSLEN   17

typedef int32_t  f_pid_t;
typedef uint32_t f_uid_t;
typedef uint32_t f_gid_t;
typedef uint32_t f_vm_size_t;
typedef uint32_t f_segsz_t;
typedef uint32_t f_fixpt_t;
typedef uint32_t f_sigset_t; // Simplified for layout

struct f_timeval {
    int32_t tv_sec;
    int32_t tv_usec;
};

struct kinfo_proc {
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
    f_pid_t ki_pid;         /* Process identifier */
    f_pid_t ki_ppid;        /* parent process id */
    f_pid_t ki_pgid;        /* process group id */
    f_pid_t ki_tpgid;        /* tty process group id */
    f_pid_t ki_sid;         /* Process session ID */
    f_pid_t ki_tsid;        /* Terminal session ID */
    short ki_jobc;          /* job control counter */
    short ki_spare_short1;
    uint32_t ki_tdev_freebsd11;
    f_sigset_t ki_siglist;
    f_sigset_t ki_sigmask;
    f_sigset_t ki_sigignore;
    f_sigset_t ki_sigcatch;
    f_uid_t ki_uid;
    f_uid_t ki_ruid;
    f_uid_t ki_svuid;
    f_gid_t ki_rgid;
    f_gid_t ki_svgid;
    short ki_ngroups;
    short ki_spare_short2;
    f_gid_t ki_groups[KI_NGROUPS];
    f_vm_size_t ki_size;
    f_segsz_t ki_rssize;
    f_segsz_t ki_swrss;
    f_segsz_t ki_tsize;
    f_segsz_t ki_dsize;
    f_segsz_t ki_ssize;
    uint16_t ki_xstat;
    uint16_t ki_acflag;
    f_fixpt_t ki_pctcpu;
    uint32_t ki_estcpu;
    uint32_t ki_slptime;
    uint32_t ki_swtime;
    uint32_t ki_cow;
    uint64_t ki_runtime;
    struct f_timeval ki_start;
    struct f_timeval ki_childtime;
    long ki_flag;
    long ki_kiflag;
    int ki_traceflag;
    char ki_stat;
    signed char ki_nice;
    char ki_lock;
    char ki_rqindex;
    uint8_t ki_oncpu_old;
    uint8_t ki_lastcpu_old;
    char ki_tdname[TDNAMLEN+1];
    char ki_wmesg[WMESGLEN+1];
    char ki_login[LOGNAMELEN+1];
    char ki_lockname[LOCKNAMELEN+1];
    char ki_comm[COMMLEN+1];
    char ki_emul[KI_EMULNAMELEN+1];
    char ki_loginclass[LOGINCLASSLEN+1];
    char ki_moretdname[MAXCOMLEN-TDNAMLEN+1];
    char ki_sparestrings[100];
    int ki_spareints[KI_NSPARE_INT];
    int64_t ki_spareint64s[KI_NSPARE_INT64];
    void *ki_spareptrs[KI_NSPARE_PTR];
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
