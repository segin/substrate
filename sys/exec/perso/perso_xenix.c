/*
 * perso_xenix.c - the Xenix personality.
 *
 * One personality for every x.out program (exec/formats/xout.c), in two
 * halves, because Xenix has two system-call ABIs: the 80386 one, first in
 * this file, and the 8086/80286 one after it.  At the end is the single
 * struct personality, whose trap and signal hooks hand each process to the
 * half its bitness names.
 *
 * The 32-bit half's entry is here: a program makes a system call through
 * the SysV/386 call gate,
 *
 *     mov  $nr, %eax
 *     lcall $0x0007, $0          ; 9A 00 00 00 00 07 00
 *
 * and returns from a signal handler through the one at selector 0x000f.
 * Substrate installs neither gate, so the lcall faults (#NP/#GP); the fault
 * is trapped, the lcall decoded, and the call emulated.  The handlers
 * themselves follow the 16-bit half, whose structures and translations
 * they share.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <machine/gdt.h>
#include <machine/idt.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>
#include <exec/formats/xout.h>
#include <exec/perso/personality.h>
#include <exec/perso/svr3/svr3_syscalls.h>
#include <exec/perso/xenix/sysv386.h>
#include <exec/perso/xenix/xenix286_syscalls.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <pm/pm.h>
#include <sys/compiler.h>
#include <sys/copy.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/exec.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/kern_syscalls.h>
#include <sys/ldt.h>
#include <sys/proc.h>
#include <sys/poll.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/syscall_impl.h>
#include <sys/sysinfo.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

#define XENIX_EFLAGS_CF   0x00000001U   /* carry flag */
#define XENIX_LCALL_LEN   7U            /* 9A off32 sel16 */
#define XENIX_GATE_SEL    0x0007U       /* SysV/386 syscall gate selector */
#define XENIX_SIGRET_SEL  0x000FU       /* ... and the signal-return gate */

static void *xenix_syscalls[MAX_SYSCALLS] = {
    [SVR3_SYS_exit]     = &sys_exit,
    [SVR3_SYS_fork]     = &sys_fork,
    [SVR3_SYS_read]     = &sys_read,
    [SVR3_SYS_write]    = &sys_write,
    [SVR3_SYS_open]     = &sys_open,
    [SVR3_SYS_close]    = &sys_close,
    [SVR3_SYS_wait]     = &sys_waitpid,
    [SVR3_SYS_link]     = &sys_link,
    [SVR3_SYS_unlink]   = &sys_unlink,
    [SVR3_SYS_chdir]    = &sys_chdir,
    [SVR3_SYS_time]     = &sys_time,
    [SVR3_SYS_mknod]    = &sys_mknod,
    [SVR3_SYS_chmod]    = &sys_chmod,
    [SVR3_SYS_chown]    = &sys_lchown,
    [SVR3_SYS_stat]     = &sys_stat,
    [SVR3_SYS_lseek]    = &sys_lseek,
    [SVR3_SYS_getpid]   = &sys_getpid,
    [SVR3_SYS_mount]    = &sys_mount,
    [SVR3_SYS_umount]   = &sys_umount,
    [SVR3_SYS_setuid]   = &sys_setuid,
    [SVR3_SYS_getuid]   = &sys_getuid,
    [SVR3_SYS_access]   = &sys_access,
    [SVR3_SYS_nice]     = &sys_nice,
    [SVR3_SYS_sync]     = &sys_sync,
    [SVR3_SYS_kill]     = &sys_kill,
    [SVR3_SYS_dup]      = &sys_dup,
    [SVR3_SYS_pipe]     = &sys_pipe,
    [SVR3_SYS_setgid]   = &sys_setgid,
    [SVR3_SYS_getgid]   = &sys_getgid,
    [SVR3_SYS_acct]     = &sys_acct,
    [SVR3_SYS_ioctl]    = &sys_ioctl,
    [SVR3_SYS_execve]   = &sys_execve,
    [SVR3_SYS_chroot]   = &sys_chroot,
    [SVR3_SYS_fcntl]    = &sys_fcntl,
    [SVR3_SYS_ulimit]   = &sys_ulimit,
    [SVR3_SYS_rmdir]    = &sys_rmdir,
    [SVR3_SYS_mkdir]    = &sys_mkdir,
    [SVR3_SYS_getdents] = &sys_getdents,
    [SVR3_SYS_getcwd]   = &sys_getcwd,
};

static const char *xenix_names[MAX_SYSCALLS] = {
    [SVR3_SYS_exit]     = "exit",
    [SVR3_SYS_fork]     = "fork",
    [SVR3_SYS_read]     = "read",
    [SVR3_SYS_write]    = "write",
    [SVR3_SYS_open]     = "open",
    [SVR3_SYS_close]    = "close",
    [SVR3_SYS_wait]     = "wait",
    [SVR3_SYS_link]     = "link",
    [SVR3_SYS_unlink]   = "unlink",
    [SVR3_SYS_chdir]    = "chdir",
    [SVR3_SYS_time]     = "time",
    [SVR3_SYS_mknod]    = "mknod",
    [SVR3_SYS_chmod]    = "chmod",
    [SVR3_SYS_chown]    = "chown",
    [SVR3_SYS_stat]     = "stat",
    [SVR3_SYS_lseek]    = "lseek",
    [SVR3_SYS_getpid]   = "getpid",
    [SVR3_SYS_mount]    = "mount",
    [SVR3_SYS_umount]   = "umount",
    [SVR3_SYS_setuid]   = "setuid",
    [SVR3_SYS_getuid]   = "getuid",
    [SVR3_SYS_access]   = "access",
    [SVR3_SYS_nice]     = "nice",
    [SVR3_SYS_sync]     = "sync",
    [SVR3_SYS_kill]     = "kill",
    [SVR3_SYS_dup]      = "dup",
    [SVR3_SYS_pipe]     = "pipe",
    [SVR3_SYS_setgid]   = "setgid",
    [SVR3_SYS_getgid]   = "getgid",
    [SVR3_SYS_acct]     = "acct",
    [SVR3_SYS_ioctl]    = "ioctl",
    [SVR3_SYS_execve]   = "exece",
    [SVR3_SYS_chroot]   = "chroot",
    [SVR3_SYS_fcntl]    = "fcntl",
    [SVR3_SYS_ulimit]   = "ulimit",
    [SVR3_SYS_rmdir]    = "rmdir",
    [SVR3_SYS_mkdir]    = "mkdir",
    [SVR3_SYS_getdents] = "getdents",
    [SVR3_SYS_getcwd]   = "getcwd",
};

static int xenix_trace_enabled(void) {
    return cmdline_debug_enabled("perso:xenix:syscall");
}

/* Translate a segmented selector:offset to a linear address using the current
 * process LDT, honouring 32-bit offsets (the shared ldt.h helper truncates to
 * 16 bits, which is fine for ELKS but not for a multi-megabyte Xenix text). */
static int xenix_seg_to_linear(uint16_t selector, uint32_t offset,
                               uintptr_t *linear_out) {
    const gdt_entry_t *ldt;
    const gdt_entry_t *entry;
    unsigned int index;

    if (!current_process || !linear_out) {
        return -EINVAL;
    }
    if ((selector & 0x4U) == 0) {
        /* A GDT selector: the flat user segments of an ELF program. */
        *linear_out = (uintptr_t)offset;
        return 0;
    }
    if (!current_process->ldt) {
        return -EINVAL;
    }
    index = (unsigned int)(selector >> 3);
    if (index >= (unsigned int)current_process->ldt_entry_count) {
        return -EINVAL;
    }
    ldt = (const gdt_entry_t *)current_process->ldt;
    entry = &ldt[index];
    if ((entry->access & 0x80U) == 0 || (entry->access & 0x10U) == 0) {
        return -EINVAL;   /* not present, or not a code/data segment */
    }
    if (offset > ldt_entry_limit(entry)) {
        return -EFAULT;
    }
    *linear_out = (uintptr_t)ldt_entry_base(entry) + (uintptr_t)offset;
    return 0;
}

/* Decode the faulting instruction: a system call, a signal return, or
 * neither. */
int sysv386_lcall_kind(registers_t *regs) {
    uintptr_t linear_ip;
    uint8_t insn[XENIX_LCALL_LEN];

    if (xenix_seg_to_linear((uint16_t)regs->cs, regs->eip, &linear_ip) != 0) {
        return 0;
    }
    if (linear_ip >= USER32_VA_END - sizeof(insn)) {
        return 0;
    }
    /* Read directly: the instruction has just been fetched, so it is
     * mapped, and copyin() refuses the first page of the address space --
     * which is where a small program's stubs are, text starting at 0. */
    memcpy(insn, (const void *)linear_ip, sizeof(insn));
    /* 0x9A = far CALL ptr16:32; the trailing selector word names the gate:
     * 7 for a system call, 0xf for the return from a signal handler. */
    if (insn[0] != 0x9AU) {
        return 0;
    }
    if (((uint16_t)insn[5] | ((uint16_t)insn[6] << 8)) == XENIX_SIGRET_SEL) {
        return 2;
    }
    if (((uint16_t)insn[5] | ((uint16_t)insn[6] << 8)) != XENIX_GATE_SEL) {
        return 0;
    }
    return 1;
}

static int x386_syscall(registers_t *regs);

static int xenix386_handle_trap(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;

    if (!regs || !current_process ||
        current_process->perso_id != PERS_XENIX ||
        !current_process->ldt) {
        return 0;
    }
    /* Only segment/protection faults can come from an lcall to an absent gate. */
    if (regs->int_no != 11 && regs->int_no != 13) {
        return 0;
    }
    switch (sysv386_lcall_kind(regs)) {
    case SYSV386_SYSCALL:
        return x386_syscall(regs);
    case SYSV386_SIGRETURN:
        return sysv386_sigreturn(regs, 0);
    default:
        return 0;
    }
}

/* =====================================================================
 * The 16-bit half: 8086 and 80286 programs.
 *
 * Xenix/286 is a 16-bit protected-mode System V.2 derivative.  Its binaries
 * are segmented x.out images (see exec/formats/xout.c) whose system-call
 * ABI has nothing in common with the 386 Xenix one:
 *
 *     _read:  mov  $3,%ax          ; AL = call, AH = sub-function
 *             jmp  __syscall
 *     __syscall:                   ; crt0, at text offset 2
 *             push %bp
 *             mov  %sp,%bp
 *             push %di
 *             push %si
 *             mov  0xa(%bp),%di    ; arg4     (0xc in middle model, where
 *             mov  0x8(%bp),%si    ; arg3      the return address is far)
 *             mov  0x6(%bp),%cx    ; arg2
 *             mov  0x4(%bp),%bx    ; arg1
 *             call 0x2             ; -> `int $5`
 *             jb   cerror
 *             ret
 *
 * So: call number in AX, up to four word arguments in BX, CX, SI and DI, and
 * the trap is `int $5`.  On return the carry flag means failure with the
 * (positive) errno in AX; on success AX holds the result and BX the high
 * half of a 32-bit one -- Xenix's stubs do `mov %bx,%dx` to assemble the
 * DX:AX that Microsoft C returns longs in.  Calls that yield two values
 * (getpid/getppid, getuid/geteuid, pipe, wait) use the same AX/BX pair.
 *
 * Substrate leaves IDT vector 5 at DPL 0, so the `int $5` faults with #GP
 * before it ever reaches the #BR handler.  We catch that here, decode the
 * `CD 05` at CS:IP to be sure, and emulate.
 *
 * Everything Xenix added to System V hangs off call 40 with the
 * sub-function in AH -- brkctl(2) most importantly, which is how a small or
 * middle model program grows its heap.  Call 57 is multiplexed the same way
 * (utssys: AH=0 uname, AH=2 ustat).
 *
 * The numbers and argument shapes were read out of the SCO Xenix 286
 * Development System's own libc; see xenix/xenix286_syscalls.h.
 */

#define X286_EFLAGS_CF     0x00000001U
#define X286_INT5_LEN      2U            /* CD 05 */
#define X286_SYSCALL_VEC   0x05U

/* Guard band kept between the top of the heap and the lowest stack offset
 * the kernel will hand out to crt0's __stkgrow. */
#define X286_STACK_GUARD   0x0400U

/* Longest pathname we will pull out of a 16-bit segment. */
#define X286_PATH_MAX      1024U

/* ------------------------------------------------------------------ */
/* Tracing                                                            */
/* ------------------------------------------------------------------ */

static int x286_trace_enabled(void) {
    return cmdline_debug_enabled("perso:x286:syscall");
}

/*
 * The normal trace prints a call once it returns, which says nothing about a
 * call that never does.  `debug=perso:x286:entry` prints on the way in too,
 * so a program parked in a blocking read or wait can be identified -- the
 * generic syscall tracer cannot see these, since they arrive as traps.
 */
static int x286_entry_trace_enabled(void) {
    return cmdline_debug_enabled("perso:x286:entry");
}

static const char *x286_call_name(unsigned int nr);
static const char *x286_xenix_name(unsigned int sub);

/* ------------------------------------------------------------------ */
/* The decoded trap frame handed to each call implementation           */
/* ------------------------------------------------------------------ */

struct x286_frame {
    registers_t *regs;
    uint16_t nr;    /* AL: the System V call number */
    uint16_t sub;   /* AH: sub-function, for the multiplexed calls */
    uint16_t bx;    /* arg1 */
    uint16_t cx;    /* arg2 */
    uint16_t si;    /* arg3 */
    uint16_t di;    /* arg4 */
    uint16_t ds;
    uint16_t es;
    uint16_t ss;
};

/* ------------------------------------------------------------------ */
/* Segment plumbing                                                    */
/* ------------------------------------------------------------------ */

static gdt_entry_t *x286_ldt_entry(uint16_t selector) {
    unsigned int index;
    gdt_entry_t *ldt;

    if (!current_process || !current_process->ldt) {
        return NULL;
    }
    if ((selector & 0x04U) == 0U) {
        return NULL;   /* GDT selector: not one of ours */
    }
    index = (unsigned int)(selector >> 3);
    if (index >= (unsigned int)current_process->ldt_entry_count) {
        return NULL;
    }
    ldt = (gdt_entry_t *)current_process->ldt;
    if ((ldt[index].access & 0x80U) == 0U ||
        (ldt[index].access & 0x10U) == 0U) {
        return NULL;   /* not present, or a system descriptor */
    }
    return &ldt[index];
}

/*
 * Translate selector:offset to a linear address, checking that the whole
 * [offset, offset+size) span stays inside the segment.  A 16-bit segment is
 * at most 64 KiB, so the arithmetic cannot wrap a uint32_t.
 */
static int x286_seg_span(uint16_t selector, uint32_t offset, size_t size,
                         uintptr_t *linear_out) {
    const gdt_entry_t *entry = x286_ldt_entry(selector);
    uint32_t limit;

    if (!entry) {
        return -EFAULT;
    }
    limit = ldt_entry_limit(entry);
    offset &= 0xFFFFU;
    if (offset > limit) {
        return -EFAULT;
    }
    if (size > 0 && (uint32_t)(size - 1U) > limit - offset) {
        return -EFAULT;
    }
    if (linear_out) {
        *linear_out = (uintptr_t)ldt_entry_base(entry) + (uintptr_t)offset;
    }
    return 0;
}

/* Near pointer: an offset in the program's current DS. */
static int x286_ds_span(const struct x286_frame *f, uint32_t offset,
                        size_t size, uintptr_t *linear_out) {
    return x286_seg_span(f->ds, offset, size, linear_out);
}

/*
 * Copy a NUL-terminated string out of DS into freshly allocated kernel
 * memory.  Callers hand the result to the kern_* entry points, which want
 * kernel pointers; plain data buffers are passed through as user linear
 * addresses instead and never copied.
 */
