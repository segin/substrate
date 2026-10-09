/*
 * sys/amd64_abi.h - user structures of the native amd64 ABI
 *
 * A native 64-bit process is compiled LP64 and exchanges the structures of
 * docs/specs/abi-amd64.md (section 5) with the kernel.  The kernel's own
 * declarations of those structures keep the i386 layout on both kernels
 * (<sys/abi32.h>), so the 64-bit system-call wrappers in
 * exec/perso/perso_native64.c convert between the two at the boundary;
 * these are the 64-bit side.
 *
 * Structures whose amd64 layout happens to equal the kernel's -- struct
 * timeval, itimerval, pollfd, utsname, termios, tms, sembuf, ipc_perm,
 * the socket addresses, and the <sys/sysinfo.h> records made only of
 * fixed-width fields -- need no twin here.
 *
 * Where section 5 of the specification is silent, the 64-bit side is the
 * natural LP64 layout of the declaration in the userland header, which is
 * what a 64-bit process was compiled with.
 */
#ifndef _SYS_AMD64_ABI_H
#define _SYS_AMD64_ABI_H

#include <stdint.h>
#include <sys/ipc.h>
#include <sys/signal.h>
#include <sys/usbdevfs.h>

#define ABI64_ASSERT_SIZE(type, size) \
    _Static_assert(sizeof(type) == (size), #type " must keep its amd64 size")

struct amd64_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};
ABI64_ASSERT_SIZE(struct amd64_timespec, 16);

/* struct flock, for fcntl(2)'s record locks: the kernel's is struct kflock
 * (<pm/pm.h>), 24 bytes with its off_t fields at 4 and 12. */
struct amd64_flock {
    int16_t l_type;
    int16_t l_whence;
    int32_t __pad0;     /* where LP64 aligns l_start; spelled out, as the
                           i386 kernel compiles this header too */
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
    int32_t __pad1;
};
ABI64_ASSERT_SIZE(struct amd64_flock, 32);

struct amd64_timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};
ABI64_ASSERT_SIZE(struct amd64_timeval, 16);

/* The FreeBSD/amd64 struct stat, with a 32-bit st_mode over FreeBSD's
 * 16-bit st_mode and st_bsdflags. */
struct amd64_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    int32_t  st_padding1;
    uint64_t st_rdev;
    struct amd64_timespec st_atim;
    struct amd64_timespec st_mtim;
    struct amd64_timespec st_ctim;
    struct amd64_timespec st_birthtim;
    int64_t  st_size;
    int64_t  st_blocks;
    int32_t  st_blksize;
    uint32_t st_flags;
    uint64_t st_gen;
    uint64_t st_spare[10];
};
ABI64_ASSERT_SIZE(struct amd64_stat, 224);

/* The fixed head of a getdents record; the NUL-terminated name follows and
 * the record is padded to a multiple of 8 bytes. */
struct amd64_dirent {
    uint64_t d_fileno;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    uint8_t  d_pad0;
    uint16_t d_namlen;
    uint16_t d_pad1;
    char     d_name[];
};
ABI64_ASSERT_SIZE(struct amd64_dirent, 24);

struct amd64_iovec {
    uint64_t iov_base;
    uint64_t iov_len;
};
ABI64_ASSERT_SIZE(struct amd64_iovec, 16);

struct amd64_rusage {
    struct amd64_timeval ru_utime;
    struct amd64_timeval ru_stime;
    int64_t ru_maxrss;
    int64_t ru_ixrss;
    int64_t ru_idrss;
    int64_t ru_isrss;
    int64_t ru_minflt;
    int64_t ru_majflt;
    int64_t ru_nswap;
    int64_t ru_inblock;
    int64_t ru_oublock;
    int64_t ru_msgsnd;
    int64_t ru_msgrcv;
    int64_t ru_nsignals;
    int64_t ru_nvcsw;
    int64_t ru_nivcsw;
};
ABI64_ASSERT_SIZE(struct amd64_rusage, 144);

/* sigset_t: 128 bits.  Signals 1..32 are word 0; the rest is zero. */
struct amd64_sigset {
    uint32_t bits[4];
};

struct amd64_sigaction {
    uint64_t sa_handler;
    int32_t  sa_flags;
    struct amd64_sigset sa_mask;
    uint32_t pad;
};
ABI64_ASSERT_SIZE(struct amd64_sigaction, 32);

