/*
 * <kvm.h> — BSD libkvm source-compatibility shim.
 *
 * Substrate's libkvm is a userspace-only port: it does not open
 * /dev/kmem, /dev/mem, or unmount root.  Every entry point is
 * implemented by translating to libsys calls (sys_proc_info,
 * sys_proc_list, sys_proc_maps, sys_proc_cmdline, etc.) and
 * reshaping the result into the BSD-ABI structures the caller
 * expects.
 *
 * Source-level compatibility targets:
 *
 *   FreeBSD  — primary; struct kinfo_proc layout mirrors
 *              FreeBSD's <sys/user.h> field ordering for the
 *              members BSD top(1) / ps(1) / fstat(1) read.
 *   NetBSD   — kvm_getproc2() entry point + matching kinfo_proc2.
 *   OpenBSD  — same kvm_*() prototypes; OpenBSD ki_* fields where
 *              names differ are aliased to the FreeBSD names.
 *
 * What's NOT supported:
 *
 *   - kvm_read()/kvm_write() against arbitrary kernel addresses.
 *     Substrate has no /dev/kmem; only /proc/kcore (planned) will
 *     be eligible, and only for whitelisted ranges.  Today both
 *     calls return -1 and leave a "no kernel memory access" error
 *     in the descriptor.
 *   - kvm_nlist() against a kernel ELF.  When /proc/kallsyms is
 *     available, the implementation parses it; otherwise nlist
 *     entries get n_value = 0 / n_type = 0 (BSD's "not found"
 *     convention).
 *   - opening core files or swap files.  kvm_open() ignores
 *     execfile / corefile / swapfile when they're NULL and refuses
 *     when they're not — the userspace shim only inspects the live
 *     running system.
 */

#ifndef _KVM_H_
#define _KVM_H_

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------
 * Descriptor + lifecycle
 * --------------------------------------------------------------- */
typedef struct __kvm kvm_t;

#define KVM_ERRSTR_SIZE 256

kvm_t *kvm_open(const char *execfile, const char *corefile,
                const char *swapfile, int flags, const char *errstr);
kvm_t *kvm_openfiles(const char *execfile, const char *corefile,
                     const char *swapfile, int flags, char *errbuf);
int    kvm_close(kvm_t *kd);
char  *kvm_geterr(kvm_t *kd);

/* ---------------------------------------------------------------
 * Kernel memory access — both stubbed to error on substrate today.
 * --------------------------------------------------------------- */
ssize_t kvm_read(kvm_t *kd, unsigned long addr, void *buf, size_t len);
ssize_t kvm_write(kvm_t *kd, unsigned long addr, const void *buf, size_t len);

/* ---------------------------------------------------------------
 * Symbol resolution (n_type/n_value follow BSD nlist(3) conventions)
 * --------------------------------------------------------------- */
struct nlist {
    const char *n_name;    /* symbol name (NUL-terminated) */
    unsigned char n_type;  /* 0 = not found */
    char          n_other;
    short         n_desc;
    unsigned long n_value; /* address (or 0 if not resolved) */
};

int kvm_nlist(kvm_t *kd, struct nlist *nl);

/* ---------------------------------------------------------------
 * Process enumeration — `op` is KERN_PROC_*, `arg` is the operand.
 *
 * The returned kinfo_proc array is allocated inside the descriptor
 * and lives until the next kvm_getprocs() call or kvm_close().
 * --------------------------------------------------------------- */
#define KERN_PROC_ALL       0
#define KERN_PROC_PID       1
#define KERN_PROC_PGRP      2
#define KERN_PROC_SESSION   3
#define KERN_PROC_TTY       4
#define KERN_PROC_UID       5
#define KERN_PROC_RUID      6

/*
 * struct kinfo_proc — BSD-compatible subset.  Layout mirrors the
 * FreeBSD field NAMES used by top(1) / ps(1) / fstat(1); the order
 * is NOT byte-identical to FreeBSD's, since substrate consumers
 * always go through the kvm_t API rather than reading the struct
 * out of /proc directly.  ki_structsize lets the consumer detect
 * which layout it's got.
 */