static int x286_ds_string(const struct x286_frame *f, uint32_t offset,
                          char **out) {
    const gdt_entry_t *entry = x286_ldt_entry(f->ds);
    uintptr_t base;
    uint32_t limit, avail;
    const char *src;
    size_t len = 0;
    char *copy;

    *out = NULL;
    if (!entry) {
        return -EFAULT;
    }
    limit = ldt_entry_limit(entry);
    offset &= 0xFFFFU;
    if (offset > limit) {
        return -EFAULT;
    }
    base = (uintptr_t)ldt_entry_base(entry);
    src = (const char *)(base + offset);
    avail = limit - offset + 1U;
    if (avail > X286_PATH_MAX) {
        avail = X286_PATH_MAX;
    }
    while (len < avail && src[len] != '\0') {
        len++;
    }
    if (len == avail) {
        return -ENAMETOOLONG;   /* runs off the segment, or absurdly long */
    }

    copy = kmalloc(len + 1U);
    if (!copy) {
        return -ENOMEM;
    }
    memcpy(copy, src, len + 1U);
    *out = copy;
    return 0;
}

static void x286_free_string(char *s) {
    if (s) {
        kfree(s, strlen(s) + 1U);
    }
}

/* DGROUP is whatever SS names: Xenix small and middle model programs run
 * with SS == DS == the first data segment for their whole life. */
static uint16_t x286_dgroup_sel(const struct x286_frame *f) {
    return f->ss;
}

/* ------------------------------------------------------------------ */
/* Value translation                                                   */
/* ------------------------------------------------------------------ */

/* Xenix open(2) flags are the System V ones; substrate's are the Linux ones. */
#define X286_O_RDONLY   0000
#define X286_O_WRONLY   0001
#define X286_O_RDWR     0002
#define X286_O_NDELAY   0004
#define X286_O_APPEND   0010
#define X286_O_SYNC     0020
#define X286_O_CREAT    0400
#define X286_O_TRUNC    01000
#define X286_O_EXCL     02000

static int x286_open_flags(uint16_t xflags) {
    int flags = (int)(xflags & 0003U);   /* access mode is identical */

    if (xflags & X286_O_NDELAY) flags |= O_NONBLOCK;
    if (xflags & X286_O_APPEND) flags |= O_APPEND;
    if (xflags & X286_O_SYNC)   flags |= O_SYNC;
    if (xflags & X286_O_CREAT)  flags |= O_CREAT;
    if (xflags & X286_O_TRUNC)  flags |= O_TRUNC;
    if (xflags & X286_O_EXCL)   flags |= O_EXCL;
    return flags;
}

/* The reverse, for fcntl(F_GETFL). */
static uint16_t x286_from_open_flags(int flags) {
    uint16_t xflags = (uint16_t)(flags & 0003);

    if (flags & O_NONBLOCK) xflags |= X286_O_NDELAY;
    if (flags & O_APPEND)   xflags |= X286_O_APPEND;
    if (flags & O_SYNC)     xflags |= X286_O_SYNC;
    return xflags;
}

/*
 * Xenix signal numbers diverge from substrate's above SIGTERM (and at 7/10),
 * because both descend from V7 but picked different extensions.
 */
static const uint8_t x286_to_native_sig[] = {
    [1]  = SIGHUP,  [2]  = SIGINT,  [3]  = SIGQUIT, [4]  = SIGILL,
    [5]  = SIGTRAP, [6]  = SIGABRT, [7]  = SIGSYS,  /* SIGEMT: no analogue */
    [8]  = SIGFPE,  [9]  = SIGKILL, [10] = SIGBUS,  [11] = SIGSEGV,
    [12] = SIGSYS,  [13] = SIGPIPE, [14] = SIGALRM, [15] = SIGTERM,
    [16] = SIGUSR1, [17] = SIGUSR2, [18] = SIGCHLD, [19] = SIGPOLL,
    [20] = SIGPOLL,
};
#define X286_NSIG ((int)(sizeof(x286_to_native_sig) / sizeof(x286_to_native_sig[0])))

static int x286_signo(uint16_t xsig) {
    if (xsig == 0 || (int)xsig >= X286_NSIG) {
        return -EINVAL;
    }
    return (int)x286_to_native_sig[xsig];
}

/* struct stat as Xenix/286 lays it out: 16-bit ints, 2-byte alignment. */
struct x286_stat {
    int16_t  st_dev;
    uint16_t st_ino;
    uint16_t st_mode;
    int16_t  st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    int16_t  st_rdev;
    int32_t  st_size;
    int32_t  st_atime;
    int32_t  st_mtime;
    int32_t  st_ctime;
} __attribute__((packed));

static void x286_translate_stat(struct x286_stat *dst, const struct stat *src) {
    memset(dst, 0, sizeof(*dst));
    dst->st_dev   = (int16_t)src->st_dev;
    dst->st_ino   = (uint16_t)src->st_ino;
    dst->st_mode  = (uint16_t)src->st_mode;
    dst->st_nlink = (int16_t)src->st_nlink;
    dst->st_uid   = (uint16_t)src->st_uid;
    dst->st_gid   = (uint16_t)src->st_gid;
    dst->st_rdev  = (int16_t)src->st_rdev;
    dst->st_size  = (int32_t)src->st_size;
    dst->st_atime = (int32_t)src->st_atime;
    dst->st_mtime = (int32_t)src->st_mtime;
    dst->st_ctime = (int32_t)src->st_ctime;
}

/* struct timeb, for ftime(2). */
struct x286_timeb {
    int32_t  time;
    uint16_t millitm;
    int16_t  timezone;
    int16_t  dstflag;
} __attribute__((packed));

/* struct tms: four longs. */
struct x286_tms {
    int32_t tms_utime;
    int32_t tms_stime;
    int32_t tms_cutime;
    int32_t tms_cstime;
} __attribute__((packed));

/* struct utsname, SYS_NMLN == 9 on Xenix. */
#define X286_NMLN 9
struct x286_utsname {
    char     sysname[X286_NMLN];
    char     nodename[X286_NMLN];
    char     release[X286_NMLN];
    char     version[X286_NMLN];
    char     machine[X286_NMLN];
    char     reserved[15];
    uint16_t sysorigin;
    uint16_t sysoem;
    int32_t  sysserial;
} __attribute__((packed));

/*
 * struct termio, the System V one Xenix ioctl(TCGETA) speaks.  The flag
 * words are 16-bit here and 32-bit in substrate's struct termios, but the
 * bit assignments in the low half are identical, so the conversion is a
 * narrowing/widening plus the c_cc reshuffle below.
 */
#define X286_NCC 8
struct x286_termio {
    uint16_t c_iflag;
    uint16_t c_oflag;
    uint16_t c_cflag;
    uint16_t c_lflag;
    char     c_line;
    uint8_t  c_cc[X286_NCC];
} __attribute__((packed));

/* System V termio aliases VMIN/VTIME onto VEOF/VEOL; substrate follows the
 * Linux termios layout where they sit at 6 and 5. */
#define X286_VEOF   4
#define X286_VEOL   5

static void x286_termios_to_termio(struct x286_termio *dst,
                                   const struct termios *src) {
    unsigned int i;

    memset(dst, 0, sizeof(*dst));
    dst->c_iflag = (uint16_t)src->c_iflag;
    dst->c_oflag = (uint16_t)src->c_oflag;
    dst->c_cflag = (uint16_t)src->c_cflag;
    dst->c_lflag = (uint16_t)src->c_lflag;
    dst->c_line  = (char)src->c_line;
    for (i = 0; i < X286_NCC; i++) {
        dst->c_cc[i] = src->c_cc[i];
    }
    if (!(src->c_lflag & ICANON)) {
        dst->c_cc[X286_VEOF] = src->c_cc[VMIN];
        dst->c_cc[X286_VEOL] = src->c_cc[VTIME];
    }
}

static void x286_termio_to_termios(struct termios *dst,
                                   const struct x286_termio *src) {
    unsigned int i;

    /* Preserve the high halves and the trailing c_cc slots substrate uses
     * but termio has no room for (VSTART/VSTOP/VSUSP/...). */
    dst->c_iflag = (dst->c_iflag & 0xFFFF0000U) | src->c_iflag;
    dst->c_oflag = (dst->c_oflag & 0xFFFF0000U) | src->c_oflag;
    dst->c_cflag = (dst->c_cflag & 0xFFFF0000U) | src->c_cflag;
    dst->c_lflag = (dst->c_lflag & 0xFFFF0000U) | src->c_lflag;
    dst->c_line  = (cc_t)src->c_line;
    for (i = 0; i < X286_NCC; i++) {
        dst->c_cc[i] = src->c_cc[i];
    }
    if (!(src->c_lflag & ICANON)) {
        dst->c_cc[VMIN]  = src->c_cc[X286_VEOF];
        dst->c_cc[VTIME] = src->c_cc[X286_VEOL];
    }
}

/* ------------------------------------------------------------------ */
/* Ordinary System V calls                                             */
/* ------------------------------------------------------------------ */

static int64_t x286_sys_exit(struct x286_frame *f) {
    /* Xenix passes the whole word; the wait status keeps the low byte. */
    return sys_exit((int)(int16_t)f->bx);
}

/*
 * fork(2).  Xenix returns the *other* process's pid in AX and uses BX as the
 * discriminator -- its _fork stub is
 *
 *     mov  $2,%ax
 *     call __syscall
 *     jb   cerror
 *     and  %bx,%bx
 *     jz   child          ; BX == 0: this is the child, return 0
 *     ret                 ; BX != 0: parent, AX is the child pid
 *
 * The child never comes back through this handler -- sched_fork_thread
 * copies the trap frame and forces only EAX to 0 -- so the child's BX has to
 * be staged into the frame before the fork, and the parent's restored after.
 * The carry flag needs the same treatment: the child inherits it, and a set
 * CF would send it straight to cerror.
 */
static int64_t x286_sys_fork(struct x286_frame *f) {
    registers_t *regs = f->regs;
    uint32_t saved_ebx = regs->ebx;
    uint32_t saved_eflags = regs->eflags;
    int pid;

    regs->ebx = 0;
    regs->eflags &= ~X286_EFLAGS_CF;
    pid = sys_fork();
    regs->ebx = saved_ebx;
    regs->eflags = saved_eflags;

    if (pid < 0) {
        return pid;
    }
    return (int64_t)((uint32_t)1U << 16 | ((uint32_t)pid & 0xFFFFU));
}

/*
 * Xenix/286 has no getdents(2): a directory is read with read(2) and comes
 * back as a stream of 16-byte V7 records, which is how ls(1) lists a
 * directory and how ttyname(3) finds the terminal by scanning /dev.  Nothing
 * in substrate speaks that format, so synthesize it here from readdir_fs(),
 * advancing the file offset with the same deletion-stable cursor rule
 * kern_getdents() uses.
 */
struct x286_direct {
    uint16_t d_ino;
    char     d_name[14];   /* NOT NUL-terminated when it fills the field */
} __attribute__((packed));

/*
 * The vnode behind a descriptor, or NULL.  f_data holds a pipe, socket or
 * kqueue object for those descriptor types and must never be dereferenced as
 * a vnode; kern_open() leaves f_type zero for an ordinary file, so the test
 * is by exclusion, matching sys_lseek()'s seekability check.
 */
static fs_node_t *x286_fd_vnode(int fd) {
    file_t *file;

    if (fd < 0 || fd >= MAX_FD || !current_process) {
        return NULL;
    }
    file = current_process->fds[fd];
    if (!file) {
        return NULL;
    }
    if (file->f_type == DTYPE_PIPE || file->f_type == DTYPE_SOCKET ||
        file->f_type == DTYPE_KQUEUE) {
        return NULL;
    }
    return (fs_node_t *)file->f_data;
}

static int64_t x286_read_directory(int fd, uintptr_t dst, uint32_t count) {
    file_t *file;
    fs_node_t *node;
    uint32_t out = 0;

    node = x286_fd_vnode(fd);
    if (!node) {
        return -EBADF;
    }
    file = current_process->fds[fd];

    while (count - out >= sizeof(struct x286_direct)) {
        struct x286_direct rec;
        struct dirent dent;
        struct dirent *d = readdir_fs(node, (uint64_t)file->f_offset, &dent);
        uint64_t cur, next;
        size_t i;

        if (!d) {
            break;   /* end of directory */
        }
        memset(&rec, 0, sizeof(rec));
        rec.d_ino = (uint16_t)d->d_ino;
        for (i = 0; i < sizeof(rec.d_name) && d->d_name[i]; i++) {
            rec.d_name[i] = d->d_name[i];
        }
        memcpy((void *)(dst + out), &rec, sizeof(rec));
        out += (uint32_t)sizeof(rec);

        cur = (uint64_t)file->f_offset;
        next = (d->d_off > cur) ? d->d_off : cur + 1;
        file->f_offset = (off_t)next;
    }
    return (int64_t)out;
}

static int64_t x286_sys_read(struct x286_frame *f) {
    int fd = (int)(int16_t)f->bx;
    uintptr_t buf;
    int rc = x286_ds_span(f, f->cx, f->si, &buf);

    if (rc != 0) {
        return rc;
    }
    {
        fs_node_t *node = x286_fd_vnode(fd);

        if (node && (node->flags & 0x7) == FS_DIRECTORY) {
            return x286_read_directory(fd, buf, (uint32_t)f->si);
        }
    }
    {
        int64_t rv = kern_read(fd, (char *)buf, (size_t)f->si);

        /*
         * System V O_NDELAY, which is what Xenix has, reports "nothing to
         * read right now" as a zero-length read -- famously indistinguishable
         * from end of file.  EAGAIN is the later POSIX O_NONBLOCK spelling
         * and means nothing to a 1987 binary: Word tests only for a positive
         * count, so handing it -EAGAIN would set carry and look like a hard
         * error on the keyboard.
         */
        if (rv == -EAGAIN) {
            return 0;
        }
        return rv;
    }
}

static int64_t x286_sys_write(struct x286_frame *f) {
    uintptr_t buf;
    int rc = x286_ds_span(f, f->cx, f->si, &buf);

    if (rc != 0) {
        return rc;
    }
    return kern_write((int)(int16_t)f->bx, (const char *)buf, (size_t)f->si);
}

