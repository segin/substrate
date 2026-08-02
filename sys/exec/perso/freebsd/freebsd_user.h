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

/*
 * FreeBSD i386 TLS TCB (variant II), from sys/i386/include/tls.h:
 *
 *   struct tcb {
 *       struct tcb     *tcb_self;    // offset 0  (%gs:0) -- required by rtld
 *       uintptr_t      *tcb_dtv;     // offset 4  (%gs:4) -- required by rtld
 *       struct pthread *tcb_thread;  // offset 8  (%gs:8) -- libthr's curthread
 *   };
 *
 * %gs points at the TCB (TLS_TP_OFFSET == 0, TLS_DTV_OFFSET == 0).  libthr
 * reads "curthread" from %gs:8 (tcb_thread) and immediately dereferences it
 * (e.g. __pthread_cleanup_push_imp touches curthread->cleanup at +0x188).
 * FreeBSD's csu/rtld seeds this slot from the *previous* gsbase TCB when it
 * allocates the program's TLS (rtld allocate_tls() memcpy's TLS_TCB_SIZE
 * bytes of the old TCB forward, then fixes tcb_self), so the kernel must
 * install an initial TCB at exec whose tcb_thread points at a valid (zeroed)
 * pthread-sized block — otherwise the very first libthr call faults on a
 * near-NULL curthread before libthr's _thr_init runs.
 */
#define FREEBSD_TCB_SELF_OFFSET    0
#define FREEBSD_TCB_DTV_OFFSET     4
#define FREEBSD_TCB_THREAD_OFFSET  8
#define FREEBSD_TCB_SIZE          12   /* sizeof(struct tcb), 3 pointers */

/*
 * Size of the placeholder main-thread struct pthread the kernel zero-fills
 * and points tcb_thread at.  The real FreeBSD 14.3 i386 struct pthread is
 * ~0x228 bytes (libthr copies that many into the initial-thread template) and
 * the highest field libthr touches before its own init runs is curthread->
 * cleanup at +0x188.  Round up generously so any early field access lands in
 * the zeroed block.
 */
#define FREEBSD_INIT_PTHREAD_SIZE  0x400

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

/* FreeBSD i386 struct itimerval / rusage: built from the 8-byte timeval, so
 * they are smaller than the native structs (64-bit time_t).  Used to marshal
 * getitimer/getrusage/wait4 results into the caller's buffer without overrun. */
struct freebsd_itimerval {
    struct freebsd_timeval it_interval;
    struct freebsd_timeval it_value;
};

