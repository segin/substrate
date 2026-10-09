/*
 * perso_xenix.c - the Xenix personality.
 *
 * One personality for every x.out program (exec/formats/xout.c), in two
 * halves, because Xenix has two system-call ABIs: the 8086/80286 one
 * (`int $5`), which is most of this file, and the 80386 one
 * (`lcall $7,$0`), after it.  At the end is the single struct personality,
 * whose trap and signal hooks hand each process to the half its bitness
 * names.
 *
 * The 80386 convention and the calls made through it are also UNIX System
 * V/386's, so they live in perso_sysv386.c; the 32-bit half here is what
 * Xenix adds to that.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <machine/gdt.h>
#include <machine/idt.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>
#include <exec/formats/elks_aout.h>
#include <exec/formats/xout.h>
#include <exec/perso/personality.h>
#include <exec/perso/svr3/svr3_syscalls.h>
#include <exec/perso/sysv386.h>
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
#include <sys/namei.h>
#include <sys/param.h>
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

/* What Xenix/386 brings to the shared entry; defined with the 32-bit
 * half, below. */
static const struct sysv386_abi xenix386_abi;

static int xenix386_handle_trap(void *regs_ptr) {
    if (!regs_ptr || !current_process ||
        current_process->perso_id != PERS_XENIX ||
        !current_process->ldt) {
        return 0;
    }
    return sysv386_handle_trap((registers_t *)regs_ptr, &xenix386_abi);
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

/*
 * The four argument slots are named for the registers a small-data program
 * passes them in.  Each holds one 16-bit word of the argument list -- a long
 * takes two -- except that a pointer is always ONE slot, whichever way it
 * came: a near pointer is its offset, with nothing above, and a far one (a
 * large-data program's) is X286_FAR(selector, offset).  x286_ptr_sel() gives
 * the segment of either, so a call's code does not ask which it has.
 */
struct x286_frame {
    registers_t *regs;
    uint16_t nr;    /* AL: the System V call number */
    uint16_t sub;   /* AH: sub-function, for the multiplexed calls */
    uint32_t bx;    /* arg1 */
    uint32_t cx;    /* arg2 */
    uint32_t si;    /* arg3 */
    uint32_t di;    /* arg4 */
    uint16_t ds;
    uint16_t es;
    uint16_t ss;
    uint8_t ldata;  /* a large-data program: see x286_unpack_block() */
};

#define X286_FAR(sel, off) (((uint32_t)(uint16_t)(sel) << 16) | (uint16_t)(off))

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

/*
 * The segment a pointer argument is in: its own if it is far, the program's
 * current DS if it is near.  No selector of a process's is 0, so 0 above the
 * offset is what says near.
 */
static uint16_t x286_ptr_sel(const struct x286_frame *f, uint32_t ptr) {
    uint16_t sel = (uint16_t)(ptr >> 16);

    return sel != 0 ? sel : f->ds;
}

/* A pointer argument, near or far, and the bytes it points at. */
static int x286_ds_span(const struct x286_frame *f, uint32_t offset,
                        size_t size, uintptr_t *linear_out) {
    return x286_seg_span(x286_ptr_sel(f, offset), offset, size, linear_out);
}

/*
 * Copy a NUL-terminated string out of DS into freshly allocated kernel
 * memory.  Callers hand the result to the kern_* entry points, which want
 * kernel pointers; plain data buffers are passed through as user linear
 * addresses instead and never copied.
 */
static int x286_ds_string(const struct x286_frame *f, uint32_t offset,
                          char **out) {
    const gdt_entry_t *entry = x286_ldt_entry(x286_ptr_sel(f, offset));
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
/* The structure and its conversions are exec/perso/sysv386.h's: struct
 * termio is laid out the same at 16 bits as at 32. */

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

/*
 * Directories, before there was mkdir(2).
 *
 * Version 7, System III and Xenix/286 have no call that makes or removes
 * a directory.  mkdir(1), a set-uid program, makes the node with mknod(2)
 * and then its two entries with link(2):
 *
 *     mknod("d", S_IFDIR | mode, 0);  link("d", "d/.");  link(parent, "d/..");
 *
 * and rmdir(1) takes it apart the same way, unlink("d/.."), unlink("d/.")
 * and unlink("d").  A directory here comes with both entries and loses
 * them with itself, so the node is made by mkdir, the links and unlinks
 * of "." and ".." succeed when the directory is there and do nothing, and
 * unlink of a directory removes it.
 */

/* Is the last component of `path` "." or ".."? */
static int x286_names_dot_entry(const char *path) {
    const char *leaf = strrchr(path, '/');

    leaf = leaf ? leaf + 1 : path;
    return leaf[0] == '.' &&
           (leaf[1] == '\0' || (leaf[1] == '.' && leaf[2] == '\0'));
}

/* 0 if `path`, ending in "." or "..", names a directory. */
static int x286_dot_entry_exists(const char *path) {
    struct stat st;
    int rc = kern_stat(path, &st);

    if (rc != 0) {
        return rc;
    }
    return S_ISDIR(st.st_mode) ? 0 : -ENOTDIR;
}

static int64_t x286_sys_link(struct x286_frame *f) {
    char *oldp = NULL, *newp = NULL;
    int rc = x286_ds_string(f, f->bx, &oldp);

    if (rc != 0) {
        return rc;
    }
    rc = x286_ds_string(f, f->cx, &newp);
    if (rc == 0) {
        /* mkdir(1) finishes a directory with link(dir, "dir/.") and
         * link(parent, "dir/.."); x286_sys_mknod has made both. */
        rc = x286_names_dot_entry(newp) ? x286_dot_entry_exists(newp)
                                        : kern_link(oldp, newp);
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
    if (x286_names_dot_entry(path)) {
        rc = x286_dot_entry_exists(path);       /* rmdir(1): see above */
    } else {
        struct stat st;

        rc = (kern_lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
            ? kern_rmdir(path) : kern_unlink(path);
    }
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
    /* `path` is the kernel's copy; the sys_ form would copy it in again
     * as a user string and fail.  The same for chmod, chown and utime. */
    /* A directory is asked for as S_IFDIR here, and by Venix with the
     * Sixth Edition's 0140000; either way the bit below is set and the
     * character-device bit is not. */
    if ((f->cx & 0060000U) == 0040000U) {
        rc = kern_mkdir(path, (int)(f->cx & 07777U));
    } else {
        rc = kern_mknod(path, (int)f->cx, (int)(int16_t)f->si);
    }
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_chmod(struct x286_frame *f) {
    char *path = NULL;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_chmodat(AT_FDCWD, path, (int)f->cx, 0);
    x286_free_string(path);
    return rc;
}

static int64_t x286_sys_chown(struct x286_frame *f) {
    char *path = NULL;
    int rc = x286_ds_string(f, f->bx, &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_fchownat(AT_FDCWD, path, (int)(int16_t)f->cx,
                       (int)(int16_t)f->si, 0);
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
    /* struct utimbuf is two 32-bit times here; none means now.  The
     * kernel sets times by path only for a user's string, so the file is
     * opened and they are set through the descriptor. */
    {
        struct timespec ts[2];
        int fd = kern_open(path, O_RDONLY, 0);

        x286_free_string(path);
        if (fd < 0) {
            return fd;
        }
        if (times != 0) {
            int32_t t[2];

            memcpy(t, (const void *)times, sizeof(t));
            memset(ts, 0, sizeof(ts));
            ts[0].tv_sec = t[0];
            ts[1].tv_sec = t[1];
        }
        rc = kern_utimensat(fd, NULL, times != 0 ? ts : NULL, 0);
        kern_close(fd);
    }
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
 * signal(2).  CX is the offset of what is to be entered, 0 for SIG_DFL
 * and 1 for SIG_IGN.  What a program passes is never its C function but
 * an entry in a table of trampolines in its C library, one for each
 * signal, which is how the library's code finds out which signal it was;
 * the function itself the library keeps.
 *
 * A program of the large text model says which segment the offset is in,
 * in SI.  One of the small model says nothing: it has one text segment,
 * its library's signal() leaves SI as it found it, and what is in SI is
 * whatever the caller was keeping there.  So SI is believed only when it
 * names a code segment of the program, and otherwise the segment is the
 * one the call was made from.  (In a small-model program a code segment
 * SI names by accident is the one segment there is.)
 *
 * The far pointer is kept as the native disposition, and x286_sendsig
 * below enters it.
 */
#define X286_SIG_DFL  0U
#define X286_SIG_IGN  1U

static int x286_is_code_selector(uint16_t sel) {
    const gdt_entry_t *e = x286_ldt_entry(sel);

    return e != NULL && (e->access & 0x08U) != 0U;      /* executable */
}

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

    if (f->cx == X286_SIG_DFL) {
        act.sa_handler = (void *)SIG_DFL;
    } else if (f->cx == X286_SIG_IGN) {
        act.sa_handler = (void *)SIG_IGN;
    } else {
        uint16_t sel = x286_is_code_selector(f->si) ? f->si : (uint16_t)f->regs->cs;

        act.sa_handler = (void *)(uintptr_t)(((uint32_t)sel << 16) |
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
 * struct differs too -- see sysv_termios_to_termio.
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
    struct sysv_termio user;
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
        sysv_termios_to_termio(&user, &native);
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
    sysv_termio_to_termios(&native, &user);

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

/*
 * Pull a NULL-terminated array of pointers to strings out of the program:
 * 16-bit offsets in a small-data program, and offset then selector, four
 * bytes each, in a large-data one.
 */
#define X286_MAX_VEC 256

static int x286_copy_vector(struct x286_frame *f, uint32_t off, char ***out,
                            size_t *slots_out) {
    const size_t words = f->ldata ? 2U : 1U;
    const uint16_t sel = x286_ptr_sel(f, off);
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
    off &= 0xFFFFU;
    rc = x286_seg_span(sel, off, words * sizeof(uint16_t), &linear);
    if (rc != 0) {
        return rc;
    }
    src = (const uint16_t *)linear;
    while (count < X286_MAX_VEC) {
        uint32_t at = off + (uint32_t)(count * words * sizeof(uint16_t));

        if (at > 0xFFFFU ||
            x286_seg_span(sel, at, words * sizeof(uint16_t), NULL) != 0) {
            return -EFAULT;
        }
        if (src[count * words] == 0 &&
            (words == 1U || src[count * words + 1U] == 0)) {
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
        rc = x286_ds_string(f, words == 1U ? src[i]
                                : X286_FAR(src[i * 2U + 1U], src[i * 2U]),
                            &vec[i]);
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

/*
 * Record locks: fcntl(fd, F_GETLK / F_SETLK / F_SETLKW, struct flock *).
 *
 * They do not come by the fcntl call.  Xenix's C library sends them to the
 * Xenix locking call, whose own modes are 0 to 4, as modes 5, 6 and 7 --
 * numbered as the fcntl commands are, and as substrate's -- with a FAR
 * pointer to the structure whatever the program's model: offset in the
 * third argument and selector in the fourth, which is also where a
 * large-data program's two words of it are read to.
 *
 * The structure is Xenix/286's, and its l_type values Xenix's own.
 */
#define X286_F_UNLCK 0
#define X286_F_WRLCK 1
#define X286_F_RDLCK 3

struct x286_flock {
    int16_t l_type;
    int16_t l_whence;
    int32_t l_start;
    int32_t l_len;
    int16_t l_pid;
    int16_t l_sysid;
} __attribute__((packed));

static int64_t x286_record_lock(struct x286_frame *f, int fd, int cmd) {
    uint32_t ptr = X286_FAR(f->di, f->si);
    struct x286_flock xf;
    struct kflock kf;
    uintptr_t user;
    int rc = x286_ds_span(f, ptr, sizeof(xf), &user);

    if (rc != 0) {
        return rc;
    }
    memcpy(&xf, (const void *)user, sizeof(xf));
    memset(&kf, 0, sizeof(kf));
    kf.l_whence = xf.l_whence;
    kf.l_start = xf.l_start;
    kf.l_len = xf.l_len;
    kf.l_pid = xf.l_pid;
    switch (xf.l_type) {
    case X286_F_RDLCK: kf.l_type = F_RDLCK; break;
    case X286_F_WRLCK: kf.l_type = F_WRLCK; break;
    case X286_F_UNLCK: kf.l_type = F_UNLCK; break;
    default:           return -EINVAL;
    }
    rc = proc_advlock(current_process, fd, cmd, &kf);
    if (rc != 0 || cmd != F_GETLK) {
        return rc;
    }
    switch (kf.l_type) {
    case F_RDLCK: xf.l_type = X286_F_RDLCK; break;
    case F_WRLCK: xf.l_type = X286_F_WRLCK; break;
    default:      xf.l_type = X286_F_UNLCK; break;
    }
    xf.l_whence = kf.l_whence;
    xf.l_start = (int32_t)kf.l_start;
    xf.l_len = (int32_t)kf.l_len;
    xf.l_pid = (int16_t)kf.l_pid;
    xf.l_sysid = 0;
    memcpy((void *)user, &xf, sizeof(xf));
    return 0;
}

static int64_t x286_sys_fcntl(struct x286_frame *f) {
    int fd = (int)(int16_t)f->bx;
    int rc;

    /* The command numbers match substrate's, but F_GETFL/F_SETFL carry
     * *open flags*, and Xenix's are the System V values -- O_NDELAY is 0004
     * there and 0x800 here.  Passing them through untranslated silently
     * dropped O_NDELAY, which turned a program's polling read of the
     * keyboard into a blocking one.  The record-locking commands do not
     * arrive here: see x286_record_lock(). */
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
    /* The fourth argument is a far pointer, of which only the segment is
     * wanted.  A small-data program's stub leaves the selector alone in
     * the slot; a large-data one's has it above the offset. */
    uint16_t sel = f->ldata ? (uint16_t)(f->di >> 16) : (uint16_t)f->di;

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
    case X286_XSYS_locking:
        /* fcntl's record locks, by this door; locking(S)'s own modes are
         * advisory, and we do not lock for them. */
        if (f->cx == F_GETLK || f->cx == F_SETLK || f->cx == F_SETLKW) {
            return x286_record_lock(f, (int)(int16_t)f->bx, (int)f->cx);
        }
        return 0;
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

/*
 * Which of a call's argument slots are pointers: bit N for slot N.  Only a
 * large-data program needs telling -- there a pointer is two words of the
 * argument block and everything else one -- and x286_unpack_block() is the
 * one reader.  A call not here has no pointers among its arguments.
 *
 * signal is not here, though its second argument is a far pointer: its
 * code takes the offset and the segment as two slots, in either model.
 */
#define X286_P0 0x01U
#define X286_P1 0x02U
#define X286_P2 0x04U
#define X286_P3 0x08U

static const uint8_t x286_ptr_args[X286_CALL_MAX] = {
    [X286_SYS_read]   = X286_P1,            [X286_SYS_write]  = X286_P1,
    [X286_SYS_open]   = X286_P0,            [X286_SYS_creat]  = X286_P0,
    [X286_SYS_link]   = X286_P0 | X286_P1,  [X286_SYS_unlink] = X286_P0,
    [X286_SYS_exec]   = X286_P0 | X286_P1,  [X286_SYS_chdir]  = X286_P0,
    [X286_SYS_mknod]  = X286_P0,            [X286_SYS_chmod]  = X286_P0,
    [X286_SYS_chown]  = X286_P0,
    [X286_SYS_stat]   = X286_P0 | X286_P1,
    [X286_SYS_mount]  = X286_P0 | X286_P1,  [X286_SYS_umount] = X286_P0,
    [X286_SYS_fstat]  = X286_P1,
    [X286_SYS_utime]  = X286_P0 | X286_P1,  [X286_SYS_access] = X286_P0,
    [X286_SYS_times]  = X286_P0,            [X286_SYS_acct]   = X286_P0,
    [X286_SYS_ioctl]  = X286_P2,            [X286_SYS_utssys] = X286_P0,
    [X286_SYS_execve] = X286_P0 | X286_P1 | X286_P2,
    [X286_SYS_chroot] = X286_P0,
};

static const uint8_t x286_xenix_ptr_args[] = {
    [X286_XSYS_creatsem] = X286_P0,   [X286_XSYS_opensem] = X286_P0,
    [X286_XSYS_ftime]    = X286_P0,   [X286_XSYS_sdget]   = X286_P0,
    [X286_XSYS_brkctl]   = X286_P3,
};

/*
 * A large-data program's arguments.  Its C library pushes them as for any
 * call of a C function -- the first lowest, a pointer as offset then
 * selector, a long as its low word then its high -- and traps with BX
 * pointing at the first, in the stack segment.  A small-data program's has
 * them in BX, CX, SI and DI, a word each, which is what the frame was
 * filled from and what every call's code reads; so the block is read into
 * the same four slots, a pointer taking one of them whole.
 *
 * Words the stack segment ends before are read as 0: a call with one
 * argument, made from the top of the stack, has nothing above it.
 */
static void x286_unpack_block(struct x286_frame *f) {
    uint32_t *const slot[4] = { &f->bx, &f->cx, &f->si, &f->di };
    uint32_t at = f->bx & 0xFFFFU;
    unsigned int ptrs = 0;
    unsigned int i;

    if (f->nr == X286_SYS_xenix) {
        if (f->sub < sizeof(x286_xenix_ptr_args)) {
            ptrs = x286_xenix_ptr_args[f->sub];
        }
    } else if (f->nr < X286_CALL_MAX) {
        ptrs = x286_ptr_args[f->nr];
    }

    for (i = 0; i < 4U; i++) {
        uint16_t word[2] = { 0, 0 };
        unsigned int n = (ptrs & (1U << i)) ? 2U : 1U;
        unsigned int w;

        for (w = 0; w < n; w++, at += sizeof(uint16_t)) {
            uintptr_t src;

            if (at <= 0xFFFFU &&
                x286_seg_span(f->ss, at, sizeof(uint16_t), &src) == 0) {
                memcpy(&word[w], (const void *)src, sizeof(uint16_t));
            }
        }
        *slot[i] = (n == 2U) ? X286_FAR(word[1], word[0]) : word[0];
    }
}

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
    f.ldata = current_process->x286_ldata;
    if (f.ldata) {
        x286_unpack_block(&f);
    }

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
 * Signal delivery.
 *
 * x286_sys_signal keeps what is to be entered as (selector << 16) | offset.
 * It is entered as an interrupt is, with this on the program's own stack:
 *
 *      SP+4  FLAGS
 *      SP+2  interrupted CS
 *      SP+0  interrupted IP
 *
 * and nothing else: no signal number, and no block of saved registers.
 * What is entered is the C library's trampoline for the signal -- a
 * `call` to code the library has in common for all of them -- and that
 * code tells the signal from which trampoline called it, saves the
 * registers itself, calls the program's function, puts the registers
 * back, and returns over this frame: with `iret` in a large-model
 * library, and in a small-model one by popping the three words and
 * jumping to the first, which comes to the same in a program with one
 * text segment.  This is read out of the two libraries (libc's signal.o
 * as linked by the system's own cc, with and without -Ml); a frame of
 * return address and signal number, which this used to push, sent the
 * large-model code back to the right place with the signal number for
 * its flags and one word too many on the stack, and the small-model
 * code nowhere at all.
 *
 * Neither return passes through the kernel, so nothing here can be
 * undone on the way back.  The signal is therefore not left blocked for
 * the length of its handler: it would stay blocked for good, and be
 * delivered once in the life of the process.  Xenix does not block it in
 * any case; it resets the disposition, which the handler sets again.
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

    (void)flags;

    if (!regs || !current_process) {
        return;
    }
    if (current_thread) {
        current_thread->sig_mask = mask;
    }
    /* What is not a code segment of the program cannot be entered. */
    if (!x286_is_code_selector(sel)) {
        sigexit(current_process, SIGILL);
        return;
    }

    sp = (uint16_t)(regs->useresp & 0xFFFFU);
    if (sp < sizeof(frame)) {
        sigexit(current_process, SIGSEGV);
        return;
    }
    sp = (uint16_t)(sp - sizeof(frame));

    frame[0] = (uint16_t)regs->eip;
    frame[1] = (uint16_t)regs->cs;
    frame[2] = (uint16_t)regs->eflags;

    if (x286_seg_span((uint16_t)regs->ss, sp, sizeof(frame), &linear) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }
    memcpy((void *)linear, frame, sizeof(frame));

    if (x286_trace_enabled()) {
        char buf[128];

        snprintf(buf, sizeof(buf),
                 "X286: [%d] deliver sig %d -> %04x:%04x (resume %04x:%04x)\n",
                 (int)current_process->pid, x286_native_to_xenix_sig(sig), sel, off,
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
 * in and a flat address space (see exec/formats/xout.c): `lcall $7,$0`,
 * arguments on the stack, a second result in EDX.  That convention, and
 * the calls made through it, Xenix/386 has in common with UNIX System
 * V/386, and they are written once in perso_sysv386.c.  What is here is
 * what Xenix brings to them (xenix386_abi): its signal and open-flag
 * numbering, which are the 16-bit half's; read(2) on a directory; the
 * ioctl requests it passes to the driver; and call 40, the Xenix
 * multiplexer.
 * ===================================================================== */

/* read(2) on a directory returns V7 directory records, as it does for a
 * 16-bit program. */
static int xenix386_read_dir(int fd, uint32_t dst, uint32_t count,
                             int64_t *result) {
    fs_node_t *node = x286_fd_vnode(fd);

    if (!node || (node->flags & 0x7) != FS_DIRECTORY) {
        return 0;
    }
    *result = x286_read_directory(fd, (uintptr_t)dst, count);
    return 1;
}

/*
 * ioctl(2).  The termio requests are the shared code's.  Anything else is
 * handed to the driver as it is: an unknown request comes back ENOTTY,
 * which is what a program probing for a capability expects.
 */
static int64_t xenix386_sys_ioctl(struct sysv386_frame *f, int *known) {
    uint32_t arg = f->a[2];

    if (f->a[1] >= SYSV_TCGETA && f->a[1] <= SYSV_TCFLSH) {
        *known = 0;
        return 0;
    }
    if (arg >= USER32_VA_END) {
        arg = 0;
    }
    return kern_ioctl((int)f->a[0], f->a[1], (void *)(uintptr_t)arg);
}

/* ---- call 40, the Xenix multiplexer ---------------------------------- */

static int64_t xenix386_xsys_rdchk(struct sysv386_frame *f) {
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

static int64_t xenix386_xsys_chsize(struct sysv386_frame *f) {
    return sys_ftruncate((int)f->a[0], f->a[1], 0);
}

static int64_t xenix386_xsys_ftime(struct sysv386_frame *f) {
    struct x286_timeb out;
    struct timeval tv;
    int rc = kern_gettimeofday(&tv, NULL);

    if (rc != 0) {
        return rc;
    }
    memset(&out, 0, sizeof(out));
    out.time = (int32_t)tv.tv_sec;
    out.millitm = (uint16_t)(tv.tv_usec / 1000);
    if (sysv386_span(f->a[0], sizeof(out)) != 0 ||
        copyout(&out, (void *)(uintptr_t)f->a[0], sizeof(out)) != 0) {
        return -EFAULT;
    }
    return 0;
}

static int64_t xenix386_sys_xenix(struct sysv386_frame *f) {
    switch (f->sub) {
    case X286_XSYS_rdchk:
        return xenix386_xsys_rdchk(f);
    case X286_XSYS_chsize:
        return xenix386_xsys_chsize(f);
    case X286_XSYS_ftime:
        return xenix386_xsys_ftime(f);
    default:
        return -EINVAL;
    }
}

/* Xenix's own calls; the rest are the shared ones. */
static int64_t xenix386_call(struct sysv386_frame *f, int *known) {
    switch (f->nr) {
    case X286_SYS_xenix:
        return xenix386_sys_xenix(f);
    case X286_SYS_ioctl:
        return xenix386_sys_ioctl(f, known);
    default:
        *known = 0;
        return 0;
    }
}

static const char *xenix386_call_name(unsigned int nr, unsigned int sub) {
    if (nr == X286_SYS_xenix) {
        const char *name = x286_xenix_name(sub);

        return name ? name : "xenix";
    }
    return x286_call_name(nr);
}

static int xenix386_signo(uint32_t sig) {
    return x286_signo((uint16_t)sig);
}

static uint32_t xenix386_signo_from(int sig) {
    return (uint32_t)x286_native_to_xenix_sig(sig);
}

static int xenix386_open_flags(uint32_t flags) {
    return x286_open_flags((uint16_t)flags);
}

static uint32_t xenix386_from_open_flags(int flags) {
    return x286_from_open_flags(flags);
}

static const struct sysv386_abi xenix386_abi = {
    .tag = "XENIX",
    .trace = xenix_trace_enabled,
    .call_name = xenix386_call_name,
    .call = xenix386_call,
    .nosys = EINVAL,
    .fix_errno = NULL,
    .signo = xenix386_signo,
    .signo_from = xenix386_signo_from,
    .sig_args = 1,                 /* handler(signo) */
    .open_flags = xenix386_open_flags,
    .from_open_flags = xenix386_from_open_flags,
    .read_dir = xenix386_read_dir,
    .sysname = "Xenix",
    .release = "2.3",
    .version = "2",
    .machine = "i386",
};

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
    (void)flags;
    sysv386_sendsig(&xenix386_abi, handler, sig, mask, (registers_t *)regs);
}

/* =====================================================================
 * PC/IX: IBM's and INTERACTIVE's System III for the 8088 (1984).
 *
 * It is here because it is the same system one release back.  Xenix/286
 * numbers its calls as System V does and System V numbers them as System
 * III did; the structures a call fills in -- stat, the 16-byte directory
 * entry read(2) returns, termio, tms, utsname -- are the ones above; and a
 * program is one text and one data segment of at most 64K each, addressed
 * through the process's local descriptor table.  So a PC/IX call is run by
 * the Xenix/286 implementation of it, and what is PC/IX's own is how a call
 * gets here and what a few of them do with their arguments.  All of that is
 * written down on the distribution media, in /usr/include/sys.s:
 *
 *   - Call N is `int 0x80+N`: a vector each, no number in a register.
 *     Vectors above 0x80 have DPL 0 gates, so the instruction raises #GP
 *     and is decoded here (CD 8N), as Xenix's `int $5` is.
 *   - The arguments are on the stack as for a C call, the caller having
 *     pushed a word where a return address would be: argument 0 is at
 *     SS:SP+2.  Xenix's stub loads the first four such words into BX, CX,
 *     SI and DI, so those four words are a struct x286_frame as they stand.
 *   - The result is in AX, a long in DX:AX; carry set means AX is an errno.
 *     Nothing else changes -- signal(2)'s stub keeps a value in BX across
 *     the trap -- so the second result goes to DX where Xenix has it in BX.
 *   - fork: the child continues after the `int`, the parent two bytes on.
 *   - wait takes where to put the status (and where to put the dead
 *     child's "slop", which is not kept here).  time takes no argument and
 *     stime a value.  getpgrp and setpgrp are one call with a flag.
 *     utssys has its function as the third argument.
 *   - There is no brk: a process has the data segment its header asks for,
 *     and the C library divides it.
 *
 * A signal handler is entered with the signal number, the flags and the
 * interrupted IP pushed, in that order from the top, and every register as
 * it was; the library's own stub saves what it must, calls the C function
 * and returns with `popf; ret`.  The disposition is reset first, as in V7.
 *
 * The programs are the a.out files MINIX and ELKS later took the format of
 * (magic 01 03), and exec/formats/elks_aout.c loads them.
 * ===================================================================== */

#define PCIX_SYSENT         0x80U       /* int 0x80|N */
#define PCIX_INT_LEN        2U
#define PCIX_FORK_SKIP      2U          /* the parent's return, past the child's */
#define PCIX_UTS_UNAME      0U
#define PCIX_UTSNAME_SIZE   (5U * X286_NMLN)

static int pcix_trace_enabled(void) {
    return cmdline_debug_enabled("perso:pcix:syscall");
}

/* The system call at CS:IP, or -1 if the instruction is not one. */
static int pcix_syscall_at(registers_t *regs) {
    uintptr_t linear_ip;
    uint8_t insn[PCIX_INT_LEN];

    if (x286_seg_span((uint16_t)regs->cs, regs->eip, sizeof(insn),
                      &linear_ip) != 0 ||
        linear_ip >= USER32_VA_END ||
        copyin((const void *)linear_ip, insn, sizeof(insn)) != 0) {
        return -1;
    }
    if (insn[0] != 0xCDU || insn[1] <= PCIX_SYSENT) {
        return -1;
    }
    return insn[1] & 0x7FU;
}

/* getpgrp() and setpgrp(): one entry, the argument saying which. */
static int64_t pcix_sys_setpgrp(struct x286_frame *f) {
    if (f->bx != 0) {
        (void)sys_setpgid(0, 0);
    }
    return sys_getpgrp();
}

/* wait(statusp, slopp): the status goes where the caller says. */
static int64_t pcix_sys_wait(struct x286_frame *f) {
    int status = 0;
    int pid = kern_waitpid(-1, &status, 0);
    uintptr_t dst;
    uint16_t word;

    if (pid < 0) {
        return pid;
    }
    if (f->bx != 0) {
        if (x286_ds_span(f, f->bx, sizeof(word), &dst) != 0) {
            return -EFAULT;
        }
        word = (uint16_t)status;
        memcpy((void *)dst, &word, sizeof(word));
    }
    return pid & 0xFFFF;
}

static int64_t pcix_sys_utssys(struct x286_frame *f) {
    struct utsname native;
    struct x286_utsname out;
    uintptr_t dst;
    int rc;

    if (f->si != PCIX_UTS_UNAME) {
        return -EINVAL;             /* ustat */
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
    strlcpy(out.sysname, "PC/IX", sizeof(out.sysname));
    strlcpy(out.nodename, native.nodename, sizeof(out.nodename));
    strlcpy(out.release, "1.0", sizeof(out.release));
    strlcpy(out.version, "3", sizeof(out.version));
    strlcpy(out.machine, "ibmpc", sizeof(out.machine));
    /* <sys/utsname.h> ends with machine: the five names and no more. */
    memcpy((void *)dst, &out, PCIX_UTSNAME_SIZE);
    return 0;
}

static int64_t pcix_call(struct x286_frame *f) {
    registers_t *regs = f->regs;
    int pid;

    switch (f->nr) {
    case X286_SYS_fork:
        /* Both come back from here with eip after the `int`, which is
         * where the child goes on; the parent is moved past that. */
        regs->eflags &= ~X286_EFLAGS_CF;
        pid = sys_fork();
        regs->eip += PCIX_FORK_SKIP;
        return pid < 0 ? pid : (pid & 0xFFFF);
    case X286_SYS_wait:
        return pcix_sys_wait(f);
    case X286_SYS_setpgrp:
        return pcix_sys_setpgrp(f);
    case X286_SYS_utssys:
        return pcix_sys_utssys(f);
    case X286_SYS_signal:
        f->si = 0;                  /* a near handler: no selector */
        return x286_sys_signal(f);
    case X286_SYS_brk:              /* the library's, not the kernel's */
    case X286_SYS_exec:             /* only exece */
    case X286_SYS_xenix:
    case X286_SYS_stty:
    case X286_SYS_gtty:
    case X286_SYS_statfs:
    case X286_SYS_fstatfs:
    case X286_SYS_msgsys:
    case X286_SYS_sysi86:           /* halt and inuinfo here */
    case X286_SYS_shmsys:
    case X286_SYS_semsys:
    case X286_SYS_uadmin:
        return -EINVAL;
    default:
        break;
    }
    return (f->nr < X286_CALL_MAX && x286_calls[f->nr])
        ? x286_calls[f->nr](f) : -EINVAL;
}

static int pcix_handle_trap(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    struct x286_frame f;
    void *saved_syscall_regs;
    uintptr_t args;
    uint16_t w[4];
    int64_t ret;
    int nr;

    if (!regs || !current_process ||
        current_process->perso_id != PERS_PCIX || !current_process->ldt) {
        return 0;
    }
    if (regs->int_no != 13 && regs->int_no != 11) {
        return 0;
    }
    nr = pcix_syscall_at(regs);
    if (nr < 0) {
        return 0;
    }

    /* Four words of arguments, above the word where a return address
     * would be.  A call that takes fewer may be near the top of the
     * stack segment; what cannot be read is not an argument. */
    memset(w, 0, sizeof(w));
    for (unsigned int i = 0; i < 4; i++) {
        if (x286_seg_span((uint16_t)regs->ss,
                          ((regs->useresp & 0xFFFFU) + 2U + 2U * i) & 0xFFFFU,
                          sizeof(w[0]), &args) != 0) {
            break;
        }
        memcpy(&w[i], (const void *)args, sizeof(w[0]));
    }

    memset(&f, 0, sizeof(f));
    f.regs = regs;
    f.nr = (uint16_t)nr;
    f.bx = w[0];
    f.cx = w[1];
    f.si = w[2];
    f.di = w[3];
    f.ds = (uint16_t)regs->ds;
    f.es = (uint16_t)regs->es;
    f.ss = (uint16_t)regs->ss;

    if (current_thread && current_thread->proc == current_process) {
        current_thread->syscall_num = f.nr;
    }
    /* Past the trap before the call: fork copies this frame, and exece
     * does not come back. */
    regs->eip += PCIX_INT_LEN;
    saved_syscall_regs = current_thread ? current_thread->syscall_regs : NULL;
    if (current_thread) {
        current_thread->syscall_regs = regs;
    }

    ret = pcix_call(&f);

    if (current_thread) {
        current_thread->syscall_regs = saved_syscall_regs;
    }
    if (pcix_trace_enabled()) {
        char buf[160];
        const char *name = x286_call_name(f.nr);

        snprintf(buf, sizeof(buf),
                 "PCIX: [%d] %s/%u(%#x, %#x, %#x, %#x) = %lld\n",
                 (int)current_process->pid, name ? name : "sys", f.nr,
                 w[0], w[1], w[2], w[3], (long long)ret);
        kprint(buf);
    }

    if (ret < 0) {
        regs->eax = (regs->eax & 0xFFFF0000U) | ((uint32_t)(-ret) & 0xFFFFU);
        regs->eflags |= X286_EFLAGS_CF;
    } else {
        regs->eax = (regs->eax & 0xFFFF0000U) | ((uint32_t)ret & 0xFFFFU);
        regs->edx = (regs->edx & 0xFFFF0000U) |
                    (((uint32_t)ret >> 16) & 0xFFFFU);
        regs->eflags &= ~X286_EFLAGS_CF;
    }
    return 1;
}

static void pcix_sendsig(void *handler, int sig, uint32_t mask,
                         uint32_t flags, void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    uint16_t frame[3];
    uintptr_t linear;
    uint16_t sp;

    (void)flags;
    if (!regs || !current_process) {
        return;
    }
    /* The handler returns with a `ret`, to the kernel never: there is
     * nothing to undo the blocking of the signal while its handler runs,
     * which System III does not do in any case -- it resets the
     * disposition instead.  Left blocked, a signal was delivered once
     * per process: the second sleep(3) never woke. */
    if (current_thread) {
        current_thread->sig_mask = mask;
    }
    sp = (uint16_t)(regs->useresp & 0xFFFFU);
    if (sp < sizeof(frame)) {
        sigexit(current_process, SIGSEGV);
        return;
    }
    sp = (uint16_t)(sp - sizeof(frame));
    frame[0] = (uint16_t)x286_native_to_xenix_sig(sig);
    frame[1] = (uint16_t)regs->eflags;
    frame[2] = (uint16_t)regs->eip;
    if (x286_seg_span((uint16_t)regs->ss, sp, sizeof(frame), &linear) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }
    memcpy((void *)linear, frame, sizeof(frame));
    if (pcix_trace_enabled()) {
        char buf[96];

        snprintf(buf, sizeof(buf), "PCIX: [%d] signal %u -> %04x (from %04x)\n",
                 (int)current_process->pid, frame[0],
                 (unsigned int)(uintptr_t)handler & 0xFFFFU, frame[2]);
        kprint(buf);
    }
    regs->useresp = (regs->useresp & 0xFFFF0000U) | sp;
    regs->eip = (uint32_t)(uintptr_t)handler & 0xFFFFU;
}

/* =====================================================================
 * Venix/86: VenturCom's Version 7 for the IBM PC (2.1, 1985).
 *
 * The other end of the same line: System III's and so Xenix's call
 * numbers are V7's with more added, and stat, the directory entry and tms
 * have not changed since.  What follows was read out of the C library on
 * the distribution (/lib/libc.a; every stub is a dozen instructions):
 *
 *   - One vector for all calls, `int 0xf1`, with the number in BX and the
 *     arguments in AX, DX, CX, SI.  Those are a struct x286_frame's four
 *     in that order.
 *   - The result is in AX, a second or the high half in DX, and the error
 *     number in CX: 0 for none, and every stub tests it with jcxz.  The
 *     carry flag says nothing.
 *   - fork returns 0 in the child; wait the status in DX; getuid the
 *     effective id in DX.
 *   - brk is the kernel's.  A program's stack is under its data (struct
 *     venix_exec, exec/formats/elks_aout.h), so the break may be anything
 *     from the end of the bss up to the end of the segment.
 *   - A terminal is V7's: ioctl with TIOCGETP and its fellows, struct
 *     sgttyb, no termio.
 *   - A signal handler is entered with the flags and the interrupted IP
 *     pushed, flags on top, and returns with popf and ret.  The library
 *     gives the kernel a stub of its own for each signal, so no number is
 *     passed.
 *
 * Three more vectors are instructions to the kernel, not calls:
 *
 *     int 0xf4   in front of every 8087 instruction, for a machine with
 *                no 8087 to emulate it by.  There is one here; the two
 *                bytes are made no-ops and the instruction runs.
 *     int 0xf2   the stack check a function begins with found no room.
 *     int 0xf3   abort().
 * ===================================================================== */

#define VENIX_SYSCALL_VEC   0xF1U
#define VENIX_STKOVF_VEC    0xF2U
#define VENIX_ABORT_VEC     0xF3U
#define VENIX_FPU_VEC       0xF4U
#define VENIX_INT_LEN       2U
#define VENIX_SYS_ftime     35
#define VENIX_SYS_ioctl     54
#define VENIX_STACK_SLOP    0x100U      /* kept between break and stack */

static int venix_trace_enabled(void) {
    return cmdline_debug_enabled("perso:venix:syscall");
}

/* The vector of the `int` at CS:IP, or -1; *linear is where it is. */
static int venix_int_at(registers_t *regs, uintptr_t *linear) {
    uint8_t insn[VENIX_INT_LEN];

    if (x286_seg_span((uint16_t)regs->cs, regs->eip, sizeof(insn),
                      linear) != 0 ||
        *linear >= USER32_VA_END ||
        copyin((const void *)*linear, insn, sizeof(insn)) != 0 ||
        insn[0] != 0xCDU) {
        return -1;
    }
    return insn[1];
}

/*
 * brk(addr).  The data segment is all there from the start; what the
 * call decides is whether the program may have the address, which it may
 * if it is above what it was loaded with and -- for a program whose
 * stack is at the top -- short of the stack.
 */
static int64_t venix_sys_brk(struct x286_frame *f) {
    uint32_t base = (uint32_t)(current_process->brk_start & 0xFFFFU);
    uint32_t sp = f->regs->useresp & 0xFFFFU;
    uint32_t top = 0xFFF0U;

    /* brk_start is a linear address; the low word is the offset in the
     * data segment, whose base is a multiple of 64K. */
    if (sp > base && sp - VENIX_STACK_SLOP < top) {
        top = sp - VENIX_STACK_SLOP;
    }
    if (f->bx < base || f->bx > top) {
        return -ENOMEM;
    }
    return 0;
}

/*
 * ioctl(2): the terminal as V7 has it, <sgtty.h>.  TIOCGETP and TIOCSETP
 * (and TIOCSETN, which does not flush) move a struct sgttyb, and it is six
 * bytes here -- sg_flags is an int, and an int is a word.  isatty(3) is a
 * TIOCGETP into six bytes of its stack with its caller's registers saved
 * just above them.
 */
struct venix_sgttyb {
    uint8_t  sg_ispeed, sg_ospeed, sg_erase, sg_kill;
    uint16_t sg_flags;
} __attribute__((packed));

#define VENIX_TIOCGETP  0x7408U
#define VENIX_TIOCSETP  0x7409U
#define VENIX_TIOCSETN  0x740AU
#define VENIX_AIOCWAIT  0x6100U         /* ('a'<<8)|0 */
#define VENIX_B9600     13
#define VENIX_CBREAK    0002U
#define VENIX_ECHO      0010U
#define VENIX_CRMOD     0020U
#define VENIX_RAW       0040U

static int64_t venix_sys_ioctl(struct x286_frame *f) {
    int fd = (int)(int16_t)f->bx;
    struct venix_sgttyb sg;
    struct termios t;
    uintptr_t arg;
    int rc;

    /* aiowait(3): nothing here is ever outstanding to wait for. */
    if (f->cx == VENIX_AIOCWAIT) {
        return x286_fd_vnode(fd) ? 0 : -EBADF;
    }
    if (f->cx != VENIX_TIOCGETP && f->cx != VENIX_TIOCSETP &&
        f->cx != VENIX_TIOCSETN) {
        return x286_fd_vnode(fd) ? -ENOTTY : -EBADF;
    }
    memset(&t, 0, sizeof(t));
    rc = kern_ioctl(fd, TCGETS, &t);
    if (rc != 0) {
        return rc;
    }
    if (x286_ds_span(f, f->si, sizeof(sg), &arg) != 0) {
        return -EFAULT;
    }
    if (f->cx == VENIX_TIOCGETP) {
        memset(&sg, 0, sizeof(sg));
        sg.sg_ispeed = sg.sg_ospeed = VENIX_B9600;
        sg.sg_erase = t.c_cc[VERASE];
        sg.sg_kill = t.c_cc[VKILL];
        if (t.c_lflag & ECHO) sg.sg_flags |= VENIX_ECHO;
        if (t.c_iflag & ICRNL) sg.sg_flags |= VENIX_CRMOD;
        if (!(t.c_lflag & ICANON)) {
            sg.sg_flags |= (t.c_lflag & ISIG) ? VENIX_CBREAK : VENIX_RAW;
        }
        memcpy((void *)arg, &sg, sizeof(sg));
        return 0;
    }
    memcpy(&sg, (const void *)arg, sizeof(sg));
    t.c_cc[VERASE] = sg.sg_erase;
    t.c_cc[VKILL] = sg.sg_kill;
    t.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG);
    t.c_iflag &= ~(tcflag_t)ICRNL;
    t.c_oflag &= ~(tcflag_t)(OPOST | ONLCR);
    if (sg.sg_flags & VENIX_ECHO) t.c_lflag |= ECHO;
    if (!(sg.sg_flags & VENIX_RAW)) {
        t.c_lflag |= ISIG;
        t.c_oflag |= OPOST;
        if (!(sg.sg_flags & VENIX_CBREAK)) t.c_lflag |= ICANON;
        if (sg.sg_flags & VENIX_CRMOD) {
            t.c_iflag |= ICRNL;
            t.c_oflag |= ONLCR;
        }
    }
    if (!(t.c_lflag & ICANON)) {
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
    }
    return kern_ioctl(fd, f->cx == VENIX_TIOCSETP ? TCSETSW : TCSETS, &t);
}

/*
 * stat(2) and fstat(2).  The structure is V7's and so Xenix's, but the
 * file type in st_mode is still the Sixth Edition's (<sys/stat.h>): the
 * top bit says the inode is in use, and the next two what it is.
 *
 *     0100000  a file          0140000  a directory
 *     0120000  a character     0160000  a block special file
 *
 * where everything later has 0100000, 0040000, 0020000 and 0060000.
 */
#define VENIX_STAT_MODE     4U          /* offset of st_mode */
#define VENIX_IFMT          0170000U
#define VENIX_IALLOC        0100000U

static int64_t venix_sys_stat(struct x286_frame *f) {
    int64_t rc = x286_calls[f->nr](f);
    uintptr_t at;
    uint16_t mode, type;

    if (rc != 0 ||
        x286_ds_span(f, f->cx + VENIX_STAT_MODE, sizeof(mode), &at) != 0) {
        return rc;
    }
    memcpy(&mode, (const void *)at, sizeof(mode));
    type = mode & VENIX_IFMT;
    mode = (uint16_t)((mode & ~VENIX_IFMT) | VENIX_IALLOC);
    if (type == S_IFDIR || type == S_IFCHR || type == S_IFBLK) {
        mode |= type;
    }
    memcpy((void *)at, &mode, sizeof(mode));
    return 0;
}

/* ---------------------------------------------------------------------
 * The calls Venix added to Version 7.
 *
 * Nothing on the distribution documents them but the lint library's
 * argument lists, so what they do was read from the kernel itself
 * (/venix, which still has its symbol table): _syssema, _sysdata,
 * _sysphys, _suspend, _syslock, _locking.
 * --------------------------------------------------------------------- */

#define VENIX_SYS_sem       45
#define VENIX_SYS_sdata     49
#define VENIX_SYS_suspend   50
#define VENIX_SYS_phys      52
#define VENIX_SYS_lock      53
#define VENIX_SYS_locking   64

/*
 * Semaphores: 45, the function in AX and the semaphore in DX.
 *
 *     0 semset(n, pri)   wait until n is clear, then set it
 *     1 semclear(n)      clear it and wake whoever waits
 *     2 semtest(n)       1 if it is set
 *     3 semtset(n, pri)  set it if it is clear; 1 if it was set already
 *
 * A semaphore is a bit.  A negative number names one of sixteen the whole
 * system shares (-1 the first); 0 to 15, one of sixteen shared by the
 * processes running the same program, which Venix keeps with the
 * program's text.  One set by a process that then exits stays set.
 */
#define VENIX_NSEM          16
#define VENIX_SEM_PROGRAMS  32
#define VENIX_SEM_NAME      64

static struct venix_sem_set {
    char     program[VENIX_SEM_NAME];   /* "" for the system's */
    uint16_t bits;
} venix_sems[1 + VENIX_SEM_PROGRAMS];
static spinlock_t venix_sem_lock;
static int venix_sem_lock_ready;

/* The word semaphore `n` is a bit of; NULL if every slot is in use. */
static struct venix_sem_set *venix_sem_set_for(int n) {
    struct venix_sem_set *spare = NULL;

    if (n < 0) {
        return &venix_sems[0];
    }
    for (int i = 1; i <= VENIX_SEM_PROGRAMS; i++) {
        struct venix_sem_set *s = &venix_sems[i];

        if (s->program[0] == '\0' || s->bits == 0) {
            if (!spare) spare = s;      /* nothing held: may be reused */
            if (s->program[0] == '\0') continue;
        }
        if (strncmp(s->program, current_process->exec_path,
                    sizeof(s->program) - 1) == 0) {
            return s;
        }
    }
    if (spare) {
        strlcpy(spare->program, current_process->exec_path,
                sizeof(spare->program));
        spare->bits = 0;
    }
    return spare;
}

static int64_t venix_sys_sem(struct x286_frame *f) {
    int n = (int)(int16_t)f->cx;
    unsigned int bit_no = n < 0 ? (unsigned int)(-1 - n) : (unsigned int)n;
    struct venix_sem_set *s;
    uint16_t bit;
    int64_t ret = 0;

    if (f->bx > 3 || bit_no >= VENIX_NSEM) {
        return -EINVAL;
    }
    if (!venix_sem_lock_ready) {
        spinlock_init(&venix_sem_lock, "venix_sem");
        venix_sem_lock_ready = 1;
    }
    bit = (uint16_t)(1U << bit_no);

    for (;;) {
        spinlock_acquire(&venix_sem_lock);
        s = venix_sem_set_for(n);
        if (!s) {
            spinlock_release(&venix_sem_lock);
            return -ENOSPC;
        }
        switch (f->bx) {
        case 1:                         /* semclear */
            s->bits &= (uint16_t)~bit;
            spinlock_release(&venix_sem_lock);
            sched_wakeup(venix_sems);
            return 0;
        case 2:                         /* semtest */
            ret = (s->bits & bit) != 0;
            spinlock_release(&venix_sem_lock);
            return ret;
        default:                        /* semset, semtset */
            if (!(s->bits & bit)) {
                s->bits |= bit;
                spinlock_release(&venix_sem_lock);
                return 0;
            }
            spinlock_release(&venix_sem_lock);
            if (f->bx == 3) {
                return 1;
            }
            break;
        }
        /* Set, and semset waits.  Everything sleeps on the one channel
         * and looks again; a short deadline covers a wakeup that came
         * between the look and the sleep. */
        if (current_thread) current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        (void)sched_sleep_until(venix_sems, get_ticks() + (uint64_t)HZ / 20 + 1);
        if (current_thread) {
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
            if (current_thread->sig_pending & ~current_thread->sig_mask) {
                return -EINTR;
            }
        }
    }
}

/* suspend(pid, flag): stop the process, or with flag 0 let it run. */
static int64_t venix_sys_suspend(struct x286_frame *f) {
    return sys_kill((int)(int16_t)f->bx, f->cx ? SIGSTOP : SIGCONT);
}

/* lock(flag): keep the process in memory, the superuser's to ask.  It
 * is never swapped here, so the asking is all there is. */
static int64_t venix_sys_lock(struct x286_frame *f) {
    return (f->bx != 0 && current_process->euid != 0) ? -EPERM : 0;
}

/*
 * locking(fd, mode, size): lock `size` bytes of the file from where the
 * descriptor is positioned, 0 meaning to the end.  Mode 0 unlocks, 1
 * locks or fails with EACCES if another process has any of it, anything
 * else waits.  The size arrives in DX and CX and the mode in SI.
 */
/*
 * They are fcntl(2)'s record locks, exclusive ones: on the file, between
 * processes, waited for by a mode that waits, and gone when the process
 * unlocks, closes the file or exits -- which is Venix's rule too.  They
 * were once a table of this personality's own, when the kernel's record
 * locks belonged to an open file and two opens of one did not contend;
 * that table could not see a close, and kept a lock until its process
 * ended.  Venix has room for thirty locks in the system (NFLOCKS); here
 * there is no such limit to run into.
 */
static int64_t venix_sys_locking(struct x286_frame *f) {
    int fd = (int)(int16_t)f->bx;
    uint32_t size = (uint32_t)f->cx | ((uint32_t)f->si << 16);
    fs_node_t *node = x286_fd_vnode(fd);
    struct kflock kf;
    int rc;

    if (!node) {
        return -EBADF;
    }
    if ((node->flags & 0x7) == FS_DIRECTORY) {
        return -EACCES;
    }
    memset(&kf, 0, sizeof(kf));
    kf.l_type = f->di == 0 ? F_UNLCK : F_WRLCK;
    kf.l_whence = 1;                    /* from where the descriptor is */
    kf.l_start = 0;
    kf.l_len = size > 0x7fffffffU ? 0 : (int32_t)size;     /* 0: to the end */
    rc = proc_advlock(current_process, fd,
                      f->di <= 1 ? F_SETLK : F_SETLKW, &kf);
    /* Another process has some of it: Venix says EACCES. */
    return rc == -EAGAIN ? -EACCES : rc;
}

/*
 * sdata and phys: the extra segment.
 *
 * A Venix program has one more segment register than it needs, ES, and
 * these two calls point it somewhere: sdata at a segment of data shared
 * with other processes, phys at the machine's memory.  The program then
 * reaches it with an ES: override.
 *
 *     sdata(path, r, 0)   attach the file `path` as shared data
 *     sdata(1, r, n)      make n*512 bytes of shared data of no name,
 *                         for the children the process goes on to have
 *     sdata(0, r, n)      point ES n*512 bytes into what is attached
 *     sdata(2, ...)       detach it; ES is the data segment again
 *
 *     phys(r, s, a)       point ES at physical address a*512
 *
 * The first argument of sdata is a function or a pathname; Venix's
 * kernel tells them apart as this does, by whether it is more than 3.
 * phys is refused while shared data is attached.  Venix let anyone at
 * the display adapters (segment 0xB000 up) and the superuser anywhere;
 * here it is the adapter and ROM area, 0xA0000 to 1 MB, and nothing else
 * for anyone -- what is below that is substrate's own memory, not a PC's.
 *
 * The segment is a mapping in the process and descriptor 3 of its table,
 * which is what ES already selects.  Two more descriptors, never loaded,
 * keep what there is to undo: 4 the whole of the shared data, 5 the
 * physical mapping.
 */
#define VENIX_LDT_ES        ELKS_LDT_ES_INDEX
#define VENIX_LDT_SDATA     4U
#define VENIX_LDT_PHYS      5U
#define VENIX_CLICK         512U
#define VENIX_SEG_MAX       0x10000U
#define VENIX_PHYS_LOW      0xA0000U
#define VENIX_PHYS_HIGH     0x100000U
#define VENIX_MAP_SHARED    0x001
#define VENIX_MAP_ANON      0x020
#define VENIX_PROT_RW       0x3
#define VENIX_MS_SYNC       2

static gdt_entry_t *venix_ldt_slot(unsigned int index) {
    return x286_ldt_entry((uint16_t)((index << 3) | 4U | 3U));
}

static int venix_ldt_set(unsigned int index, uint32_t base, uint32_t size) {
    struct user_desc d;
    gdt_entry_t e;

    memset(&d, 0, sizeof(d));
    memset(&e, 0, sizeof(e));
    d.entry_number = index;
    d.base_addr = base;
    d.limit = size ? size - 1U : 0U;
    d.useable = 1;
    d.seg_not_present = size == 0;
    if (size != 0) {
        fill_ldt_entry(&e, &d);
    }
    return ldt_write_raw(current_process, index, &e, 1);
}

/* ES back to the data segment. */
static int venix_es_reset(void) {
    gdt_entry_t *ds = venix_ldt_slot(ELKS_LDT_DS_INDEX);

    return ds ? venix_ldt_set(VENIX_LDT_ES, ldt_entry_base(ds),
                              ldt_entry_limit(ds) + 1U)
              : -EINVAL;
}

/* Forget the mapping descriptor `index` records, if it records one. */
static void venix_unmap_slot(unsigned int index) {
    gdt_entry_t *e = venix_ldt_slot(index);

    if (e) {
        uint32_t base = ldt_entry_base(e) & ~0xFFFU;
        uint32_t len = (ldt_entry_limit(e) + 1U + 0xFFFU) & ~0xFFFU;

        /* What was stored in a file's shared data is the file's. */
        (void)sys_msync((void *)(uintptr_t)base, len, VENIX_MS_SYNC);
        (void)sys_munmap((void *)(uintptr_t)base, len);
        (void)venix_ldt_set(index, 0, 0);
    }
}

static int64_t venix_sys_sdata(struct x286_frame *f) {
    gdt_entry_t *seg = venix_ldt_slot(VENIX_LDT_SDATA);
    uint32_t size, off, prot = VENIX_PROT_RW;
    struct stat st;
    char *path = NULL;
    void *va;
    int fd, rc;

    if (f->bx == 0) {                   /* move ES within it */
        if (!seg) return -EINVAL;
        off = (uint32_t)f->si * VENIX_CLICK;
        size = ldt_entry_limit(seg) + 1U;
        if (off >= size) return -EINVAL;
        return venix_ldt_set(VENIX_LDT_ES, ldt_entry_base(seg) + off,
                             size - off);
    }
    if (f->bx == 2 || f->bx == 3) {     /* detach */
        if (!seg) return -EINVAL;
        rc = venix_es_reset();
        venix_unmap_slot(VENIX_LDT_SDATA);
        return rc;
    }
    if (seg) return -EINVAL;
    if (f->bx == 1) {                   /* new, of no name */
        size = (uint32_t)f->si * VENIX_CLICK;
        if (size == 0 || size > VENIX_SEG_MAX) return -EINVAL;
        va = sys_mmap(NULL, size, (int)prot,
                      VENIX_MAP_SHARED | VENIX_MAP_ANON, -1, 0);
    } else {                            /* a file */
        rc = x286_ds_string(f, f->bx, &path);
        if (rc != 0) return rc;
        fd = kern_open(path, O_RDWR, 0);
        if (fd == -EACCES || fd == -EROFS) {
            fd = kern_open(path, O_RDONLY, 0);
            prot = 0x1;
        }
        x286_free_string(path);
        if (fd < 0) return fd;
        rc = kern_fstat(fd, &st);
        if (rc == 0 && !S_ISREG(st.st_mode)) rc = -EISDIR;
        if (rc != 0) {
            kern_close(fd);
            return rc;
        }
        size = ((uint32_t)st.st_size + VENIX_CLICK - 1U) & ~(VENIX_CLICK - 1U);
        if (size == 0 || size > VENIX_SEG_MAX) {
            kern_close(fd);
            return -ENOMEM;
        }
        va = sys_mmap(NULL, size, (int)prot, VENIX_MAP_SHARED, fd, 0);
        kern_close(fd);
    }
    if ((uintptr_t)va >= USER32_VA_END) {
        return -ENOMEM;
    }
    venix_unmap_slot(VENIX_LDT_PHYS);
    rc = venix_ldt_set(VENIX_LDT_SDATA, (uint32_t)(uintptr_t)va, size);
    if (rc == 0) {
        rc = venix_ldt_set(VENIX_LDT_ES, (uint32_t)(uintptr_t)va, size);
    }
    if (rc != 0) {
        (void)sys_munmap(va, (size + 0xFFFU) & ~0xFFFU);
    }
    return rc;
}

static int64_t venix_sys_phys(struct x286_frame *f) {
    uint32_t addr = (uint32_t)f->si * VENIX_CLICK;
    uint32_t page = addr & ~0xFFFU;
    uint32_t len;
    void *va;
    int fd, rc;

    if (venix_ldt_slot(VENIX_LDT_SDATA) || f->si == 0xFFFFU ||
        addr < VENIX_PHYS_LOW || addr >= VENIX_PHYS_HIGH) {
        return -EPERM;
    }
    len = VENIX_PHYS_HIGH - page;
    if (len > VENIX_SEG_MAX + 0x1000U) len = VENIX_SEG_MAX + 0x1000U;
    fd = kern_open("/dev/mem", O_RDWR, 0);
    if (fd < 0) {
        return fd == -ENOENT ? -EPERM : fd;
    }
    va = sys_mmap(NULL, len, VENIX_PROT_RW, VENIX_MAP_SHARED, fd, page);
    kern_close(fd);
    if ((uintptr_t)va >= USER32_VA_END) {
        return -EPERM;
    }
    venix_unmap_slot(VENIX_LDT_PHYS);
    rc = venix_ldt_set(VENIX_LDT_PHYS, (uint32_t)(uintptr_t)va, len);
    if (rc == 0) {
        uint32_t skip = addr - page;

        rc = venix_ldt_set(VENIX_LDT_ES, (uint32_t)(uintptr_t)va + skip,
                           len - skip > VENIX_SEG_MAX ? VENIX_SEG_MAX
                                                      : len - skip);
    }
    return rc;
}

static int64_t venix_call(struct x286_frame *f) {
    registers_t *regs = f->regs;
    int pid;

    switch (f->nr) {
    case VENIX_SYS_sem:
        return venix_sys_sem(f);
    case VENIX_SYS_sdata:
        return venix_sys_sdata(f);
    case VENIX_SYS_suspend:
        return venix_sys_suspend(f);
    case VENIX_SYS_phys:
        return venix_sys_phys(f);
    case VENIX_SYS_lock:
        return venix_sys_lock(f);
    case VENIX_SYS_locking:
        return venix_sys_locking(f);
    case X286_SYS_stat:
    case X286_SYS_fstat:
        return venix_sys_stat(f);
    case X286_SYS_fork:
        /* The child's frame is this one with AX made 0: its CX has to
         * say "no error" already. */
        regs->ecx &= 0xFFFF0000U;
        pid = sys_fork();
        return pid < 0 ? pid : (pid & 0xFFFF);
    case X286_SYS_brk:
        return venix_sys_brk(f);
    case VENIX_SYS_ftime:
        return x286_xsys_ftime(f);
    case VENIX_SYS_ioctl:
        return venix_sys_ioctl(f);
    case X286_SYS_signal:
        f->si = 0;                  /* a near handler: no selector */
        return x286_sys_signal(f);
    case X286_SYS_stty:
    case X286_SYS_gtty:             /* ioctl here */
    case X286_SYS_fstatfs:
    case X286_SYS_setpgrp:
    case X286_SYS_xenix:
    case X286_SYS_uadmin:
    case X286_SYS_utssys:
    case X286_SYS_fcntl:
    case X286_SYS_ulimit:
        return -EINVAL;
    default:
        break;
    }
    return (f->nr < X286_CALL_MAX && x286_calls[f->nr])
        ? x286_calls[f->nr](f) : -EINVAL;
}

static int venix_handle_trap(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    struct x286_frame f;
    void *saved_syscall_regs;
    uintptr_t linear = 0;
    int64_t ret;
    int vec;

    if (!regs || !current_process ||
        current_process->perso_id != PERS_VENIX || !current_process->ldt) {
        return 0;
    }
    if (regs->int_no != 13 && regs->int_no != 11) {
        return 0;
    }
    vec = venix_int_at(regs, &linear);
    switch (vec) {
    case VENIX_SYSCALL_VEC:
        break;
    case VENIX_FPU_VEC: {
        /* Out of the way for good where the text can be written (it is
         * the process's own copy); stepped over this once if not. */
        static const uint8_t nops[VENIX_INT_LEN] = { 0x90, 0x90 };

        if (copyout(nops, (void *)linear, sizeof(nops)) != 0) {
            regs->eip += VENIX_INT_LEN;
        }
        return 1;
    }
    case VENIX_STKOVF_VEC:
        sigexit(current_process, SIGSEGV);
        return 1;
    case VENIX_ABORT_VEC:
        sigexit(current_process, SIGABRT);
        return 1;
    default:
        return 0;
    }

    memset(&f, 0, sizeof(f));
    f.regs = regs;
    f.nr = (uint16_t)(regs->ebx & 0xFFFFU);
    f.bx = (uint16_t)(regs->eax & 0xFFFFU);
    f.cx = (uint16_t)(regs->edx & 0xFFFFU);
    f.si = (uint16_t)(regs->ecx & 0xFFFFU);
    f.di = (uint16_t)(regs->esi & 0xFFFFU);
    f.ds = (uint16_t)regs->ds;
    f.es = (uint16_t)regs->es;
    f.ss = (uint16_t)regs->ss;

    if (current_thread && current_thread->proc == current_process) {
        current_thread->syscall_num = f.nr;
    }
    regs->eip += VENIX_INT_LEN;
    saved_syscall_regs = current_thread ? current_thread->syscall_regs : NULL;
    if (current_thread) {
        current_thread->syscall_regs = regs;
    }

    ret = venix_call(&f);

    if (current_thread) {
        current_thread->syscall_regs = saved_syscall_regs;
    }
    if (venix_trace_enabled()) {
        char buf[160];
        const char *name = x286_call_name(f.nr);

        snprintf(buf, sizeof(buf),
                 "VENIX: [%d] %s/%u(%#x, %#x, %#x, %#x) = %lld\n",
                 (int)current_process->pid, name ? name : "sys", f.nr,
                 f.bx, f.cx, f.si, f.di, (long long)ret);
        kprint(buf);
    }

    if (ret < 0) {
        regs->eax = (regs->eax & 0xFFFF0000U) | 0xFFFFU;
        regs->ecx = (regs->ecx & 0xFFFF0000U) | ((uint32_t)(-ret) & 0xFFFFU);
    } else {
        regs->eax = (regs->eax & 0xFFFF0000U) | ((uint32_t)ret & 0xFFFFU);
        regs->edx = (regs->edx & 0xFFFF0000U) |
                    (((uint32_t)ret >> 16) & 0xFFFFU);
        regs->ecx &= 0xFFFF0000U;
    }
    return 1;
}

static void venix_sendsig(void *handler, int sig, uint32_t mask,
                          uint32_t flags, void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    uint16_t frame[2];
    uintptr_t linear;
    uint16_t sp;

    (void)flags;
    if (!regs || !current_process) {
        return;
    }
    /* As for PC/IX: the handler never returns to the kernel, so nothing
     * may be left blocked on its account. */
    if (current_thread) {
        current_thread->sig_mask = mask;
    }
    sp = (uint16_t)(regs->useresp & 0xFFFFU);
    if (sp < sizeof(frame)) {
        sigexit(current_process, SIGSEGV);
        return;
    }
    sp = (uint16_t)(sp - sizeof(frame));
    frame[0] = (uint16_t)regs->eflags;
    frame[1] = (uint16_t)regs->eip;
    if (x286_seg_span((uint16_t)regs->ss, sp, sizeof(frame), &linear) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }
    memcpy((void *)linear, frame, sizeof(frame));
    if (venix_trace_enabled()) {
        char buf[96];

        snprintf(buf, sizeof(buf), "VENIX: [%d] signal %d -> %04x (from %04x)\n",
                 (int)current_process->pid, sig,
                 (unsigned int)(uintptr_t)handler & 0xFFFFU, frame[1]);
        kprint(buf);
    }
    regs->useresp = (regs->useresp & 0xFFFF0000U) | sp;
    regs->eip = (uint32_t)(uintptr_t)handler & 0xFFFFU;
}

struct personality personality_venix = {
    .name = "Venix",
    .id = PERS_VENIX,
    .syscall_table = xenix_syscalls,
    .syscall_names = xenix_names,
    .syscall_fmts = NULL,
    .syscall_count = MAX_SYSCALLS,
    .path_prefix = "/perso/venix",
    /* /dev is the kernel's: the tree's, where it has one, is whatever a
     * backup left there, and phys(2) wants the real /dev/mem. */
    .native_dev = 1,
    .sendsig = venix_sendsig,
    .handle_trap = venix_handle_trap,
};

struct personality personality_pcix = {
    .name = "PCIX",
    .id = PERS_PCIX,
    .syscall_table = xenix_syscalls,
    .syscall_names = xenix_names,
    .syscall_fmts = NULL,
    .syscall_count = MAX_SYSCALLS,
    .path_prefix = "/perso/pcix",
    /* /dev is the kernel's: the tree's nodes are PC/IX's own, numbered for
     * its drivers -- its /dev/null is (4,2) -- and open here as whatever
     * has that number, if anything has. */
    .native_dev = 1,
    .sendsig = pcix_sendsig,
    .handle_trap = pcix_handle_trap,
};

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
