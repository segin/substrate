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
 * timeval, itimerval, rlimit, pollfd, utsname, termios, the socket
 * addresses -- need no twin here.
 */
#ifndef _SYS_AMD64_ABI_H
#define _SYS_AMD64_ABI_H

#include <stdint.h>
#include <sys/signal.h>

#define ABI64_ASSERT_SIZE(type, size) \
    _Static_assert(sizeof(type) == (size), #type " must keep its amd64 size")

struct amd64_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};
ABI64_ASSERT_SIZE(struct amd64_timespec, 16);

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