static int64_t x286_sys_open(struct x286_frame *f) {
    char *path = NULL;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_open(path, x286_open_flags(f->cx), (int)f->si);
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_close(struct x286_frame *f) {
    return kern_close((int)(int16_t)f->bx);
}

static int64_t x286_sys_wait(struct x286_frame *f) {
    int status = 0;
    int pid;

    (void)f;
    pid = kern_waitpid(-1, &status, 0);
    if (pid < 0) {
        return pid;
    }
    /* AX = pid, BX = status: the V7 two-register return. */
    return (int64_t)((uint32_t)pid | ((uint32_t)(status & 0xFFFF) << 16));
}

static int64_t x286_sys_creat(struct x286_frame *f) {
    char *path = NULL;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_open(path, O_WRONLY | O_CREAT | O_TRUNC, (int)f->cx);
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_link(struct x286_frame *f) {
    char *oldp = NULL, *newp = NULL;
    int rc = x286_ds_string(f, f->bx, &oldp);

    if (rc != 0) {
        return rc;
    }
    rc = x286_ds_string(f, f->cx, &newp);
    if (rc == 0) {
        rc = kern_link(oldp, newp);
        x286_free_string(newp);
    }
    x286_free_string(oldp);
    return rc;
}

static int64_t x286_sys_unlink(struct x286_frame *f) {
    char *path = NULL;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_unlink(path);
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_chdir(struct x286_frame *f) {
    char *path = NULL;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_chdir(path);
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_time(struct x286_frame *f) {
    time_t now = kern_time(NULL);

    (void)f;
    if (now < 0) {
        return (int64_t)now;
    }
    /* AX = low half, BX = high half: the DX:AX long the Xenix stub builds. */
    return (int64_t)(uint32_t)now;
}

static int64_t x286_sys_mknod(struct x286_frame *f) {
    char *path = NULL;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = sys_mknod(path, (int)f->cx, (int)(int16_t)f->si);
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_chmod(struct x286_frame *f) {
    char *path = NULL;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = sys_chmod(path, (int)f->cx);
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_chown(struct x286_frame *f) {
    char *path = NULL;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = sys_chown(path, (int)(int16_t)f->cx, (int)(int16_t)f->si);
    x286_free_string(path);
    return rc;
}

/*
 * brk(2): the argument is a plain offset within DGROUP.  Nothing to map --
 * the loader already gave DGROUP its full 64 KiB -- so this is bookkeeping
 * against the stack, exactly as Xenix did within one segment.
 */
/*
 * The break and the stack grow toward each other inside the one 64 KiB
 * DGROUP, so the break is capped by whichever is lower: the absolute floor
 * under the top of the segment (XOUT286_STACK_RESERVE), or a guard band
 * below the live stack pointer.
 *
 * Both caps are deliberately loose.  brkctl(2) on DGROUP is the only way a
 * program like Microsoft Word can get memory at all -- it issues no sdget,
 * no shmget and never asks for a second segment -- and it sizes its own
 * request to leave itself the stack it wants.  Refusing more than that just
 * makes it retry smaller for ever; the program, not the kernel, is what
 * arbitrates this boundary, through crt0's _chkstk/__stkgrow.
 *
 * If you are here because Word 3.0 printed "Insufficient memory / MEMORY
 * ERROR!", it is almost certainly NOT this code.  That message is Word
 * exhausting its own 64 KiB DGROUP, and how close it comes depends on the
 * size of the terminal description it loads from
 * /usr/lib/MSTOOLS/termdesc.  Measured, with everything else identical:
 *
 *      vt52  5023 B  ok     console.sco    8954 B  ok
 *      vt100 6803 B  ok     color_console  9793 B  ok
 *      wyse50 7234 B ok     ansi           9859 B  INSUFFICIENT MEMORY
 *
 * 66 bytes decide it.  `ansi` is the largest entry in Word's own termdesc
 * and the only one that does not fit; every other terminal it knows works.
 * Word's layout is data+bss 0x6da0, a 0x1258 scratch buffer, then a heap it
 * asks for as 0x7c00 and accepts down to 0x200 in 512-byte steps (the retry
 * loop lives at 0x5f:0xb81c), against a ceiling of 0x10000 minus its own
 * 2 KiB stack reserve -- i.e. 0xf800, which is where its break lands.
 *
 * Note substrate's login sets TERM=linux, which Word's termdesc does not
 * contain at all: it exits with "No termdesc entry for linux" before any of
 * this.  Run Xenix MSTOOLS programs with TERM=vt100.
 */
static int64_t x286_set_break(const struct x286_frame *f, uint32_t newbrk) {
    uint32_t sp = f->regs->useresp & 0xFFFFU;
    uint32_t ceiling = XOUT286_WINDOW_SIZE - XOUT286_STACK_RESERVE;
    uint32_t sp_limit = (sp > X286_STACK_GUARD) ? sp - X286_STACK_GUARD : 0U;

    if (sp_limit < ceiling) {
        ceiling = sp_limit;
    }
    if (newbrk < current_process->brk_start || newbrk > ceiling) {
        return -ENOMEM;
    }
    current_process->brk = newbrk;
    return 0;
}

static int64_t x286_sys_brk(struct x286_frame *f) {
    int64_t rc = x286_set_break(f, f->bx);

    return rc < 0 ? rc : 0;
}

static int64_t x286_do_stat(struct x286_frame *f, const struct stat *native,
                            uint32_t buf_off) {
    struct x286_stat out;
    uintptr_t dst;
    int rc = x286_ds_span(f, buf_off, sizeof(out), &dst);

    if (rc != 0) {
        return rc;
    }
    x286_translate_stat(&out, native);
    memcpy((void *)dst, &out, sizeof(out));
    return 0;
}

static int64_t x286_sys_stat(struct x286_frame *f) {
    char *path = NULL;
    struct stat native;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_stat(path, &native);
    x286_free_string(path);
    if (rc != 0) {
        return rc;
    }
    return x286_do_stat(f, &native, f->cx);
}

static int64_t x286_sys_fstat(struct x286_frame *f) {
    struct stat native;
    int rc = kern_fstat((int)(int16_t)f->bx, &native);

    if (rc != 0) {
        return rc;
    }
    return x286_do_stat(f, &native, f->cx);
}

static int64_t x286_sys_lseek(struct x286_frame *f) {
    /* off_t is a *signed* long: CX is its low half, SI its high half.  It has
     * to be sign-extended into the 64-bit offset sys_lseek takes, because
     * seeking backwards is ordinary -- brand(1) rewrites a header by seeking
     * to a negative displacement from the current position or the end.
     * Widening it as unsigned turned every such seek into one of nearly 4 GiB
     * instead, and the write that followed extended the file past 4 GiB: the
     * branded binaries came out with correct contents, a correct block count,
     * and an i_size of 0x1_0000_0166. */
    int32_t off = (int32_t)((uint32_t)f->cx | ((uint32_t)f->si << 16));
    int64_t off64 = (int64_t)off;
    int64_t rc = sys_lseek((int)(int16_t)f->bx, (uint32_t)off64,
                           (uint32_t)((uint64_t)off64 >> 32),
                           (int)(int16_t)f->di);

    if (rc < 0) {
        return rc;
    }
    return (int64_t)(uint32_t)rc;
}

static int64_t x286_sys_getpid(struct x286_frame *f) {
    uint32_t pid = (uint32_t)sys_getpid();
    uint32_t ppid = (uint32_t)sys_getppid();

    (void)f;
    return (int64_t)((pid & 0xFFFFU) | ((ppid & 0xFFFFU) << 16));
}

static int64_t x286_sys_mount(struct x286_frame *f) {
    char *spec = NULL, *dir = NULL;
    int rc = x286_ds_string(f, f->bx, &spec);

    if (rc != 0) {
        return rc;
    }
    rc = x286_ds_string(f, f->cx, &dir);
    if (rc == 0) {
        rc = kern_mount(spec, dir, NULL, (unsigned long)f->si, NULL);
        x286_free_string(dir);
    }
    x286_free_string(spec);
    return rc;
}

static int64_t x286_sys_umount(struct x286_frame *f) {
    char *spec = NULL;
    int rc = x286_ds_string(f, f->bx, &spec);

    if (rc != 0) {
        return rc;
    }
    rc = kern_umount(spec);
    x286_free_string(spec);
    return rc;
}

static int64_t x286_sys_setuid(struct x286_frame *f) {
    return sys_setuid((int)(int16_t)f->bx);
}

static int64_t x286_sys_getuid(struct x286_frame *f) {
    uint32_t uid = (uint32_t)sys_getuid();
    uint32_t euid = (uint32_t)sys_geteuid();

    (void)f;
    return (int64_t)((uid & 0xFFFFU) | ((euid & 0xFFFFU) << 16));
}

static int64_t x286_sys_stime(struct x286_frame *f) {
    time_t t = (time_t)((uint32_t)f->bx | ((uint32_t)f->cx << 16));

    return sys_stime(&t);
}

static int64_t x286_sys_alarm(struct x286_frame *f) {
    return (int64_t)(uint32_t)sys_alarm((unsigned int)f->bx);
}

static int64_t x286_sys_pause(struct x286_frame *f) {
    (void)f;
    return sys_pause();
}

static int64_t x286_sys_utime(struct x286_frame *f) {
    char *path = NULL;
    uintptr_t times = 0;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    if (f->cx != 0) {
        rc = x286_ds_span(f, f->cx, 2U * sizeof(int32_t), &times);
        if (rc != 0) {
            x286_free_string(path);
            return rc;
        }
    }
    rc = sys_utime(path, (void *)times);
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_access(struct x286_frame *f) {
    char *path = NULL;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_access(path, (int)f->cx);
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_nice(struct x286_frame *f) {
    return sys_nice((int)(int16_t)f->bx);
}

static int64_t x286_sys_sync(struct x286_frame *f) {
    (void)f;
    return sys_sync();
}

static int64_t x286_sys_kill(struct x286_frame *f) {
    int sig = x286_signo(f->cx);

    if (f->cx != 0 && sig < 0) {
        return sig;
    }
    return sys_kill((int)(int16_t)f->bx, f->cx ? sig : 0);
}

static int64_t x286_sys_setpgrp(struct x286_frame *f) {
    /* Xenix setpgrp() takes no argument, makes the caller a group leader
     * and returns the resulting group -- which is the caller's own pid
     * whether or not it already was one. */
    (void)f;
    (void)sys_setpgid(0, 0);
    return sys_getpgrp();
}

static int64_t x286_sys_dup(struct x286_frame *f) {
    /*
     * Xenix folds dup2 into dup: bit 0100 of the fd means "and use CX as the
     * new descriptor".  The libc's _gdup stub is what sets it.
     */
    if (f->bx & 0100U) {
        return sys_dup2((int)(f->bx & 077U), (int)(int16_t)f->cx);
    }
    return sys_dup((int)(int16_t)f->bx);
}

static int64_t x286_sys_pipe(struct x286_frame *f) {
    int fds[2] = { -1, -1 };
    int rc;

    (void)f;
    rc = kern_pipe(fds);
    if (rc < 0) {
        return rc;
    }
    /* AX = read end, BX = write end. */
    return (int64_t)(((uint32_t)fds[0] & 0xFFFFU) |
                     (((uint32_t)fds[1] & 0xFFFFU) << 16));
}

static int64_t x286_sys_times(struct x286_frame *f) {
    struct tms native;
    struct x286_tms out;
    uintptr_t dst;
    clock_t rc;
    int err = x286_ds_span(f, f->bx, sizeof(out), &dst);

    if (err != 0) {
        return err;
    }
    memset(&native, 0, sizeof(native));
    rc = kern_times(&native);
    /* clock_t is 32 bits: a negative errno lives in its sign bit. */
    if ((int32_t)rc < 0) {
        return (int64_t)(int32_t)rc;
    }
    out.tms_utime  = (int32_t)native.tms_utime;
    out.tms_stime  = (int32_t)native.tms_stime;
    out.tms_cutime = (int32_t)native.tms_cutime;
    out.tms_cstime = (int32_t)native.tms_cstime;
    memcpy((void *)dst, &out, sizeof(out));
    return (int64_t)(uint32_t)rc;
}

static int64_t x286_sys_setgid(struct x286_frame *f) {
    return sys_setgid((int)(int16_t)f->bx);
}

static int64_t x286_sys_getgid(struct x286_frame *f) {
    uint32_t gid = (uint32_t)sys_getgid();
    uint32_t egid = (uint32_t)sys_getegid();

    (void)f;
    return (int64_t)((gid & 0xFFFFU) | ((egid & 0xFFFFU) << 16));
}

/*
 * signal(2).  Xenix passes the handler as a far pointer: CX is the offset
 * and SI the selector (both zero for SIG_DFL, CX == 1 for SIG_IGN).  We
 * stash the far pointer in the native disposition so it survives here, and
 * hand delivery to x286_sendsig below.
 */
#define X286_SIG_DFL  0U
#define X286_SIG_IGN  1U

static int64_t x286_sys_signal(struct x286_frame *f) {
    struct sigaction act, old;
    int sig = x286_signo(f->bx);
    uint32_t handler;
    int rc;

    if (sig < 0) {
        return sig;
    }
    memset(&act, 0, sizeof(act));
    memset(&old, 0, sizeof(old));

    if (f->si == 0 && f->cx == X286_SIG_DFL) {
        act.sa_handler = (void *)SIG_DFL;
    } else if (f->si == 0 && f->cx == X286_SIG_IGN) {
        act.sa_handler = (void *)SIG_IGN;
    } else {
        act.sa_handler = (void *)(uintptr_t)(((uint32_t)f->si << 16) |
                                             (uint32_t)f->cx);
        /* V7 semantics, which Xenix keeps: the disposition reverts to
         * SIG_DFL as the handler is entered, and the handler re-arms it. */
        act.sa_flags = SA_RESETHAND;
    }

    rc = kern_sigaction(sig, &act, &old);
    if (rc != 0) {
        return rc;
    }

    handler = (uint32_t)(uintptr_t)old.sa_handler;
    if (handler == (uint32_t)(uintptr_t)SIG_DFL) {
        return 0;
    }
    if (handler == (uint32_t)(uintptr_t)SIG_IGN) {
        return X286_SIG_IGN;
    }
    /* AX = offset, BX = selector -- the DX:AX far pointer the stub returns. */
    return (int64_t)handler;
}

static int64_t x286_sys_acct(struct x286_frame *f) {
    char *path = NULL;
    int rc;

    if (f->bx == 0) {
        return kern_acct(NULL);
    }
    rc = x286_ds_string(f, f->bx, &path);
    if (rc != 0) {
        return rc;
    }
    rc = kern_acct(path);
    x286_free_string(path);
    return rc;
}

/*
 * ioctl(2).  Xenix numbers the termio group ('T'<<8|n) one lower than
 * substrate does, because substrate follows Linux in reserving 0x5401..04
 * for the termios (TCGETS) family that Xenix has no equivalent of.  The
 * struct differs too -- see x286_termios_to_termio.
 */
#define X286_TIOC    ('T' << 8)
#define X286_TCGETA  (X286_TIOC | 1)
#define X286_TCSETA  (X286_TIOC | 2)
#define X286_TCSETAW (X286_TIOC | 3)
#define X286_TCSETAF (X286_TIOC | 4)
#define X286_TCSBRK  (X286_TIOC | 5)
#define X286_TCXONC  (X286_TIOC | 6)
#define X286_TCFLSH  (X286_TIOC | 7)

static int64_t x286_ioctl_termio(struct x286_frame *f, int fd, uint16_t cmd) {
    struct termios native;
    struct x286_termio user;
    uintptr_t argp;
    uint32_t set_cmd;
    int rc = x286_ds_span(f, f->si, sizeof(user), &argp);

    if (rc != 0) {
        return rc;
    }
    if (cmd == X286_TCGETA) {
        rc = kern_ioctl(fd, TCGETS, &native);
        if (rc != 0) {
            return rc;
        }
        x286_termios_to_termio(&user, &native);
        memcpy((void *)argp, &user, sizeof(user));
        return 0;
    }

    /* Read-modify-write: termio cannot express the speeds or the tail of
     * c_cc, so start from what the tty currently has. */
    rc = kern_ioctl(fd, TCGETS, &native);
    if (rc != 0) {
        return rc;
    }
    memcpy(&user, (const void *)argp, sizeof(user));
    x286_termio_to_termios(&native, &user);

    switch (cmd) {
    case X286_TCSETA:  set_cmd = TCSETS;  break;
    case X286_TCSETAW: set_cmd = TCSETSW; break;
    default:           set_cmd = TCSETSF; break;
    }
    return kern_ioctl(fd, set_cmd, &native);
}

static int64_t x286_sys_ioctl(struct x286_frame *f) {
    int fd = (int)(int16_t)f->bx;
    uint16_t cmd = f->cx;
    uintptr_t argp = 0;

    switch (cmd) {
    case X286_TCGETA:
    case X286_TCSETA:
    case X286_TCSETAW:
    case X286_TCSETAF:
        return x286_ioctl_termio(f, fd, cmd);
    case X286_TCSBRK:
        return kern_ioctl(fd, TCSBRK, (void *)(uintptr_t)f->si);
    case X286_TCXONC:
        return kern_ioctl(fd, TCXONC, (void *)(uintptr_t)f->si);
    case X286_TCFLSH:
        return kern_ioctl(fd, TCFLSH, (void *)(uintptr_t)f->si);
    default:
        break;
    }

    /* Anything else: hand the near pointer through untranslated and let the
     * driver decide.  Unknown requests come back ENOTTY, which is what a
     * Xenix program expects when it probes for a capability. */
    if (f->si != 0 && x286_ds_span(f, f->si, 1, &argp) != 0) {
        argp = 0;
    }
    return kern_ioctl(fd, (uint32_t)cmd, (void *)argp);
}

/* utssys(buf, mv, type): AH selects uname (0) or ustat (2). */
static int64_t x286_sys_utssys(struct x286_frame *f) {
    struct utsname native;
    struct x286_utsname out;
    uintptr_t dst;
    int rc;

    if (f->sub != 0) {
        return -ENOSYS;   /* ustat / fusers */
    }
    rc = x286_ds_span(f, f->bx, sizeof(out), &dst);
    if (rc != 0) {
        return rc;
    }
    memset(&native, 0, sizeof(native));
    rc = kern_uname(&native);
    if (rc != 0) {
        return rc;
    }
    memset(&out, 0, sizeof(out));
    strlcpy(out.sysname, "Xenix", sizeof(out.sysname));
    strlcpy(out.nodename, native.nodename, sizeof(out.nodename));
    strlcpy(out.release, "2.3", sizeof(out.release));
    strlcpy(out.version, "2", sizeof(out.version));
    strlcpy(out.machine, "i286", sizeof(out.machine));
    memcpy((void *)dst, &out, sizeof(out));
    return 0;
}

/*
 * Free a vector built by x286_copy_vector.  `slots` is the allocation's own
 * entry count, not the number of strings actually filled in -- a partially
 * built vector (the error path) has NULLs in the tail but was still sized
 * for the whole thing, and kfree() wants the size it was handed.
 */
static void x286_free_vector(char **vec, size_t slots) {
    size_t i;

    if (!vec) {
        return;
    }
    for (i = 0; i < slots; i++) {
        x286_free_string(vec[i]);
    }
    kfree(vec, slots * sizeof(char *));
}

/* Pull a NULL-terminated array of 16-bit near pointers out of DS. */
#define X286_MAX_VEC 256

static int x286_copy_vector(struct x286_frame *f, uint32_t off, char ***out,
                            size_t *slots_out) {
    uintptr_t linear;
    const uint16_t *src;
    char **vec;
    size_t count = 0;
    size_t i;
    int rc;

    *out = NULL;
    *slots_out = 0;
    if (off == 0) {
        return 0;
    }
    rc = x286_ds_span(f, off, sizeof(uint16_t), &linear);
    if (rc != 0) {
        return rc;
    }
    src = (const uint16_t *)linear;
    while (count < X286_MAX_VEC) {
        if (x286_ds_span(f, off + (uint32_t)(count * 2U), sizeof(uint16_t),
                         NULL) != 0) {
            return -EFAULT;
        }
        if (src[count] == 0) {
            break;
        }
        count++;
    }
    if (count >= X286_MAX_VEC) {
        return -E2BIG;
    }

    vec = kmalloc((count + 1U) * sizeof(char *));
    if (!vec) {
        return -ENOMEM;
    }
    memset(vec, 0, (count + 1U) * sizeof(char *));
    for (i = 0; i < count; i++) {
        rc = x286_ds_string(f, src[i], &vec[i]);
        if (rc != 0) {
            x286_free_vector(vec, count + 1U);
            return rc;
        }
    }
    *out = vec;
    *slots_out = count + 1U;
    return 0;
}

static int64_t x286_sys_execve(struct x286_frame *f) {
    char *path = NULL;
    char **argv = NULL;
    char **envp = NULL;
    size_t argv_slots = 0, envp_slots = 0;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = x286_copy_vector(f, f->cx, &argv, &argv_slots);
    if (rc == 0) {
        rc = x286_copy_vector(f, f->si, &envp, &envp_slots);
    }
    if (x286_trace_enabled()) {
        char buf[160];

        /* The personality prefix still applies -- a Xenix /bin/sh under
         * /perso/xenix wins -- but exec is allowed to fall through to a
         * substrate-native binary, since exec replaces the personality too. */
        snprintf(buf, sizeof(buf), "X286: [%d] execve \"%s\" argv0=\"%s\"\n",
                 current_process ? (int)current_process->pid : -1,
                 path, (argv && argv[0]) ? argv[0] : "");
        kprint(buf);
    }
    if (rc == 0) {
        rc = kern_execve(path, argv, envp);
    }
    x286_free_vector(argv, argv_slots);
    x286_free_vector(envp, envp_slots);
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_exec(struct x286_frame *f) {
    /* The pre-environ exec(path, argv): inherit the current environment. */
    struct x286_frame local = *f;

    local.si = 0;
    return x286_sys_execve(&local);
}

static int64_t x286_sys_umask(struct x286_frame *f) {
    return sys_umask((int)f->bx);
}

static int64_t x286_sys_chroot(struct x286_frame *f) {
    char *path = NULL;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_chroot(path);
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_fcntl(struct x286_frame *f) {
    int fd = (int)(int16_t)f->bx;
    int rc;

    /* The command numbers match substrate's, but F_GETFL/F_SETFL carry
     * *open flags*, and Xenix's are the System V values -- O_NDELAY is 0004
     * there and 0x800 here.  Passing them through untranslated silently
     * dropped O_NDELAY, which turned a program's polling read of the
     * keyboard into a blocking one.  The record-locking commands need a
     * struct flock translation no caller has needed yet. */
    switch (f->cx) {
    case F_DUPFD:
    case F_GETFD:
    case F_SETFD:
        return sys_fcntl(fd, (int)f->cx, (int)(int16_t)f->si);
    case F_GETFL:
        rc = sys_fcntl(fd, F_GETFL, 0);
        if (rc < 0) {
            return rc;
        }
        return (int64_t)x286_from_open_flags(rc);
    case F_SETFL:
        return sys_fcntl(fd, F_SETFL, x286_open_flags(f->si));
    default:
        return -EINVAL;
    }
}

static int64_t x286_sys_ulimit(struct x286_frame *f) {
    long arg = (long)(uint32_t)((uint32_t)f->cx | ((uint32_t)f->si << 16));
    int rc = sys_ulimit((int)(int16_t)f->bx, arg);

    if (rc < 0) {
        return rc;
    }
    return (int64_t)(uint32_t)rc;
}

/* ------------------------------------------------------------------ */
/* Call 40: the Xenix multiplexer                                      */
/* ------------------------------------------------------------------ */

/*
 * brkctl(command, long increment, char far *ptr) -- Xenix's segmented
 * sbrk(2).  BX is the command, CX:SI the signed increment and DI the
 * selector half of ptr (its offset "is never used", per brkctl(S)).  The
 * result is a far pointer to the base of the affected region, or -1.
 *
 * DGROUP's break lives in current_process->brk, as it does for every other
 * personality.  A far data segment instead carries its break in its own
 * descriptor limit: the loader sized it to its contents, growing it here is
 * a descriptor edit, and the ISR reloads DS/ES/FS/GS from the trap frame on
 * the way out, so the CPU re-reads the new limit before user code runs.
 */
static int64_t x286_brkctl_grow_seg(uint16_t sel, int32_t increment) {
    gdt_entry_t *entry = x286_ldt_entry(sel);
    struct user_desc info;
    uint32_t old_size, new_size;

    if (!entry || (entry->access & 0x08U) != 0U) {
        return -EINVAL;   /* absent, or a code segment */
    }
    old_size = ldt_entry_limit(entry) + 1U;
    if (increment >= 0) {
        if ((uint32_t)increment > XOUT286_WINDOW_SIZE - old_size) {
            return -ENOMEM;
        }
        new_size = old_size + (uint32_t)increment;
    } else {
        if ((uint32_t)(-increment) > old_size) {
            return -EINVAL;
        }
        new_size = old_size - (uint32_t)(-increment);
        if (new_size == 0) {
            new_size = 1;
        }
    }

    memset(&info, 0, sizeof(info));
    info.base_addr = ldt_entry_base(entry);
    info.limit = new_size - 1U;
    info.limit_in_pages = 0;
    info.seg_32bit = 0;
    info.contents = 0;
    info.useable = 1;
    fill_ldt_entry(entry, &info);

    /* Positive: base of the new region.  Negative or zero: the new end.
     *
     * Clamp the offset to 16 bits before packing it beside the selector.  A
     * segment grown to the full 64 KiB has an end of 0x10000, which would
     * carry into the selector half and hand the caller a far pointer into
     * the NEXT descriptor. */
    {
        uint32_t off = (increment > 0) ? old_size : new_size;

        if (off > 0xFFFFU) {
            off = 0xFFFFU;
        }
        return (int64_t)(((uint32_t)sel << 16) | off);
    }
}

static int64_t x286_brkctl_new_seg(struct x286_frame *f, int32_t increment) {
    unsigned int count = (unsigned int)current_process->ldt_entry_count;
    unsigned int idx = count;
    gdt_entry_t *entries;
    struct user_desc info;
    vm_object_t *obj;
    uint32_t base, size;
    uint16_t sel;
    int rc;

    (void)f;
    if (increment < 0) {
        return -EINVAL;   /* BR_NEWSEG may not shrink */
    }
    /*
     * increment == 0 means "a whole new segment", not "nothing".
     *
     * This is how a small-data program reaches memory beyond DGROUP.  Word
     * 3.0 is large-text/small-data (x_renv 0xc847: XE_LTEXT set, XE_LDATA
     * clear), so every byte it owns lives in the single 64 KiB DGROUP -- and
     * once the break has climbed as far as it goes, the only way on is a
     * second segment.  Word asks for exactly that:
     *
     *   brkctl(BR_IMPSEG, 0, ...) = 0x6f:f800   -- how far did the break get?
     *   brkctl(BR_NEWSEG, 0, ...) = -EINVAL     -- may I have another segment?
     *
     * Rejecting the second call was read by Word as "no memory left anywhere",
     * and it printed "Insufficient memory" / "MEMORY ERROR!" and bailed out to
     * its emergency save.  A zero increment is not a shrink and not a
     * malformed request; on a 286 a fresh data segment has exactly one useful
     * size, the 64 KiB architectural maximum, which is also the window this
     * loader hands every segment.  Give it that; the caller sizes its own
     * allocations inside it and can trim the descriptor later with
     * BR_ARGSEG.
     */
    size = increment > 0 ? (uint32_t)increment : XOUT286_WINDOW_SIZE;
    if (size > XOUT286_WINDOW_SIZE) {
        return -ENOMEM;
    }
    if (idx >= XOUT286_MAX_SEGS) {
        return -ENOMEM;
    }

    base = xout286_window_base(idx);
    obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, XOUT286_WINDOW_SIZE);
    if (!obj) {
        return -ENOMEM;
    }
    if (vm_map_insert(current_process->vm_map, obj, 0, base,
                      base + XOUT286_WINDOW_SIZE,
                      VM_PROT_READ | VM_PROT_WRITE,
                      VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_COPY) != 0) {
        vm_object_deallocate(obj);
        return -ENOMEM;
    }

    entries = kmalloc((idx + 1U) * sizeof(gdt_entry_t));
    if (!entries) {
        return -ENOMEM;
    }
    memcpy(entries, current_process->ldt, count * sizeof(gdt_entry_t));
    memset(&entries[idx], 0, sizeof(gdt_entry_t));

    memset(&info, 0, sizeof(info));
    info.base_addr = base;
    info.limit = size - 1U;
    info.limit_in_pages = 0;
    info.seg_32bit = 0;
    info.contents = 0;
    info.useable = 1;
    fill_ldt_entry(&entries[idx], &info);

    rc = ldt_replace_process(current_process, entries, idx + 1U);
    kfree(entries, (idx + 1U) * sizeof(gdt_entry_t));
    if (rc != 0) {
        return -ENOMEM;
    }
    ldt_activate(current_process);

    sel = (uint16_t)((idx << 3) | 0x04U | 0x03U);
    return (int64_t)((uint32_t)sel << 16);   /* offset 0 in the new segment */
}

/*
 * BR_IMPSEG names "the implied segment": the program's LAST data segment.
 *
 * The LDT is laid out by the loader in segment-table order and only ever
 * appended to, by BR_NEWSEG -- so the last data segment is simply the
 * highest-numbered present, non-code entry.  That is DGROUP for a program
 * whose only data segment it is, the trailing far data segment for one built
 * with several, and the newest arrival once BR_NEWSEG has handed one out.
 *
 * Resolving this to DGROUP unconditionally is what stranded Word 3.0.  Having
 * filled DGROUP it asked for a second segment and then asked the implied
 * segment how much room it had -- and got DGROUP's maxed-out break back,
 * every time, no matter how many fresh segments it was given:
 *
 *   brkctl(BR_IMPSEG, 0) = 0x6f:f800   -- DGROUP, full
 *   brkctl(BR_NEWSEG, 0) = 0x7f:0000   -- a whole new segment
 *   brkctl(BR_IMPSEG, 0) = 0x6f:f800   -- ...and still DGROUP, full
 */
static uint16_t x286_last_data_sel(uint16_t dgroup) {
    unsigned int count = (unsigned int)current_process->ldt_entry_count;
    const gdt_entry_t *ldt = (const gdt_entry_t *)current_process->ldt;
    uint16_t last = dgroup;

    if (!ldt) {
        return dgroup;
    }
    for (unsigned int i = 0; i < count; i++) {
        const gdt_entry_t *e = &ldt[i];

        if ((e->access & 0x80U) == 0U) continue;   /* not present */
        if ((e->access & 0x10U) == 0U) continue;   /* not a code/data segment */
        if ((e->access & 0x08U) != 0U) continue;   /* code, not data */
        last = (uint16_t)((i << 3) | 0x04U | 0x03U);
    }
    return last;
}

static int64_t x286_xsys_brkctl(struct x286_frame *f) {
    int32_t increment = (int32_t)((uint32_t)f->cx | ((uint32_t)f->si << 16));
    uint16_t cmd = f->bx & (uint16_t)~X286_BR_HUGE;
    uint16_t dgroup = x286_dgroup_sel(f);
    uint16_t sel = f->di;

    if (cmd == X286_BR_IMPSEG) {
        sel = x286_last_data_sel(dgroup);
        cmd = X286_BR_ARGSEG;
    }

    switch (cmd) {
    case X286_BR_ARGSEG:
        if (sel == dgroup) {
            uint32_t old = current_process->brk;
            uint32_t want;
            int64_t rc;

            if (increment >= 0) {
                if ((uint32_t)increment > XOUT286_WINDOW_SIZE - old) {
                    return -ENOMEM;
                }
                want = old + (uint32_t)increment;
            } else {
                if ((uint32_t)(-increment) > old) {
                    return -EINVAL;
                }
                want = old - (uint32_t)(-increment);
            }
            rc = x286_set_break(f, want);
            if (rc < 0) {
                return rc;
            }
            return (int64_t)(((uint32_t)dgroup << 16) |
                             (increment > 0 ? old : want));
        }
        return x286_brkctl_grow_seg(sel, increment);

    case X286_BR_NEWSEG:
        return x286_brkctl_new_seg(f, increment);

    case X286_BR_FREESEG: {
        gdt_entry_t *entry = x286_ldt_entry(sel);

        if (!entry || sel == dgroup) {
            return -EINVAL;
        }
        entry->access &= (uint8_t)~0x80U;   /* mark not present */
        return (int64_t)((uint32_t)sel << 16);
    }

    default:
        return -EINVAL;
    }
}

/*
 * __stkgrow: crt0 calls this from _chkstk when a function's frame would push
 * SP below STKHQQ.  BX is the lowest offset the stack now needs; the reply
 * is the lowest offset we will actually allow, which crt0 stores back into
 * STKHQQ with a 128-byte cushion added.  Since DGROUP is already a full
 * 64 KiB mapping, "growing" the stack only means checking it has not run
 * into the heap.
 */
static int64_t x286_xsys_stkgrow(struct x286_frame *f) {
    uint32_t want = f->bx;

    /*
     * The stack may grow down to the break and no further -- they grow toward
     * each other in the one 64 KiB DGROUP, so the break is the floor.
     *
     * This used to demand a further X286_STACK_GUARD (1 KiB) of clearance,
     * which double-counts: brkctl already refuses to raise the break above
     * XOUT286_WINDOW_SIZE - XOUT286_STACK_RESERVE, and x286_set_break also
     * caps it at sp - X286_STACK_GUARD, so the stack is already protected
     * from the break's side.  Charging the guard again here spends it out of
     * the stack instead.  Word reserves exactly 2 KiB -- it stops its break
     * at 0xf800 for precisely that -- and the guard left only 1 KiB of it
     * usable, so `stkgrow 0xfac0` (a 1344-byte stack, comfortably inside the
     * reserve) came back ENOMEM and Word killed itself with SIGBUS.
     */
    if (want <= current_process->brk) {
        return -ENOMEM;
    }
    return (int64_t)want;
}

static int64_t x286_xsys_ftime(struct x286_frame *f) {
    struct x286_timeb out;
    struct timeval tv;
    uintptr_t dst;
    int rc = x286_ds_span(f, f->bx, sizeof(out), &dst);

    if (rc != 0) {
        return rc;
    }
    memset(&tv, 0, sizeof(tv));
    rc = kern_gettimeofday(&tv, NULL);
    if (rc != 0) {
        return rc;
    }
    memset(&out, 0, sizeof(out));
    out.time = (int32_t)tv.tv_sec;
    out.millitm = (uint16_t)(tv.tv_usec / 1000);
    out.timezone = 0;
    out.dstflag = 0;
    memcpy((void *)dst, &out, sizeof(out));
    return 0;
}

static int64_t x286_xsys_nap(struct x286_frame *f) {
    /* nap(long milliseconds) -- returns the time actually slept. */
    uint32_t ms = (uint32_t)f->bx | ((uint32_t)f->cx << 16);
    uint32_t hz = get_hz();
    uint64_t ticks, deadline;

    if (ms == 0) {
        sched_yield();
        return 0;
    }
    /* Round up so a nap is never shorter than asked, then add the usual
     * extra tick for the partial one we are already inside. */
    ticks = ((uint64_t)ms * hz + 999U) / 1000U;
    deadline = get_ticks() + ticks + 1U;

    current_thread->flags |= THREAD_F_INTERRUPTIBLE;
    (void)sched_sleep_until(&current_thread->sig_pending, deadline);
    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
    return (int64_t)ms;
}

static int64_t x286_xsys_rdchk(struct x286_frame *f) {
    /* rdchk(fd): 1 if a read would not block, 0 if it would. */
    struct pollfd pfd;
    int rc;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = (int)(int16_t)f->bx;
    pfd.events = POLLIN;
    rc = kern_poll(&pfd, 1, 0);
    if (rc < 0) {
        return rc;
    }
    return (pfd.revents & POLLIN) ? 1 : 0;
}

static int64_t x286_xsys_chsize(struct x286_frame *f) {
    /* The new size is one 32-bit long in CX:SI -- CX the low HALFWORD, SI the
     * high one -- not two 32-bit halves, which is what sys_ftruncate takes.
     * Passing them straight through put SI into bits 32..47 of a 64-bit
     * length, so truncating a 135 KiB file (SI == 2) asked for a size of
     * 0x2_0000_0000 and left an inode claiming 8 GiB.  Files under 64 KiB
     * came out right only because their SI was zero. */
    int32_t len = (int32_t)((uint32_t)f->cx | ((uint32_t)f->si << 16));

    if (len < 0) {
        return -EINVAL;
    }
    return sys_ftruncate((int)(int16_t)f->bx, (uint32_t)len, 0);
}

static int64_t x286_sys_xenix(struct x286_frame *f) {
    switch (f->sub) {
    case X286_XSYS_brkctl:  return x286_xsys_brkctl(f);
    case X286_XSYS_stkgrow: return x286_xsys_stkgrow(f);
    case X286_XSYS_ftime:   return x286_xsys_ftime(f);
    case X286_XSYS_nap:     return x286_xsys_nap(f);
    case X286_XSYS_rdchk:   return x286_xsys_rdchk(f);
    case X286_XSYS_chsize:  return x286_xsys_chsize(f);
    case X286_XSYS_locking: return 0;   /* advisory; we do not lock */
    default:                return -ENOSYS;
    }
}

/* ------------------------------------------------------------------ */
/* Dispatch table                                                      */
/* ------------------------------------------------------------------ */

typedef int64_t (*x286_callfn)(struct x286_frame *);

#define X286_CALL_MAX 64

static const x286_callfn x286_calls[X286_CALL_MAX] = {
    [X286_SYS_exit]    = x286_sys_exit,
    [X286_SYS_fork]    = x286_sys_fork,
    [X286_SYS_read]    = x286_sys_read,
    [X286_SYS_write]   = x286_sys_write,
    [X286_SYS_open]    = x286_sys_open,
    [X286_SYS_close]   = x286_sys_close,
    [X286_SYS_wait]    = x286_sys_wait,
    [X286_SYS_creat]   = x286_sys_creat,
    [X286_SYS_link]    = x286_sys_link,
    [X286_SYS_unlink]  = x286_sys_unlink,
    [X286_SYS_exec]    = x286_sys_exec,
    [X286_SYS_chdir]   = x286_sys_chdir,
    [X286_SYS_time]    = x286_sys_time,
    [X286_SYS_mknod]   = x286_sys_mknod,
    [X286_SYS_chmod]   = x286_sys_chmod,
    [X286_SYS_chown]   = x286_sys_chown,
    [X286_SYS_brk]     = x286_sys_brk,
    [X286_SYS_stat]    = x286_sys_stat,
    [X286_SYS_lseek]   = x286_sys_lseek,
    [X286_SYS_getpid]  = x286_sys_getpid,
    [X286_SYS_mount]   = x286_sys_mount,
    [X286_SYS_umount]  = x286_sys_umount,
    [X286_SYS_setuid]  = x286_sys_setuid,
    [X286_SYS_getuid]  = x286_sys_getuid,
    [X286_SYS_stime]   = x286_sys_stime,
    [X286_SYS_alarm]   = x286_sys_alarm,
    [X286_SYS_fstat]   = x286_sys_fstat,
    [X286_SYS_pause]   = x286_sys_pause,
    [X286_SYS_utime]   = x286_sys_utime,
    [X286_SYS_access]  = x286_sys_access,
    [X286_SYS_nice]    = x286_sys_nice,
    [X286_SYS_sync]    = x286_sys_sync,
    [X286_SYS_kill]    = x286_sys_kill,
    [X286_SYS_setpgrp] = x286_sys_setpgrp,
    [X286_SYS_xenix]   = x286_sys_xenix,
    [X286_SYS_dup]     = x286_sys_dup,
    [X286_SYS_pipe]    = x286_sys_pipe,
    [X286_SYS_times]   = x286_sys_times,
    [X286_SYS_setgid]  = x286_sys_setgid,
    [X286_SYS_getgid]  = x286_sys_getgid,
    [X286_SYS_signal]  = x286_sys_signal,
    [X286_SYS_acct]    = x286_sys_acct,
    [X286_SYS_ioctl]   = x286_sys_ioctl,
    [X286_SYS_utssys]  = x286_sys_utssys,
    [X286_SYS_execve]  = x286_sys_execve,
    [X286_SYS_umask]   = x286_sys_umask,
    [X286_SYS_chroot]  = x286_sys_chroot,
    [X286_SYS_fcntl]   = x286_sys_fcntl,
    [X286_SYS_ulimit]  = x286_sys_ulimit,
};

static const char *const x286_names[X286_CALL_MAX] = {
    [X286_SYS_exit] = "exit",       [X286_SYS_fork] = "fork",
    [X286_SYS_read] = "read",       [X286_SYS_write] = "write",
    [X286_SYS_open] = "open",       [X286_SYS_close] = "close",
    [X286_SYS_wait] = "wait",       [X286_SYS_creat] = "creat",
    [X286_SYS_link] = "link",       [X286_SYS_unlink] = "unlink",
    [X286_SYS_exec] = "exec",       [X286_SYS_chdir] = "chdir",
    [X286_SYS_time] = "time",       [X286_SYS_mknod] = "mknod",
    [X286_SYS_chmod] = "chmod",     [X286_SYS_chown] = "chown",
    [X286_SYS_brk] = "brk",         [X286_SYS_stat] = "stat",
    [X286_SYS_lseek] = "lseek",     [X286_SYS_getpid] = "getpid",
    [X286_SYS_mount] = "mount",     [X286_SYS_umount] = "umount",
    [X286_SYS_setuid] = "setuid",   [X286_SYS_getuid] = "getuid",
    [X286_SYS_stime] = "stime",     [X286_SYS_ptrace] = "ptrace",
    [X286_SYS_alarm] = "alarm",     [X286_SYS_fstat] = "fstat",
    [X286_SYS_pause] = "pause",     [X286_SYS_utime] = "utime",
    [X286_SYS_stty] = "stty",       [X286_SYS_gtty] = "gtty",
    [X286_SYS_access] = "access",   [X286_SYS_nice] = "nice",
    [X286_SYS_statfs] = "statfs",   [X286_SYS_sync] = "sync",
    [X286_SYS_kill] = "kill",       [X286_SYS_fstatfs] = "fstatfs",
    [X286_SYS_setpgrp] = "setpgrp", [X286_SYS_xenix] = "xenix",
    [X286_SYS_dup] = "dup",         [X286_SYS_pipe] = "pipe",
    [X286_SYS_times] = "times",     [X286_SYS_profil] = "profil",
    [X286_SYS_plock] = "plock",     [X286_SYS_setgid] = "setgid",
    [X286_SYS_getgid] = "getgid",   [X286_SYS_signal] = "signal",
    [X286_SYS_acct] = "acct",       [X286_SYS_ioctl] = "ioctl",
    [X286_SYS_uadmin] = "uadmin",   [X286_SYS_utssys] = "utssys",
    [X286_SYS_execve] = "execve",   [X286_SYS_umask] = "umask",
    [X286_SYS_chroot] = "chroot",   [X286_SYS_fcntl] = "fcntl",
    [X286_SYS_ulimit] = "ulimit",
};

static const char *const x286_xenix_names[] = {
    [X286_XSYS_locking] = "locking",   [X286_XSYS_creatsem] = "creatsem",
    [X286_XSYS_opensem] = "opensem",   [X286_XSYS_sigsem] = "sigsem",
    [X286_XSYS_waitsem] = "waitsem",   [X286_XSYS_nbwaitsem] = "nbwaitsem",
    [X286_XSYS_rdchk] = "rdchk",       [X286_XSYS_stkgrow] = "stkgrow",
    [X286_XSYS_chsize] = "chsize",     [X286_XSYS_ftime] = "ftime",
    [X286_XSYS_nap] = "nap",           [X286_XSYS_sdget] = "sdget",
    [X286_XSYS_sdfree] = "sdfree",     [X286_XSYS_sdenter] = "sdenter",
    [X286_XSYS_sdleave] = "sdleave",   [X286_XSYS_sdgetv] = "sdgetv",
    [X286_XSYS_sdwaitv] = "sdwaitv",   [X286_XSYS_brkctl] = "brkctl",
    [X286_XSYS_msgctl] = "msgctl",     [X286_XSYS_msgget] = "msgget",
    [X286_XSYS_msgsnd] = "msgsnd",     [X286_XSYS_msgrcv] = "msgrcv",
    [X286_XSYS_semctl] = "semctl",     [X286_XSYS_semget] = "semget",
    [X286_XSYS_semop] = "semop",       [X286_XSYS_shmctl] = "shmctl",
    [X286_XSYS_shmget] = "shmget",     [X286_XSYS_shmat] = "shmat",
    [X286_XSYS_proctl] = "proctl",     [X286_XSYS_execseg] = "execseg",
};

static const char *x286_call_name(unsigned int nr) {
    if (nr < X286_CALL_MAX && x286_names[nr]) {
        return x286_names[nr];
    }
    return NULL;
}

static const char *x286_xenix_name(unsigned int sub) {
    if (sub < (sizeof(x286_xenix_names) / sizeof(x286_xenix_names[0])) &&
        x286_xenix_names[sub]) {
        return x286_xenix_names[sub];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Trap entry                                                          */
/* ------------------------------------------------------------------ */

/* Is the faulting instruction `int $5`? */
static int x286_is_syscall_int(registers_t *regs) {
    uintptr_t linear_ip;
    uint8_t insn[X286_INT5_LEN];

    if (x286_seg_span((uint16_t)regs->cs, regs->eip, sizeof(insn),
                      &linear_ip) != 0) {
        return 0;
    }
    if (linear_ip >= USER32_VA_END) {
        return 0;
    }
    if (copyin((const void *)linear_ip, insn, sizeof(insn)) != 0) {
        return 0;
    }
    return insn[0] == 0xCDU && insn[1] == X286_SYSCALL_VEC;
}

/*
 * Microsoft 8087-emulator fixup.
 *
 * Xenix binaries that do floating point do not contain x87 instructions.  The
 * compiler emitted them, but the linker rewrote each one into a two-byte
 * software interrupt so the program would also run on a machine with no
 * coprocessor, where a trap handler emulates it in software:
 *
 *     INT 0xF0+n   <- ESC 0xD8+n       the eight x87 opcodes
 *     INT 0xF8     <- a raw ESC instruction follows, run it as-is
 *     INT 0xF9     <- FWAIT
 *
 * The ModR/M and displacement bytes are left exactly as the compiler emitted
 * them and follow the INT, so `fldcw [addr]` becomes CD F1 2E <addr> -- which
 * is why every FP-using binary died at the identical byte pattern CD F1 2E,
 * the fldcw in its runtime's FP init, before main ever ran.  See the 8087(HW)
 * manual page on the distribution media.
 *
 * The encoding is two bytes for a one-byte opcode on purpose: it leaves room
 * to patch the instruction back to its native form in place when a
 * coprocessor IS present.  `9B <ESC>` -- FWAIT then the real opcode -- is
 * exactly two bytes and leaves the operands where they already are.  So that
 * is what this does, once per site, and the CPU runs the program's own
 * floating point from then on.  substrate has a real x87 with lazy switching
 * (fpu_init clears CR0.EM), so the first patched instruction takes an #NM,
 * the FPU handler loads this process's state, and it proceeds.
 *
 * Patching is safe here: xout.c maps text private and writable for exactly
 * this kind of thing, so a write COWs at worst and never touches another
 * process's copy.
 *
 * Returns 1 when the instruction was rewritten, and the caller must then
 * return to user mode WITHOUT advancing eip so the patched form executes.
 */
#define X286_FPU_ESC_FIRST  0xF0U   /* INT 0xF0..0xF7 == ESC 0xD8..0xDF */
#define X286_FPU_ESC_LAST   0xF7U
#define X286_FPU_RAW_ESC    0xF8U   /* a real ESC instruction follows */
#define X286_FPU_WAIT       0xF9U   /* FWAIT */

static int x286_fixup_fpu_insn(registers_t *regs) {
    uintptr_t linear;
    uint8_t insn[2];
    uint8_t patch[2];

    if (x286_seg_span((uint16_t)regs->cs, regs->eip, sizeof(insn),
                      &linear) != 0) {
        return 0;
    }
    if (linear >= USER32_VA_END) {
        return 0;
    }
    if (copyin((const void *)linear, insn, sizeof(insn)) != 0) {
        return 0;
    }
    if (insn[0] != 0xCDU || insn[1] < X286_FPU_ESC_FIRST) {
        return 0;
    }

    patch[0] = 0x9BU;   /* FWAIT */
    if (insn[1] <= X286_FPU_ESC_LAST) {
        patch[1] = (uint8_t)(0xD8U + (insn[1] - X286_FPU_ESC_FIRST));
    } else if (insn[1] == X286_FPU_RAW_ESC || insn[1] == X286_FPU_WAIT) {
        /* Nothing of our own to run: for 0xF8 the real ESC is the next
         * instruction and the CPU reaches it on its own, and 0xF9 is just the
         * wait.  Either way FWAIT + NOP is the faithful two-byte native form. */
        patch[1] = 0x90U;   /* NOP */
    } else {
        /* 0xFA..0xFF are the emulator's own control entries (startup, far
         * call thunks).  They are rare -- one or two sites in a whole binary
         * -- and nothing here knows what they mean, so leave them to fault
         * rather than guess and corrupt the program quietly. */
        if (x286_trace_enabled()) {
            char buf[96];

            snprintf(buf, sizeof(buf),
                     "X286: unhandled 8087-emulator INT %#04x at %#x:%#x\n",
                     insn[1], (unsigned)(regs->cs & 0xFFFFU),
                     (unsigned)regs->eip);
            kprint(buf);
        }
        return 0;
    }

    if (copyout(patch, (void *)linear, sizeof(patch)) != 0) {
        return 0;
    }
    return 1;
}

static int x286_handle_trap(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    struct x286_frame f;
    x286_callfn fn;
    void *saved_syscall_regs;
    int64_t ret;

    if (!regs || !current_process ||
        current_process->perso_id != PERS_XENIX ||
        !current_process->ldt) {
        return 0;
    }
    /* An `int $5` from CPL 3 against a DPL 0 gate raises #GP with the IDT
     * bit set in the error code; #NP is possible if the gate were absent. */
    if (regs->int_no != 13 && regs->int_no != 11) {
        return 0;
    }
    if (!x286_is_syscall_int(regs)) {
        /* Not a system call.  The other thing that traps here is a floating
         * point instruction the linker left in its emulator form; rewrite it
         * and re-execute at the same eip. */
        return x286_fixup_fpu_insn(regs);
    }

    memset(&f, 0, sizeof(f));
    f.regs = regs;
    f.nr   = (uint16_t)(regs->eax & 0xFFU);
    f.sub  = (uint16_t)((regs->eax >> 8) & 0xFFU);
    f.bx   = (uint16_t)(regs->ebx & 0xFFFFU);
    f.cx   = (uint16_t)(regs->ecx & 0xFFFFU);
    f.si   = (uint16_t)(regs->esi & 0xFFFFU);
    f.di   = (uint16_t)(regs->edi & 0xFFFFU);
    f.ds   = (uint16_t)regs->ds;
    f.es   = (uint16_t)regs->es;
    f.ss   = (uint16_t)regs->ss;

    if (current_thread && current_thread->proc == current_process) {
        current_thread->syscall_num = f.nr;
    }

    /* Step past the trap *before* dispatching: fork(2) copies this frame
     * into the child, and execve(2) never comes back to fix it up. */
    regs->eip += X286_INT5_LEN;

    /* fork(2) reaches for current_thread->syscall_regs to find the frame to
     * clone, and only the int 0x80 path sets it -- point it at this trap's
     * frame for the duration of the call, then put it back so nothing else
     * mistakes an emulated Xenix trap for a native syscall in progress. */
    saved_syscall_regs = current_thread ? current_thread->syscall_regs : NULL;
    if (current_thread) {
        current_thread->syscall_regs = regs;
    }

    if (x286_entry_trace_enabled()) {
        char buf[128];
        const char *name = x286_call_name(f.nr);

        snprintf(buf, sizeof(buf), "X286> [%d] %s.%u(%#x, %#x, %#x, %#x)\n",
                 current_process ? (int)current_process->pid : -1,
                 name ? name : "?", f.sub, f.bx, f.cx, f.si, f.di);
        kprint(buf);
    }

    fn = (f.nr < X286_CALL_MAX) ? x286_calls[f.nr] : NULL;
    ret = fn ? fn(&f) : -ENOSYS;

    if (current_thread) {
        current_thread->syscall_regs = saved_syscall_regs;
    }

    if (x286_trace_enabled()) {
        char buf[160];
        const char *name = x286_call_name(f.nr);

        int pid = current_process ? (int)current_process->pid : -1;

        if (f.nr == X286_SYS_xenix) {
            const char *sub = x286_xenix_name(f.sub);

            snprintf(buf, sizeof(buf),
                     "X286: [%d] xenix.%s(%#x, %#x, %#x, %#x) = %lld\n",
                     pid, sub ? sub : "?", f.bx, f.cx, f.si, f.di,
                     (long long)ret);
        } else if (name) {
            snprintf(buf, sizeof(buf),
                     "X286: [%d] %s(%#x, %#x, %#x, %#x) = %lld\n",
                     pid, name, f.bx, f.cx, f.si, f.di, (long long)ret);
        } else {
            snprintf(buf, sizeof(buf),
                     "X286: [%d] sys%u.%u(%#x, %#x, %#x, %#x) = %lld\n",
                     pid, f.nr, f.sub, f.bx, f.cx, f.si, f.di, (long long)ret);
        }
        kprint(buf);
    }

    /* Carry set with the errno in AX, or AX:BX holding the result. */
    if (ret < 0) {
        regs->eax = (regs->eax & 0xFFFF0000U) | ((uint32_t)(-ret) & 0xFFFFU);
        regs->eflags |= X286_EFLAGS_CF;
    } else {
        regs->eax = (regs->eax & 0xFFFF0000U) |
                    ((uint32_t)ret & 0xFFFFU);
        regs->ebx = (regs->ebx & 0xFFFF0000U) |
                    (((uint32_t)ret >> 16) & 0xFFFFU);
        regs->eflags &= ~X286_EFLAGS_CF;
    }
    return 1;
}

/*
 * Signal delivery, V7 style -- which is what Xenix/286 is.
 *
 * x286_sys_signal parks the handler's far pointer in the native disposition,
 * so it arrives here as (selector << 16) | offset.  Entering it means pushing
 * a 16-bit far-call frame onto the program's own stack:
 *
 *      SP+4  signo          (the handler's int argument)
 *      SP+2  interrupted CS \  the "return address" -- the handler's lret
 *      SP+0  interrupted IP /  resumes the interrupted instruction directly
 *
 * There is no trampoline and no saved register block, because V7 had none:
 * the handler is an ordinary C function, so it preserves what the ABI says
 * it must, and everything else is understood to be clobbered.  The Xenix
 * libc always registers a far thunk (its signal(2) stub passes %cs as the
 * selector even in small model), so a far frame is right for both models.
 */
static int x286_native_to_xenix_sig(int sig) {
    int i;

    for (i = 1; i < X286_NSIG; i++) {
        if (x286_to_native_sig[i] == (uint8_t)sig) {
            return i;
        }
    }
    return sig;
}

static void x286_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags,
                         void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    uint32_t far_handler = (uint32_t)(uintptr_t)handler;
    uint16_t sel = (uint16_t)(far_handler >> 16);
    uint16_t off = (uint16_t)far_handler;
    uint16_t sp;
    uintptr_t linear;
    uint16_t frame[3];

    (void)mask; (void)flags;

    if (!regs || !current_process) {
        return;
    }
    /* A handler with no selector is not something we can far-call into. */
    if (sel == 0 || !x286_ldt_entry(sel)) {
        sigexit(current_process, SIGILL);
        return;
    }

    sp = (uint16_t)(regs->useresp & 0xFFFFU);
    if (sp < sizeof(frame)) {
        sigexit(current_process, SIGSEGV);
        return;
    }
    sp = (uint16_t)(sp - sizeof(frame));

    frame[0] = (uint16_t)regs->eip;             /* return offset */
    frame[1] = (uint16_t)regs->cs;              /* return selector */
    frame[2] = (uint16_t)x286_native_to_xenix_sig(sig);

    if (x286_seg_span((uint16_t)regs->ss, sp, sizeof(frame), &linear) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }
    memcpy((void *)linear, frame, sizeof(frame));

    if (x286_trace_enabled()) {
        char buf[128];

        snprintf(buf, sizeof(buf),
                 "X286: [%d] deliver sig %d -> %04x:%04x (resume %04x:%04x)\n",
                 (int)current_process->pid, frame[2], sel, off,
                 (unsigned int)regs->cs, (unsigned int)regs->eip);
        kprint(buf);
    }

    regs->useresp = (regs->useresp & 0xFFFF0000U) | sp;
    regs->cs = sel;
    regs->eip = off;
}


/* =====================================================================
 * The 32-bit half: 80386 programs.
 *
 * Same system, same call numbers, same structures -- but a different way
 * in and a flat address space (see exec/formats/xout.c).  A libc stub is
 *
 *     _open:  mov   $5,%eax
 *             lcall $7,$0
 *             jb    cerror
 *             ret
 *
 * so the number is in EAX (with the cxenix sub-function in AH, as in the
 * 16-bit half) and the arguments are the caller's cdecl words, still on the
 * stack above the stub's return address.  Carry reports failure with the
 * errno in EAX.  A second result comes back in EDX: the parent's pid from
 * getpid, the effective id from getuid/getgid, the status from wait, the
 * write end from pipe -- and from fork, zero in the parent and non-zero in
 * the child, which is the opposite of what the 16-bit stub tests for.
 *
 * Substrate has no call gate at LDT selector 7, so the lcall faults and
 * arrives here by way of the #GP/#NP handler.
 *
 * Pointers are plain addresses.  The structures are the 16-bit half's with
 * the compiler's natural alignment, which changes only struct stat.
 * ===================================================================== */

/* struct stat: seven 16-bit fields, two bytes of padding, four longs. */
struct x386_stat {
    int16_t  st_dev;
    uint16_t st_ino;
    uint16_t st_mode;
    int16_t  st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    int16_t  st_rdev;
    uint16_t pad;
    int32_t  st_size;
    int32_t  st_atime;
    int32_t  st_mtime;
    int32_t  st_ctime;
};

/* kern_sigprocmask's `how`. */
#define X386_MASK_BLOCK    1
#define X386_MASK_UNBLOCK  2
#define X386_MASK_SET      3

/* signal(2)'s first argument carries the System V.3 variants in its
 * second byte: signal, sigset, sighold, sigrelse, sigignore, sigpause. */
#define X386_SIGNO_MASK    0x00FFU
#define X386_SIG_SET       0x0100U
#define X386_SIG_HOLD      0x0200U
#define X386_SIG_RELSE     0x0400U
#define X386_SIG_IGNORE    0x0800U
#define X386_SIG_PAUSE     0x1000U
#define X386_SIG_HOLDVAL   2U         /* SIG_HOLD, as a disposition */

/* The whole of [addr, addr+len) is below the top of user space. */
int x386_span(uint32_t addr, uint32_t len) {
    if (addr >= USER32_VA_END || len > USER32_VA_END - addr) {
        return -EFAULT;
    }
    return 0;
}

void x386_free_string(char *s) {
    x286_free_string(s);
}

int x386_string(uint32_t addr, char **out) {
    size_t len = 0;
    char *copy;

    *out = NULL;
    if (addr == 0 || x386_span(addr, 1) != 0) {
        return -EFAULT;
    }
    if (copyinstr((const void *)(uintptr_t)addr, NULL, X286_PATH_MAX,
                  &len) != 0 || len == 0) {
        return -ENAMETOOLONG;
    }
    copy = kmalloc(len);
    if (!copy) {
        return -ENOMEM;
    }
    if (copyin((const void *)(uintptr_t)addr, copy, len) != 0) {
        kfree(copy, len);
        return -EFAULT;
    }
    copy[len - 1U] = '\0';
    *out = copy;
    return 0;
}

/* Call `fn` on the path at `addr`. */
static int64_t x386_path1(uint32_t addr, int (*fn)(const char *)) {
    char *path = NULL;
    int rc = x386_string(addr, &path);

    if (rc == 0) {
        rc = fn(path);
        x286_free_string(path);
    }
    return rc;
}

static int64_t x386_sys_exit(struct x386_frame *f) {
    return sys_exit((int)f->a[0]);
}

static int64_t x386_sys_fork(struct x386_frame *f) {
    registers_t *regs = f->regs;
    uint32_t saved_edx = regs->edx;
    uint32_t saved_eflags = regs->eflags;
    int pid;

    /* The child resumes from a copy of this frame with only EAX forced to
     * zero, so what tells it that it is the child has to be in the frame
     * before the fork: EDX non-zero, carry clear. */
    regs->edx = 1;
    regs->eflags &= ~XENIX_EFLAGS_CF;
    pid = sys_fork();
    regs->edx = saved_edx;
    regs->eflags = saved_eflags;

    if (pid < 0) {
        return pid;
    }
    return (int64_t)(uint32_t)pid;   /* EDX = 0: the parent */
}

static int64_t x386_sys_read(struct x386_frame *f) {
    int fd = (int)f->a[0];
    fs_node_t *node;
    int64_t rv;

    if (x386_span(f->a[1], f->a[2]) != 0) {
        return -EFAULT;
    }
    node = x286_fd_vnode(fd);
    if (node && (node->flags & 0x7) == FS_DIRECTORY) {
        return x286_read_directory(fd, (uintptr_t)f->a[1], f->a[2]);
    }
    rv = kern_read(fd, (char *)(uintptr_t)f->a[1], (size_t)f->a[2]);
    /* O_NDELAY reports "nothing yet" as a zero-length read. */
    return rv == -EAGAIN ? 0 : rv;
}

static int64_t x386_sys_write(struct x386_frame *f) {
    if (x386_span(f->a[1], f->a[2]) != 0) {
        return -EFAULT;
    }
    return kern_write((int)f->a[0], (const char *)(uintptr_t)f->a[1],
                      (size_t)f->a[2]);
}

static int64_t x386_sys_open(struct x386_frame *f) {
    char *path = NULL;
    int rc = x386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_open(path, x286_open_flags((uint16_t)f->a[1]), (int)f->a[2]);
    x286_free_string(path);
    return rc;
}

static int64_t x386_sys_close(struct x386_frame *f) {
    return kern_close((int)f->a[0]);
}

static int64_t x386_sys_wait(struct x386_frame *f) {
    int status = 0;
    int pid;

    (void)f;
    pid = kern_waitpid(-1, &status, 0);
    if (pid < 0) {
        return pid;
    }
    return (int64_t)((uint64_t)(uint32_t)pid |
                     ((uint64_t)(uint32_t)(status & 0xFFFF) << 32));
}

static int64_t x386_sys_creat(struct x386_frame *f) {
    char *path = NULL;
    int rc = x386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_open(path, O_WRONLY | O_CREAT | O_TRUNC, (int)f->a[1]);
    x286_free_string(path);
    return rc;
}

static int64_t x386_sys_link(struct x386_frame *f) {
    char *oldp = NULL, *newp = NULL;
    int rc = x386_string(f->a[0], &oldp);

    if (rc == 0) {
        rc = x386_string(f->a[1], &newp);
    }
    if (rc == 0) {
        rc = kern_link(oldp, newp);
    }
    x286_free_string(oldp);
    x286_free_string(newp);
    return rc;
}

static int64_t x386_sys_unlink(struct x386_frame *f) {
    return x386_path1(f->a[0], kern_unlink);
}

static int64_t x386_sys_chdir(struct x386_frame *f) {
    return x386_path1(f->a[0], kern_chdir);
}

static int64_t x386_sys_chroot(struct x386_frame *f) {
    return x386_path1(f->a[0], kern_chroot);
}

static int64_t x386_sys_time(struct x386_frame *f) {
    (void)f;
    return (int64_t)(uint32_t)kern_time(NULL);
}

static int64_t x386_sys_mknod(struct x386_frame *f) {
    char *path = NULL;
    int rc = x386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = sys_mknod(path, (int)f->a[1], (int)f->a[2]);
    x286_free_string(path);
    return rc;
}

static int64_t x386_sys_chmod(struct x386_frame *f) {
    char *path = NULL;
    int rc = x386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = sys_chmod(path, (int)f->a[1]);
    x286_free_string(path);
    return rc;
}

static int64_t x386_sys_chown(struct x386_frame *f) {
    char *path = NULL;
    int rc = x386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = sys_chown(path, (int)f->a[1], (int)f->a[2]);
    x286_free_string(path);
    return rc;
}

static int64_t x386_sys_access(struct x386_frame *f) {
    char *path = NULL;
    int rc = x386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_access(path, (int)f->a[1]);
    x286_free_string(path);
    return rc;
}

/*
 * brk(2) takes the new break as an address and returns 0.  sys_brk reports
 * failure the native way, by leaving the break where it was.
 */
static int64_t x386_sys_brk(struct x386_frame *f) {
    uint32_t want = f->a[0];
    uint32_t got = (uint32_t)(uintptr_t)sys_brk((void *)(uintptr_t)want);

    return got == want ? 0 : -ENOMEM;
}

static int64_t x386_put_stat(const struct stat *native, uint32_t dst) {
    struct x386_stat out;

    memset(&out, 0, sizeof(out));
    out.st_dev   = (int16_t)native->st_dev;
    out.st_ino   = (uint16_t)native->st_ino;
    out.st_mode  = (uint16_t)native->st_mode;
    out.st_nlink = (int16_t)native->st_nlink;
    out.st_uid   = (uint16_t)native->st_uid;
    out.st_gid   = (uint16_t)native->st_gid;
    out.st_rdev  = (int16_t)native->st_rdev;
    out.st_size  = (int32_t)native->st_size;
    out.st_atime = (int32_t)native->st_atime;
    out.st_mtime = (int32_t)native->st_mtime;
    out.st_ctime = (int32_t)native->st_ctime;
    if (x386_span(dst, sizeof(out)) != 0 ||
        copyout(&out, (void *)(uintptr_t)dst, sizeof(out)) != 0) {
        return -EFAULT;
    }
    return 0;
}

static int64_t x386_sys_stat(struct x386_frame *f) {
    char *path = NULL;
    struct stat native;
    int rc = x386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_stat(path, &native);
    x286_free_string(path);
    return rc != 0 ? rc : x386_put_stat(&native, f->a[1]);
}

static int64_t x386_sys_fstat(struct x386_frame *f) {
    struct stat native;
    int rc = kern_fstat((int)f->a[0], &native);

    return rc != 0 ? rc : x386_put_stat(&native, f->a[1]);
}

static int64_t x386_sys_lseek(struct x386_frame *f) {
    int64_t off = (int64_t)(int32_t)f->a[1];   /* off_t is a signed long */
    int64_t rc = sys_lseek((int)f->a[0], (uint32_t)off,
                           (uint32_t)((uint64_t)off >> 32), (int)f->a[2]);

    return rc < 0 ? rc : (int64_t)(uint32_t)rc;
}

/* Two results: the first in EAX, the second in EDX. */
int64_t x386_pair(uint32_t first, uint32_t second) {
    return (int64_t)((uint64_t)first | ((uint64_t)second << 32));
}

static int64_t x386_sys_getpid(struct x386_frame *f) {
    (void)f;
    return x386_pair((uint32_t)sys_getpid(), (uint32_t)sys_getppid());
}

static int64_t x386_sys_getuid(struct x386_frame *f) {
    (void)f;
    return x386_pair((uint32_t)sys_getuid() & 0xFFFFU,
                     (uint32_t)sys_geteuid() & 0xFFFFU);
}

static int64_t x386_sys_getgid(struct x386_frame *f) {
    (void)f;
    return x386_pair((uint32_t)sys_getgid() & 0xFFFFU,
                     (uint32_t)sys_getegid() & 0xFFFFU);
}

static int64_t x386_sys_setuid(struct x386_frame *f) {
    return sys_setuid((int)f->a[0]);
}

static int64_t x386_sys_setgid(struct x386_frame *f) {
    return sys_setgid((int)f->a[0]);
}

static int64_t x386_sys_alarm(struct x386_frame *f) {
    return (int64_t)(uint32_t)sys_alarm((unsigned int)f->a[0]);
}

static int64_t x386_sys_pause(struct x386_frame *f) {
    (void)f;
    return sys_pause();
}

static int64_t x386_sys_nice(struct x386_frame *f) {
    return sys_nice((int)f->a[0]);
}

static int64_t x386_sys_sync(struct x386_frame *f) {
    (void)f;
    return sys_sync();
}

static int64_t x386_sys_kill(struct x386_frame *f) {
    int sig = 0;

    if (f->a[1] != 0) {
        sig = x286_signo((uint16_t)f->a[1]);
        if (sig < 0) {
            return sig;
        }
    }
    return sys_kill((int)f->a[0], sig);
}

static int64_t x386_sys_setpgrp(struct x386_frame *f) {
    /* setpgrp(flag): non-zero makes the caller a group leader; either way
     * the result is the process group. */
    if (f->a[0] != 0) {
        (void)sys_setpgid(0, 0);
    }
    return sys_getpgrp();
}

static int64_t x386_sys_dup(struct x386_frame *f) {
    return sys_dup((int)f->a[0]);
}

static int64_t x386_sys_pipe(struct x386_frame *f) {
    int fds[2] = { -1, -1 };
    int rc;

    (void)f;
    rc = kern_pipe(fds);
    if (rc < 0) {
        return rc;
    }
    return x386_pair((uint32_t)fds[0], (uint32_t)fds[1]);
}

static int64_t x386_sys_times(struct x386_frame *f) {
    struct tms native;
    struct x286_tms out;
    clock_t rc;

    memset(&native, 0, sizeof(native));
    rc = kern_times(&native);
    if ((int32_t)rc < 0) {
        return (int64_t)(int32_t)rc;
    }
    out.tms_utime  = (int32_t)native.tms_utime;
    out.tms_stime  = (int32_t)native.tms_stime;
    out.tms_cutime = (int32_t)native.tms_cutime;
    out.tms_cstime = (int32_t)native.tms_cstime;
    if (x386_span(f->a[0], sizeof(out)) != 0 ||
        copyout(&out, (void *)(uintptr_t)f->a[0], sizeof(out)) != 0) {
        return -EFAULT;
    }
    return (int64_t)(uint32_t)rc;
}

static int64_t x386_sys_umask(struct x386_frame *f) {
    return sys_umask((int)f->a[0]);
}

static int64_t x386_sys_ulimit(struct x386_frame *f) {
    int rc = sys_ulimit((int)f->a[0], (long)(int32_t)f->a[1]);

    return rc < 0 ? rc : (int64_t)(uint32_t)rc;
}

static int64_t x386_sys_fcntl(struct x386_frame *f) {
    int fd = (int)f->a[0];
    int rc;

    switch (f->a[1]) {
    case F_DUPFD:
    case F_GETFD:
    case F_SETFD:
        return sys_fcntl(fd, (int)f->a[1], (int)f->a[2]);
    case F_GETFL:
        rc = sys_fcntl(fd, F_GETFL, 0);
        return rc < 0 ? rc : (int64_t)x286_from_open_flags(rc);
    case F_SETFL:
        return sys_fcntl(fd, F_SETFL, x286_open_flags((uint16_t)f->a[2]));
    default:
        return -EINVAL;
    }
}

static int64_t x386_sys_ioctl(struct x386_frame *f) {
    int fd = (int)f->a[0];
    uint32_t cmd = f->a[1];
    uint32_t arg = f->a[2];
    struct termios native;
    struct x286_termio user;
    int rc;

    switch (cmd) {
    case X286_TCGETA:
        rc = kern_ioctl(fd, TCGETS, &native);
        if (rc != 0) {
            return rc;
        }
        x286_termios_to_termio(&user, &native);
        if (x386_span(arg, sizeof(user)) != 0 ||
            copyout(&user, (void *)(uintptr_t)arg, sizeof(user)) != 0) {
            return -EFAULT;
        }
        return 0;
    case X286_TCSETA:
    case X286_TCSETAW:
    case X286_TCSETAF:
        /* termio cannot express everything termios holds: start from what
         * the terminal has. */
        rc = kern_ioctl(fd, TCGETS, &native);
        if (rc != 0) {
            return rc;
        }
        if (x386_span(arg, sizeof(user)) != 0 ||
            copyin((const void *)(uintptr_t)arg, &user, sizeof(user)) != 0) {
            return -EFAULT;
        }
        x286_termio_to_termios(&native, &user);
        return kern_ioctl(fd, cmd == X286_TCSETA ? TCSETS :
                              cmd == X286_TCSETAW ? TCSETSW : TCSETSF,
                          &native);
    case X286_TCSBRK:
        return kern_ioctl(fd, TCSBRK, (void *)(uintptr_t)arg);
    case X286_TCXONC:
        return kern_ioctl(fd, TCXONC, (void *)(uintptr_t)arg);
    case X286_TCFLSH:
        return kern_ioctl(fd, TCFLSH, (void *)(uintptr_t)arg);
    default:
        /* Let the driver decide; an unknown request comes back ENOTTY,
         * which is what a program probing for a capability expects. */
        if (arg >= USER32_VA_END) {
            arg = 0;
        }
        return kern_ioctl(fd, cmd, (void *)(uintptr_t)arg);
    }
}

/* utssys(buf, mv, type): type 0 is uname. */
static int64_t x386_sys_utssys(struct x386_frame *f) {
    struct utsname native;
    struct x286_utsname out;
    int rc;

    if (f->a[2] != 0) {
        return -EINVAL;   /* ustat / fusers */
    }
    memset(&native, 0, sizeof(native));
    rc = kern_uname(&native);
    if (rc != 0) {
        return rc;
    }
    memset(&out, 0, sizeof(out));
    strlcpy(out.sysname, "Xenix", sizeof(out.sysname));
    strlcpy(out.nodename, native.nodename, sizeof(out.nodename));
    strlcpy(out.release, "2.3", sizeof(out.release));
    strlcpy(out.version, "2", sizeof(out.version));
    strlcpy(out.machine, "i386", sizeof(out.machine));
    if (x386_span(f->a[0], sizeof(out)) != 0 ||
        copyout(&out, (void *)(uintptr_t)f->a[0], sizeof(out)) != 0) {
        return -EFAULT;
    }
    return 0;
}

/* A NULL-terminated array of 32-bit pointers to strings. */
static int x386_copy_vector(uint32_t addr, char ***out, size_t *slots_out) {
    char **vec;
    size_t n = 0, i;

    *out = NULL;
    *slots_out = 0;
    if (addr == 0) {
        return 0;
    }
    for (;;) {
        uint32_t p;

        if (n >= X286_MAX_VEC) {
            return -E2BIG;
        }
        if (x386_span(addr + (uint32_t)n * 4U, 4) != 0 ||
            copyin((const void *)(uintptr_t)(addr + (uint32_t)n * 4U), &p,
                   sizeof(p)) != 0) {
            return -EFAULT;
        }
        if (p == 0) {
            break;
        }
        n++;
    }
    vec = kmalloc((n + 1U) * sizeof(char *));
    if (!vec) {
        return -ENOMEM;
    }
    memset(vec, 0, (n + 1U) * sizeof(char *));
    for (i = 0; i < n; i++) {
        uint32_t p = 0;
        int rc = copyin((const void *)(uintptr_t)(addr + (uint32_t)i * 4U),
                        &p, sizeof(p)) != 0 ? -EFAULT
                                            : x386_string(p, &vec[i]);

        if (rc != 0) {
            x286_free_vector(vec, n + 1U);
            return rc;
        }
    }
    *out = vec;
    *slots_out = n + 1U;
    return 0;
}

static int64_t x386_sys_execve(struct x386_frame *f) {
    char *path = NULL;
    char **argv = NULL, **envp = NULL;
    size_t argv_slots = 0, envp_slots = 0;
    int rc = x386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = x386_copy_vector(f->a[1], &argv, &argv_slots);
    if (rc == 0) {
        rc = x386_copy_vector(f->a[2], &envp, &envp_slots);
    }
    if (rc == 0) {
        rc = kern_execve(path, argv, envp);
    }
    x286_free_vector(argv, argv_slots);
    x286_free_vector(envp, envp_slots);
    x286_free_string(path);
    return rc;
}

static int64_t x386_sys_exec(struct x386_frame *f) {
    /* exec(path, argv), from before there was an environment to pass. */
    struct x386_frame local = *f;

    local.a[2] = 0;
    return x386_sys_execve(&local);
}

/*
 * signal(2), and the System V.3 calls that share its number.
 *
 * The stub hands over more than its arguments: EDX holds the address of
 * libc's return trampoline,
 *
 *     add   $4,%esp          ; drop the signal number
 *     lcall $0xf,$0          ; and return from the signal
 *
 * which is where a handler has to return to.  It is kept per signal, in the
 * slot the Linux personality uses for the same purpose (sa_restorer).
 */
static uint32_t x386_native_handler(uint32_t h, int *flags) {
    *flags = 0;
    if (h == X286_SIG_DFL) {
        return (uint32_t)(uintptr_t)SIG_DFL;
    }
    if (h == X286_SIG_IGN) {
        return (uint32_t)(uintptr_t)SIG_IGN;
    }
    return h;
}

static int64_t x386_sys_signal(struct x386_frame *f) {
    uint32_t variant = f->a[0] & ~X386_SIGNO_MASK;
    int sig = x286_signo((uint16_t)(f->a[0] & X386_SIGNO_MASK));
    struct sigaction act, old;
    uint32_t bit, handler;
    int flags, rc;

    if (sig < 0) {
        return sig;
    }
    bit = 1U << (sig - 1);

    switch (variant) {
    case X386_SIG_HOLD:
        return kern_sigprocmask(X386_MASK_BLOCK, &bit, NULL);
    case X386_SIG_RELSE:
        return kern_sigprocmask(X386_MASK_UNBLOCK, &bit, NULL);
    case X386_SIG_PAUSE: {
        uint32_t mask = 0;

        (void)kern_sigprocmask(X386_MASK_BLOCK, NULL, &mask);
        mask &= ~bit;
        return kern_sigsuspend(&mask);
    }
    case X386_SIG_IGNORE:
        memset(&act, 0, sizeof(act));
        act.sa_handler = (void *)SIG_IGN;
        return kern_sigaction(sig, &act, NULL);
    case 0:
    case X386_SIG_SET:
        break;
    default:
        return -EINVAL;
    }

    memset(&act, 0, sizeof(act));
    memset(&old, 0, sizeof(old));
    if (variant == X386_SIG_SET && f->a[1] == X386_SIG_HOLDVAL) {
        /* sigset(sig, SIG_HOLD): block it, leave the disposition. */
        rc = kern_sigaction(sig, NULL, &old);
        if (rc == 0) {
            rc = kern_sigprocmask(X386_MASK_BLOCK, &bit, NULL);
        }
    } else {
        act.sa_handler = (void *)(uintptr_t)x386_native_handler(f->a[1],
                                                                &flags);
        /* signal(): the disposition reverts as the handler is entered.
         * sigset(): it stays, and the signal is held while it runs. */
        if (variant == 0 && f->a[1] > X286_SIG_IGN) {
            act.sa_flags = SA_RESETHAND | SA_NODEFER;
        }
        rc = kern_sigaction(sig, &act, &old);
        if (rc == 0 && current_process) {
            current_process->linux_sig_restorer[sig - 1] =
                (void *)(uintptr_t)f->regs->edx;
        }
    }
    if (rc != 0) {
        return rc;
    }

    handler = (uint32_t)(uintptr_t)old.sa_handler;
    if (handler == (uint32_t)(uintptr_t)SIG_DFL) {
        return X286_SIG_DFL;
    }
    if (handler == (uint32_t)(uintptr_t)SIG_IGN) {
        return X286_SIG_IGN;
    }
    return (int64_t)handler;
}

/* ---- call 40, the Xenix multiplexer ---------------------------------- */

static int64_t x386_xsys_rdchk(struct x386_frame *f) {
    struct pollfd pfd;
    int rc;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = (int)f->a[0];
    pfd.events = POLLIN;
    rc = kern_poll(&pfd, 1, 0);
    if (rc < 0) {
        return rc;
    }
    return (rc > 0 && (pfd.revents & (POLLIN | POLLHUP))) ? 1 : 0;
}

static int64_t x386_xsys_chsize(struct x386_frame *f) {
    return sys_ftruncate((int)f->a[0], f->a[1], 0);
}

static int64_t x386_xsys_ftime(struct x386_frame *f) {
    struct x286_timeb out;
    struct timeval tv;
    int rc = kern_gettimeofday(&tv, NULL);

    if (rc != 0) {
        return rc;
    }
    memset(&out, 0, sizeof(out));
    out.time = (int32_t)tv.tv_sec;
    out.millitm = (uint16_t)(tv.tv_usec / 1000);
    if (x386_span(f->a[0], sizeof(out)) != 0 ||
        copyout(&out, (void *)(uintptr_t)f->a[0], sizeof(out)) != 0) {
        return -EFAULT;
    }
    return 0;
}

static int64_t x386_sys_xenix(struct x386_frame *f) {
    switch (f->sub) {
    case X286_XSYS_rdchk:
        return x386_xsys_rdchk(f);
    case X286_XSYS_chsize:
        return x386_xsys_chsize(f);
    case X286_XSYS_ftime:
        return x386_xsys_ftime(f);
    default:
        return -EINVAL;
    }
}

typedef int64_t (*x386_callfn)(struct x386_frame *);

static const x386_callfn x386_calls[X286_CALL_MAX] = {
    [X286_SYS_exit]    = x386_sys_exit,
    [X286_SYS_fork]    = x386_sys_fork,
    [X286_SYS_read]    = x386_sys_read,
    [X286_SYS_write]   = x386_sys_write,
    [X286_SYS_open]    = x386_sys_open,
    [X286_SYS_close]   = x386_sys_close,
    [X286_SYS_wait]    = x386_sys_wait,
    [X286_SYS_creat]   = x386_sys_creat,
    [X286_SYS_link]    = x386_sys_link,
    [X286_SYS_unlink]  = x386_sys_unlink,
    [X286_SYS_exec]    = x386_sys_exec,
    [X286_SYS_chdir]   = x386_sys_chdir,
    [X286_SYS_time]    = x386_sys_time,
    [X286_SYS_mknod]   = x386_sys_mknod,
    [X286_SYS_chmod]   = x386_sys_chmod,
    [X286_SYS_chown]   = x386_sys_chown,
    [X286_SYS_brk]     = x386_sys_brk,
    [X286_SYS_stat]    = x386_sys_stat,
    [X286_SYS_lseek]   = x386_sys_lseek,
    [X286_SYS_getpid]  = x386_sys_getpid,
    [X286_SYS_setuid]  = x386_sys_setuid,
    [X286_SYS_getuid]  = x386_sys_getuid,
    [X286_SYS_alarm]   = x386_sys_alarm,
    [X286_SYS_fstat]   = x386_sys_fstat,
    [X286_SYS_pause]   = x386_sys_pause,
    [X286_SYS_access]  = x386_sys_access,
    [X286_SYS_nice]    = x386_sys_nice,
    [X286_SYS_sync]    = x386_sys_sync,
    [X286_SYS_kill]    = x386_sys_kill,
    [X286_SYS_setpgrp] = x386_sys_setpgrp,
    [X286_SYS_xenix]   = x386_sys_xenix,
    [X286_SYS_dup]     = x386_sys_dup,
    [X286_SYS_pipe]    = x386_sys_pipe,
    [X286_SYS_times]   = x386_sys_times,
    [X286_SYS_setgid]  = x386_sys_setgid,
    [X286_SYS_getgid]  = x386_sys_getgid,
    [X286_SYS_signal]  = x386_sys_signal,
    [X286_SYS_ioctl]   = x386_sys_ioctl,
    [X286_SYS_utssys]  = x386_sys_utssys,
    [X286_SYS_execve]  = x386_sys_execve,
    [X286_SYS_umask]   = x386_sys_umask,
    [X286_SYS_chroot]  = x386_sys_chroot,
    [X286_SYS_fcntl]   = x386_sys_fcntl,
    [X286_SYS_ulimit]  = x386_sys_ulimit,
};

int64_t xenix386_call(struct x386_frame *f, int *known) {
    x386_callfn fn = (f->nr < X286_CALL_MAX) ? x386_calls[f->nr] : NULL;

    *known = fn != NULL;
    return fn ? fn(f) : -EINVAL;
}

static int x386_syscall(registers_t *regs) {
    return sysv386_syscall(regs, xenix386_call, "XENIX", xenix_trace_enabled());
}

int sysv386_syscall(registers_t *regs, sysv386_callfn call, const char *tag,
                    int trace) {
    struct x386_frame f;
    uintptr_t linear_sp;
    int known = 1;
    void *saved_syscall_regs;
    int64_t ret;

    memset(&f, 0, sizeof(f));
    f.regs = regs;
    f.nr   = regs->eax & 0xFFU;
    f.sub  = (regs->eax >> 8) & 0xFFU;

    /* The arguments are the caller's, above the stub's return address. */
    if (xenix_seg_to_linear((uint16_t)regs->ss, regs->useresp,
                            &linear_sp) == 0 &&
        linear_sp < USER32_VA_END - sizeof(uint32_t) * 7U) {
        unsigned int i;

        for (i = 0; i < 6U; i++) {
            if (copyin((const void *)(linear_sp + (i + 1U) * sizeof(uint32_t)),
                       &f.a[i], sizeof(uint32_t)) != 0) {
                break;
            }
        }
    }

    if (current_thread && current_thread->proc == current_process) {
        current_thread->syscall_num = f.nr;
    }

    /* Step past the lcall before dispatching: fork copies this frame into
     * the child, and execve does not come back to fix it up. */
    regs->eip += XENIX_LCALL_LEN;

    /* fork finds the frame to clone through syscall_regs, which only the
     * native entry path sets. */
    saved_syscall_regs = current_thread ? current_thread->syscall_regs : NULL;
    if (current_thread) {
        current_thread->syscall_regs = regs;
    }

    ret = call(&f, &known);

    if (current_thread) {
        current_thread->syscall_regs = saved_syscall_regs;
    }

    if (trace) {
        char buf[160];
        const char *name = x286_call_name((uint16_t)f.nr);
        int pid = current_process ? (int)current_process->pid : -1;

        if (f.nr == X286_SYS_xenix) {
            const char *sub = x286_xenix_name((uint16_t)f.sub);

            snprintf(buf, sizeof(buf),
                     "%s: [%d] xenix.%s/%u(%#x, %#x, %#x) = %lld%s\n", tag,
                     pid, sub ? sub : "?", f.sub, f.a[0], f.a[1], f.a[2],
                     (long long)(ret < 0 ? ret : (int64_t)(uint32_t)ret),
                     known ? "" : " [unimplemented]");
        } else {
            snprintf(buf, sizeof(buf),
                     "%s: [%d] %s/%u(%#x, %#x, %#x, %#x) = %lld%s\n", tag,
                     pid, name ? name : "sys", f.nr, f.a[0], f.a[1], f.a[2],
                     f.a[3],
                     (long long)(ret < 0 ? ret : (int64_t)(uint32_t)ret),
                     known ? "" : " [unimplemented]");
        }
        kprint(buf);
    }

    /* Carry and an errno, or the result in EAX with its second half, if it
     * has one, in EDX. */
    if (ret < 0) {
        regs->eax = (uint32_t)(-ret);
        regs->eflags |= XENIX_EFLAGS_CF;
    } else {
        regs->eax = (uint32_t)ret;
        regs->edx = (uint32_t)((uint64_t)ret >> 32);
        regs->eflags &= ~XENIX_EFLAGS_CF;
    }
    return 1;
}

/*
 * Signal delivery.  The handler is entered as handler(signo) with libc's
 * trampoline for a return address, and below that -- where the trampoline's
 * `lcall $0xf,$0` finds it at the top of the stack -- what x386_sigreturn
 * needs to put the interrupted program back:
 *
 *      ESP+0   trampoline
 *      ESP+4   signo
 *      ESP+8   struct x386_sigcontext
 *
 * The context is this kernel's own business: libc never looks inside it.
 */
struct x386_sigcontext {
    uint32_t magic;
    uint32_t eip, eflags, esp;
    uint32_t eax, ecx, edx, ebx, ebp, esi, edi;
    uint32_t mask;
};
#define X386_SIGCTX_MAGIC  0x58334753U   /* "SG3X" */
/* The flags a program may set for itself: CF PF AF ZF SF TF DF OF. */
#define X386_EFLAGS_USER   0x00000DD5U

/* The most argument words a handler is entered with: System V Release 4's
 * (signo, siginfo, ucontext). */
#define X386_SIG_MAXARGS 3U

struct x386_sigframe {
    uint32_t ret;
    uint32_t arg[X386_SIG_MAXARGS];
    struct x386_sigcontext ctx;
};

static void x386_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags,
                         void *regs_ptr) {
    uint32_t tramp = 0;

    (void)flags;
    if (!regs_ptr || !current_process) {
        return;
    }
    if (sig >= 1 && sig <= NSIG) {
        tramp = (uint32_t)(uintptr_t)
                current_process->linux_sig_restorer[sig - 1];
    }
    sysv386_sendsig(handler, (uint32_t)x286_native_to_xenix_sig(sig), mask,
                    (registers_t *)regs_ptr, tramp, 1);
}

void sysv386_sendsig(void *handler, uint32_t signo, uint32_t mask,
                     registers_t *regs, uint32_t tramp, unsigned int nargs) {
    struct x386_sigframe frame;
    uint32_t size, sp;

    if (!regs || !current_process || nargs < 1U || nargs > X386_SIG_MAXARGS) {
        return;
    }
    /* No trampoline means no way back out of the handler. */
    if (tramp == 0) {
        sigexit(current_process, SIGILL);
        return;
    }

    /* The frame is laid out for the most arguments; with fewer, the
     * context moves up to follow the last of them. */
    memset(&frame, 0, sizeof(frame));
    frame.ret = tramp;
    frame.arg[0] = signo;
    frame.ctx.magic = X386_SIGCTX_MAGIC;
    frame.ctx.eip = regs->eip;
    frame.ctx.eflags = regs->eflags;
    frame.ctx.esp = regs->useresp;
    frame.ctx.eax = regs->eax;
    frame.ctx.ecx = regs->ecx;
    frame.ctx.edx = regs->edx;
    frame.ctx.ebx = regs->ebx;
    frame.ctx.ebp = regs->ebp;
    frame.ctx.esi = regs->esi;
    frame.ctx.edi = regs->edi;
    frame.ctx.mask = mask;

    size = (1U + nargs) * (uint32_t)sizeof(uint32_t) +
           (uint32_t)sizeof(frame.ctx);
    sp = (regs->useresp - size) & ~3U;
    if (x386_span(sp, size) != 0 ||
        copyout(&frame, (void *)(uintptr_t)sp,
                (1U + nargs) * sizeof(uint32_t)) != 0 ||
        copyout(&frame.ctx,
                (void *)(uintptr_t)(sp + (1U + nargs) * sizeof(uint32_t)),
                sizeof(frame.ctx)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    regs->useresp = sp;
    regs->eip = (uint32_t)(uintptr_t)handler;
    regs->eflags &= ~0x00000400U;   /* DF clear on entry to C */
}

int sysv386_sigreturn(registers_t *regs, unsigned int skip) {
    struct x386_sigcontext ctx;
    uint32_t at = regs->useresp + skip * (uint32_t)sizeof(uint32_t);

    if (x386_span(at, sizeof(ctx)) != 0 ||
        copyin((const void *)(uintptr_t)at, &ctx, sizeof(ctx)) != 0 ||
        ctx.magic != X386_SIGCTX_MAGIC) {
        sigexit(current_process, SIGSEGV);
        return 1;
    }
    regs->eip = ctx.eip;
    regs->useresp = ctx.esp;
    regs->eflags = (regs->eflags & ~X386_EFLAGS_USER) |
                   (ctx.eflags & X386_EFLAGS_USER);
    regs->eax = ctx.eax;
    regs->ecx = ctx.ecx;
    regs->edx = ctx.edx;
    regs->ebx = ctx.ebx;
    regs->ebp = ctx.ebp;
    regs->esi = ctx.esi;
    regs->edi = ctx.edi;
    (void)kern_sigprocmask(X386_MASK_SET, &ctx.mask, NULL);
    return 1;
}

/* =====================================================================
 * One personality.
 *
 * Named "Xenix" and rooted at /perso/xenix.  A Xenix/386 installation is a
 * single tree holding 386 x.out binaries beside 286 and 8086 ones, and a
 * program of one kind execs another without knowing; the x.out header picks
 * the loader, the loader sets the process's bitness, and the hooks below
 * send a trap or a signal to the half that understands the process.
 *
 * Neither half is driven by syscall_table: Xenix calls arrive as faults
 * (`lcall $7,$0`, `int $5`) and each half decodes its own.  The table is
 * what an `int $0x80` from a Xenix process would be dispatched through,
 * which no Xenix program issues.
 */
static int xenix_is_16bit(void) {
    return current_process && current_process->bitness == BITNESS_16;
}

static int xenix_handle_trap(void *regs_ptr) {
    if (xenix_is_16bit()) {
        return x286_handle_trap(regs_ptr);
    }
    return xenix386_handle_trap(regs_ptr);
}

static void xenix_sendsig(void *handler, int sig, uint32_t mask,
                          uint32_t flags, void *regs) {
    if (xenix_is_16bit()) {
        x286_sendsig(handler, sig, mask, flags, regs);
        return;
    }
    x386_sendsig(handler, sig, mask, flags, regs);
}

struct personality personality_xenix = {
    .name = "Xenix",
    .id = PERS_XENIX,
    .syscall_table = xenix_syscalls,
    .syscall_names = xenix_names,
    .syscall_fmts = NULL,
    .syscall_count = MAX_SYSCALLS,
    .path_prefix = "/perso/xenix",
    .sendsig = xenix_sendsig,
    .handle_trap = xenix_handle_trap,
};