struct amd64_stack {
    uint64_t ss_sp;
    uint64_t ss_size;
    int32_t  ss_flags;
    uint32_t pad;
};
ABI64_ASSERT_SIZE(struct amd64_stack, 24);

struct amd64_siginfo {
    int32_t  si_signo;
    int32_t  si_errno;
    int32_t  si_code;
    int32_t  si_pid;
    uint32_t si_uid;
    int32_t  si_status;
    uint64_t si_addr;
    uint64_t si_value;
    uint64_t reason[5];
};
ABI64_ASSERT_SIZE(struct amd64_siginfo, 80);

struct amd64_thr_param {
    uint64_t start_func;
    uint64_t arg;
    uint64_t stack_base;
    uint64_t stack_size;
    uint64_t tls_base;
    uint64_t tls_size;
    uint64_t child_tid;         /* long * in the process */
    uint64_t parent_tid;
    int32_t  flags;
    uint32_t pad;
    uint64_t rtp;               /* accepted and ignored */
    uint64_t spare[3];
};
ABI64_ASSERT_SIZE(struct amd64_thr_param, 104);

struct amd64_itimerspec {
    struct amd64_timespec it_interval;
    struct amd64_timespec it_value;
};
ABI64_ASSERT_SIZE(struct amd64_itimerspec, 32);

/* struct rlimit: rlim_t is an unsigned long, and RLIM_INFINITY all ones. */
struct amd64_rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};
ABI64_ASSERT_SIZE(struct amd64_rlimit, 16);

#define AMD64_RLIM_INFINITY (~(uint64_t)0)

/* <sys/statfs.h>: the Linux-shaped record the userland header declares,
 * not the BSD-shaped struct statfs the kernel keeps (<sys/mount.h>). */
struct amd64_statfs {
    uint32_t f_type;
    uint32_t pad0;
    uint64_t f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    int64_t  f_fsid;
    uint32_t f_namelen;
    uint32_t pad1;
    uint64_t f_frsize;
    uint32_t f_flags;
    uint32_t pad2;
    uint64_t f_spare[4];
};
ABI64_ASSERT_SIZE(struct amd64_statfs, 120);

struct amd64_statvfs {
    uint64_t f_bsize;
    uint64_t f_frsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint64_t f_favail;
    uint64_t f_fsid;
    uint64_t f_flag;
    uint64_t f_namemax;
    char     f_fstypename[16];
    char     f_basetype[16];
};
ABI64_ASSERT_SIZE(struct amd64_statvfs, 120);

struct amd64_mq_attr {
    int64_t mq_flags;
    int64_t mq_maxmsg;
    int64_t mq_msgsize;
    int64_t mq_curmsgs;
};
ABI64_ASSERT_SIZE(struct amd64_mq_attr, 32);

struct amd64_sched_param {
    int32_t sched_priority;
    int32_t sched_ss_low_priority;
    struct amd64_timespec sched_ss_repl_period;
    struct amd64_timespec sched_ss_init_budget;
    int32_t sched_ss_max_repl;
    uint32_t pad;
};
ABI64_ASSERT_SIZE(struct amd64_sched_param, 48);

/* struct ipc_perm has only 32-bit and narrower members, so it is the same
 * 28 bytes in both layouts and the two records below embed it as it is. */
struct amd64_shmid_ds {
    struct ipc_perm shm_perm;
    uint32_t pad;
    uint64_t shm_segsz;
    int64_t  shm_atime;
    int64_t  shm_dtime;
    int64_t  shm_ctime;
    int32_t  shm_cpid;
    int32_t  shm_lpid;
    uint64_t shm_nattch;
};
ABI64_ASSERT_SIZE(struct amd64_shmid_ds, 80);

struct amd64_semid_ds {
    struct ipc_perm sem_perm;
    uint32_t pad;
    int64_t  sem_otime;
    int64_t  sem_ctime;
    uint64_t sem_nsems;
};
ABI64_ASSERT_SIZE(struct amd64_semid_ds, 56);