struct freebsd_rusage {
    struct freebsd_timeval ru_utime;
    struct freebsd_timeval ru_stime;
    int32_t ru_maxrss;
    int32_t ru_ixrss;
    int32_t ru_idrss;
    int32_t ru_isrss;
    int32_t ru_minflt;
    int32_t ru_majflt;
    int32_t ru_nswap;
    int32_t ru_inblock;
    int32_t ru_oublock;
    int32_t ru_msgsnd;
    int32_t ru_msgrcv;
    int32_t ru_nsignals;
    int32_t ru_nvcsw;
    int32_t ru_nivcsw;
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
 * FreeBSD 13+ stat structure (i386 ABI, FreeBSD 14.x layout).
 *
 * On i386 __STAT_TIME_T_EXT is defined (freebsd sys/sys/stat.h:155-157), so
 * the released struct stat interleaves a zeroed 4-byte st_*_ext word BEFORE
 * each `struct timespec` (stat.h:169-184).  i386 __time_t is 32-bit
 * (freebsd sys/x86/include/_types.h:80), so each timespec is 8 bytes
 * (tv_sec int32 + tv_nsec long).  The seconds therefore live at byte offsets
 * 52/64/76/88, and the _ext words at 48/60/72/84 are always zero (the kernel
 * clears them, freebsd sys/kern/vfs_syscalls.c:2488-2493).
 *
 * The struct is 208 bytes (0xD0) either way, but a 64-bit-tv_sec / no-_ext
 * layout puts every seconds field 4 bytes too low: the low 32 bits of the
 * seconds land in the st_*_ext slot and the (zero) high 32 bits land in the
 * real tv_sec, so every timestamp reads as 0 (Thu Jan 1 1970).  dev_t, ino_t,
 * nlink_t are all uint64_t on FreeBSD 14.
 * Used by fstat_freebsd13 (551), lstat_freebsd13, stat_freebsd13 syscalls.
 */
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
    int32_t  st_atim_ext;                  /* offset  48 (zero-filled) */
    struct freebsd_timespec st_atim;       /* offset  52 (8 bytes) */
    int32_t  st_mtim_ext;                  /* offset  60 (zero-filled) */
    struct freebsd_timespec st_mtim;       /* offset  64 */
    int32_t  st_ctim_ext;                  /* offset  72 (zero-filled) */
    struct freebsd_timespec st_ctim;       /* offset  76 */
    int32_t  st_btim_ext;                  /* offset  84 (zero-filled) */
    struct freebsd_timespec st_birthtim;   /* offset  88 */
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

/*
 * Modern FreeBSD i386 signal-delivery ABI.
 *
 * The traditional 3-argument sigframe above (sf_sig/sf_code/sf_scp) is the
 * pre-FreeBSD-4 layout.  Every current FreeBSD/i386 binary -- and, crucially,
 * libthr -- installs its handlers expecting the modern layout:
 *
 *     void handler(int signum, siginfo_t *info, ucontext_t *uc);
 *
 * libthr *always* installs its thr_sighandler wrapper with SA_SIGINFO, so it
 * unconditionally dereferences the kernel-supplied `info` pointer (its
 * handle_signal() reads info->si_code / info->si_addr when re-dispatching a
 * non-SA_SIGINFO user handler such as SDL's SIGINT/SIGTERM catcher).  A NULL
 * `info` faults at addr 0x18 (offsetof si_addr).  The kernel therefore has to
 * build a real siginfo_t + ucontext_t on the user stack.
 *
 * Layouts below match FreeBSD 12+/13/14 i386:
 *   sys/i386/include/ucontext.h   (mcontext_t, sizeof == 640)
 *   sys/sys/_ucontext.h           (ucontext_t)
 *   sys/sys/signal.h              (struct __siginfo, struct sigaction)
 * All __register_t are 32-bit on i386.
 */

/* Real 128-bit FreeBSD sigset_t (the simplified freebsd_sigset_t above is a
 * 32-bit stand-in used by the kinfo_proc layout only). */
struct freebsd_sigset {
    uint32_t __bits[4];
};

struct freebsd_mcontext {
    int32_t mc_onstack;
    int32_t mc_gs;
    int32_t mc_fs;
    int32_t mc_es;
    int32_t mc_ds;
    int32_t mc_edi;
    int32_t mc_esi;
    int32_t mc_ebp;
    int32_t mc_isp;
    int32_t mc_ebx;
    int32_t mc_edx;
    int32_t mc_ecx;
    int32_t mc_eax;
    int32_t mc_trapno;
    int32_t mc_err;
    int32_t mc_eip;
    int32_t mc_cs;
    int32_t mc_eflags;
    int32_t mc_esp;
    int32_t mc_ss;
    int32_t mc_len;            /* sizeof(mcontext_t) == 640 */
    int32_t mc_fpformat;
    int32_t mc_ownedfp;
    int32_t mc_flags;
    int32_t mc_fpstate[128];   /* 512 bytes; 16-aligned in FreeBSD */
    int32_t mc_fsbase;
    int32_t mc_gsbase;
    int32_t mc_xfpustate;
    int32_t mc_xfpustate_len;
    int32_t mc_spare2[4];
};

#define FREEBSD_MC_LEN            640      /* sizeof(struct freebsd_mcontext) */
#define FREEBSD_MC_FPFMT_NODEV    0x10000  /* _MC_FPFMT_NODEV  */
#define FREEBSD_MC_FPOWNED_NONE   0x20000  /* _MC_FPOWNED_NONE */

struct freebsd_ucontext {
    struct freebsd_sigset   uc_sigmask;   /* 16 bytes */
    struct freebsd_mcontext uc_mcontext;  /* offset 16 */
    uint32_t                uc_link;      /* struct __ucontext *   */
    uint32_t                uc_stack_ss_sp;
    uint32_t                uc_stack_ss_size;
    int32_t                 uc_stack_ss_flags;
    int32_t                 uc_flags;
    int32_t                 __spare__[4];
};

struct freebsd_siginfo {
    int32_t  si_signo;
    int32_t  si_errno;
    int32_t  si_code;
    int32_t  si_pid;
    uint32_t si_uid;
    int32_t  si_status;
    uint32_t si_addr;         /* offset 24 (0x18) */
    uint32_t si_value;        /* union sigval */
    int32_t  _reason[8];      /* union _reason (32 bytes) */
};

/* Frame the kernel builds on the user stack.  The handler is entered directly
 * (eip = handler) with the stack laid out as a normal call:
 *   [ sf_ra ][ signum ][ siginfo ][ ucontext ][ addr ][ ahu ] ...
 * When the handler returns it pops sf_ra and lands in the FreeBSD sigreturn
 * trampoline (0xFE000030), which loads EBX = [esp+8] = sf_ucontext (&sf_uc)
 * and issues sigreturn. */
struct freebsd_rt_sigframe {
    uint32_t sf_ra;            /* return address -> sigreturn trampoline */
    int32_t  sf_signum;        /* arg1 */
    uint32_t sf_siginfo;       /* arg2: &sf_si (SA_SIGINFO) or integer code */
    uint32_t sf_ucontext;      /* arg3: &sf_uc */
    uint32_t sf_addr;          /* arg4: fault address (undocumented) */
    uint32_t sf_ahu;           /* handler ptr (kept for layout parity) */
    struct freebsd_ucontext sf_uc;
    struct freebsd_siginfo  sf_si;
};

/* FreeBSD i386 struct sigaction { handler; int sa_flags; sigset_t sa_mask; }.
 * Note the member order differs from substrate-native (handler; mask; flags),
 * so the raw native sys_sigaction misreads flags/mask -- freebsd_sys_sigaction
 * marshals the layout and translates the sa_flags/sa_mask bits. */
struct freebsd_sigaction {
    uint32_t sa_handler;       /* void (*)(int) or void (*)(int,siginfo_t*,void*) */
    int32_t  sa_flags;
    struct freebsd_sigset sa_mask;
};

/* FreeBSD sa_flags bits (sys/sys/signal.h) -- different values from native. */
#define FBSD_SA_ONSTACK    0x0001
#define FBSD_SA_RESTART    0x0002
#define FBSD_SA_RESETHAND  0x0004
#define FBSD_SA_NOCLDSTOP  0x0008
#define FBSD_SA_NODEFER    0x0010
#define FBSD_SA_NOCLDWAIT  0x0020
#define FBSD_SA_SIGINFO    0x0040

/* FreeBSD signal translation functions */
void freebsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
int  freebsd_sys_sigreturn(void *regs);
int  freebsd_sys_sigaction(int sig, const void *act, void *oact);

/* FreeBSD <-> substrate-native signal number / mask translation. */
int      freebsd_to_native_signo(int sig);
int      native_to_freebsd_signo(int sig);
uint32_t freebsd_to_native_sigmask(uint32_t m);
uint32_t native_to_freebsd_sigmask(uint32_t m);

/* FreeBSD signal syscall wrappers that translate the number/mask. */
int freebsd_sys_kill(int pid, int sig);
int freebsd_sys_thr_kill(long tid, int sig);
int freebsd_sys_sigprocmask(int how, const void *set, void *oset);
int freebsd_sys_sigsuspend(const void *mask);
int freebsd_sys_sigpending(void *set);

/* Additional FreeBSD syscall wrappers (freebsd_user.c). */
int     freebsd_sys_zero(void);
int     freebsd_sys_mkfifo(const char *path, int mode);
int64_t freebsd_sys_pread(int fd, void *buf, size_t nbyte,
                          uint32_t off_lo, uint32_t off_hi);
int64_t freebsd_sys_pwrite(int fd, const void *buf, size_t nbyte,
                           uint32_t off_lo, uint32_t off_hi);
int     freebsd_sys_fpathconf(int fd, int name);
int     freebsd_sys_pathconf(const char *path, int name);
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