struct kinfo_proc {
    int      ki_structsize;       /* sizeof(struct kinfo_proc) */
    pid_t    ki_pid;
    pid_t    ki_ppid;
    pid_t    ki_pgid;
    pid_t    ki_sid;
    pid_t    ki_tpgid;            /* foreground pgrp of ki_tty */

    uid_t    ki_ruid;             /* real uid */
    uid_t    ki_uid;              /* effective uid */
    gid_t    ki_rgid;
    gid_t    ki_groups[16];
    short    ki_ngroups;

    char     ki_comm[32];         /* short command name */

    /* state — BSD uses single chars: R/S/T/Z/I */
    char     ki_stat;
    char     ki_state[4];         /* string form for ps `state` column */

    int      ki_nice;             /* nice value (-20 .. 19) */
    int      ki_pri;              /* current scheduling priority */
    short    ki_tdev;             /* controlling tty device (-1 == none) */

    /* memory */
    uint32_t ki_size;             /* virtual size in bytes */
    uint32_t ki_rssize;           /* resident size in pages */
    uint32_t ki_tsize;            /* text size in pages */
    uint32_t ki_dsize;            /* data size in pages */
    uint32_t ki_ssize;            /* stack size in pages */

    /* timing — seconds since boot for start, jiffies for cpu */
    uint32_t ki_start;
    uint32_t ki_runtime;          /* user + sys jiffies */
    uint32_t ki_utime;
    uint32_t ki_stime;

    int16_t  ki_perso;            /* substrate-specific personality id */
    uint8_t  ki_bitness;          /* substrate-specific (16/32/64) */
    uint8_t  ki_pad0;
};

struct kinfo_proc *kvm_getprocs(kvm_t *kd, int op, int arg, int *cnt);

/* getproc2: NetBSD-style fixed-size record selection.  We accept
 * but only support elem_size == sizeof(struct kinfo_proc); for any
 * other value we set kd's error and return NULL. */
struct kinfo_proc *kvm_getproc2(kvm_t *kd, int op, int arg,
                                size_t elem_size, int *cnt);

/* ---------------------------------------------------------------
 * Argv / envv extraction (returns NULL-terminated char** owned by kd)
 * --------------------------------------------------------------- */
char **kvm_getargv(kvm_t *kd, const struct kinfo_proc *p, int nchr);
char **kvm_getenvv(kvm_t *kd, const struct kinfo_proc *p, int nchr);

/* ---------------------------------------------------------------
 * VM map (per-process mappings) + open file table.
 * --------------------------------------------------------------- */
struct kinfo_vmentry {
    int       kve_structsize;
    uintptr_t kve_start;
    uintptr_t kve_end;
    int       kve_protection;       /* PROT_READ|PROT_WRITE|PROT_EXEC */
    int       kve_flags;            /* substrate sys_map_t::flags */
    char      kve_path[256];
};

struct kinfo_file {
    int    kf_structsize;
    int    kf_fd;
    int    kf_type;                 /* KF_TYPE_* */
    off_t  kf_offset;
    int    kf_flags;
    char   kf_path[256];
};

#define KF_TYPE_NONE     0
#define KF_TYPE_VNODE    1
#define KF_TYPE_SOCKET   2
#define KF_TYPE_PIPE     3
#define KF_TYPE_FIFO     4
#define KF_TYPE_PTS      5
#define KF_TYPE_UNKNOWN  255

struct kinfo_vmentry *kvm_getvmmap(kvm_t *kd, const struct kinfo_proc *p, int *cnt);
struct kinfo_file    *kvm_getfiles(kvm_t *kd, int op, int arg, int *cnt);

#ifdef __cplusplus
}
#endif

#endif /* _KVM_H_ */