struct amd64_sysinfo {
    int64_t  uptime;
    uint64_t loads[3];
    uint64_t totalram;
    uint64_t freeram;
    uint64_t sharedram;
    uint64_t bufferram;
    uint64_t totalswap;
    uint64_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint32_t pad1;
    uint64_t totalhigh;
    uint64_t freehigh;
    uint32_t mem_unit;
    uint32_t pad2;
};
ABI64_ASSERT_SIZE(struct amd64_sysinfo, 112);

/* sys_procinfo_t as the userland <sys/sysinfo.h> declares it: the kernel's
 * record without its trailing is_kernel member. */
struct amd64_procinfo {
    int32_t  pid;
    int32_t  ppid;
    int32_t  pgid;
    int32_t  sid;
    uint32_t uid;
    uint32_t gid;
    uint32_t euid;
    uint32_t egid;
    uint8_t  state;
    uint8_t  bitness;
    int16_t  perso_id;
    int16_t  tty;
    uint16_t nice;
    char     name[32];
    uint32_t start_time;
    uint32_t user_time;
    uint32_t sys_time;
    uint32_t vsize;
    uint32_t rss;
};
ABI64_ASSERT_SIZE(struct amd64_procinfo, 92);

/* sys_map_t: start and end are uintptr_t. */
struct amd64_map {
    uint64_t start;
    uint64_t end;
    uint32_t flags;
    char     name[256];
    uint32_t pad;
};
ABI64_ASSERT_SIZE(struct amd64_map, 280);

/* sys_swapinfo_t: the same members, padded to the alignment of its 64-bit
 * ones. */
struct amd64_swapinfo {
    char     path[256];
    uint64_t total;
    uint64_t used;
    int32_t  priority;
    uint32_t pad;
};
ABI64_ASSERT_SIZE(struct amd64_swapinfo, 280);

struct amd64_sigevent {
    int32_t  sigev_notify;
    int32_t  sigev_signo;
    uint64_t sigev_value;
    uint64_t sigev_notify_function;
    uint64_t sigev_notify_attributes;
};
ABI64_ASSERT_SIZE(struct amd64_sigevent, 32);

struct amd64_msghdr {
    uint64_t msg_name;
    uint32_t msg_namelen;
    uint32_t pad0;
    uint64_t msg_iov;
    int32_t  msg_iovlen;
    uint32_t pad1;
    uint64_t msg_control;
    uint32_t msg_controllen;
    int32_t  msg_flags;
};
ABI64_ASSERT_SIZE(struct amd64_msghdr, 48);

/* struct robust_list_head (<sys/futex.h>); the list it heads is a chain of
 * 64-bit next pointers. */
struct amd64_robust_list_head {
    uint64_t list_next;
    int64_t  futex_offset;
    uint64_t list_op_pending;
};
ABI64_ASSERT_SIZE(struct amd64_robust_list_head, 24);

/*
 * The device and socket ioctl records, whose userland declarations
 * (<sys/fb.h>, <sys/input.h>, <net/if.h>, <sys/usbdevfs.h>) spell their
 * fields with long and pointers.  The i386 twins are in <sys/compat32.h>.
 */

/* FBIOGET_FSCREENINFO: struct fb_fix_screeninfo. */
struct amd64_fb_fix_screeninfo {
    char     id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint16_t pad0;
    uint32_t line_length;
    uint32_t pad1;
    uint64_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t reserved[3];
    uint16_t pad2;
};
ABI64_ASSERT_SIZE(struct amd64_fb_fix_screeninfo, 80);

/* FBIOGET_VIDEO_MODES: struct video_mode_query. */
struct amd64_video_mode_query {
    uint32_t count;
    uint32_t pad;
    uint64_t modes;
};
ABI64_ASSERT_SIZE(struct amd64_video_mode_query, 16);

/* A record read from /dev/input/event0: struct input_event. */
struct amd64_input_event {
    int64_t  time_sec;
    int64_t  time_usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
};
ABI64_ASSERT_SIZE(struct amd64_input_event, 24);

/* struct ifreq: the name, then a union that struct ifmap's two longs widen
 * to 24 bytes.  Every member the kernel exchanges lies in the union's first
 * 16 bytes, at the offsets of struct ifreq32. */
struct amd64_ifreq {
    char    ifr_name[16];
    uint8_t ifr_ifru[24];
};
ABI64_ASSERT_SIZE(struct amd64_ifreq, 40);

/* The union keeps its <net/if.h> member names, as struct ifconf32 does, so
 * the ifc_buf and ifc_req accessor macros apply. */
struct amd64_ifconf {
    int32_t  ifc_len;
    uint32_t pad;
    union {
        uint64_t ifcu_buf;
        uint64_t ifcu_req;          /* struct ifreq * in the process */
    } ifc_ifcu;
};
ABI64_ASSERT_SIZE(struct amd64_ifconf, 16);

/* USBDEVFS_CONTROL: struct usbdevfs_ctrltransfer.  The request number
 * encodes the argument's size, so a 64-bit process issues this one. */
struct amd64_usbdevfs_ctrltransfer {
    uint8_t  bRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint32_t timeout;
    uint32_t pad;
    uint64_t data;
};
ABI64_ASSERT_SIZE(struct amd64_usbdevfs_ctrltransfer, 24);

#define USBDEVFS_CONTROL_AMD64 \
    _IOWR('U', 0, struct amd64_usbdevfs_ctrltransfer)

/* The FreeBSD/amd64 machine context. */
struct amd64_mcontext {
    uint64_t mc_onstack;
    uint64_t mc_rdi, mc_rsi, mc_rdx, mc_rcx, mc_r8, mc_r9, mc_rax, mc_rbx,
             mc_rbp, mc_r10, mc_r11, mc_r12, mc_r13, mc_r14, mc_r15;
    uint32_t mc_trapno;
    uint16_t mc_fs, mc_gs;
    uint64_t mc_addr;
    uint32_t mc_flags;
    uint16_t mc_es, mc_ds;
    uint64_t mc_err, mc_rip, mc_cs, mc_rflags, mc_rsp, mc_ss;
    uint64_t mc_len;                /* sizeof(struct amd64_mcontext) */
    uint64_t mc_fpformat;
    uint64_t mc_ownedfp;
    uint8_t  mc_fpstate[512] __attribute__((aligned(16)));   /* FXSAVE image */
    uint64_t mc_fsbase, mc_gsbase;
    uint64_t mc_xfpustate, mc_xfpustate_len;
    uint64_t mc_spare[4];
};
ABI64_ASSERT_SIZE(struct amd64_mcontext, 800);

#define AMD64_MC_FPFMT_NODEV  0x10000   /* no FP state in the context */
#define AMD64_MC_FPFMT_XMM    0x10002   /* mc_fpstate is an FXSAVE image */
#define AMD64_MC_FPOWNED_NONE 0x20000
#define AMD64_MC_FPOWNED_FPU  0x20001

struct amd64_ucontext {
    struct amd64_sigset   uc_sigmask;
    struct amd64_mcontext uc_mcontext;
    uint64_t              uc_link;
    struct amd64_stack    uc_stack;
    int32_t               uc_flags;
    int32_t               spare[4];
} __attribute__((aligned(16)));
ABI64_ASSERT_SIZE(struct amd64_ucontext, 880);

/* What signal delivery pushes; %rsp points at it on entry to the
 * trampoline, which calls *sf_ahu and then sigreturn(&sf_uc). */
struct amd64_sigframe {
    uint64_t              sf_ahu;
    struct amd64_ucontext sf_uc;
    struct amd64_siginfo  sf_si;
};
ABI64_ASSERT_SIZE(struct amd64_sigframe, 976);

/*
 * Is the calling process a native 64-bit one?  The converters shared with
 * 32-bit processes (kern/compat32.c) ask this to pick the user layout.
 * Needs <sys/proc.h>.
 */
#ifdef SUBSTRATE_ARCH_X86_64
#define proc_abi_is_amd64() \
    (current_process != NULL && current_process->bitness == BITNESS_64)
#else
#define proc_abi_is_amd64() 0
#endif

/* Kernel structure -> its amd64 user layout (kern/compat32.c), for the
 * signal frame.  The callers include <sys/signal.h>. */
void stack_to_amd64(const stack_t *k, struct amd64_stack *u);
void siginfo_to_amd64(const siginfo_t *k, struct amd64_siginfo *u);

/*
 * The handler a 64-bit native process gets for system call `num`, or NULL
 * where the i386-layout handler already serves it (every call that takes
 * only scalars, strings, byte buffers and structures listed above as
 * layout-identical).
 */
void *native_amd64_syscall(uint32_t num);

#endif /* _SYS_AMD64_ABI_H */
