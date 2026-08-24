/*
 * perso_sco_x286.c - SCO Xenix/286 ("SCO-X/286") personality.
 *
 * Xenix/286 is a 16-bit protected-mode System V.2 derivative.  Its binaries
 * are segmented x.out images (see exec/formats/xout286.c) whose system-call
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
 * Development System's own libc; see sco_x286/sco_x286_syscalls.h.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/i386/gdt.h>
#include <arch/i386/idt.h>
#include <arch/i386/pmap.h>
#include <exec/formats/xout286.h>
#include <exec/perso/personality.h>
#include <exec/perso/sco_x286/sco_x286_syscalls.h>
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
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

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
    return kern_read(fd, (char *)buf, (size_t)f->si);
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
    /* off_t is a long: CX is its low half, SI its high half. */
    uint32_t off = (uint32_t)f->cx | ((uint32_t)f->si << 16);
    int64_t rc = sys_lseek((int)(int16_t)f->bx, off, 0, (int)(int16_t)f->di);

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
    if ((long)rc < 0) {
        return (int64_t)(long)rc;
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
         * /perso/xenix286s wins -- but exec is allowed to fall through to a
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
    uint32_t floor = current_process->brk + X286_STACK_GUARD;

    if (want <= floor) {
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
    return sys_ftruncate((int)(int16_t)f->bx, (uint32_t)f->cx,
                         (uint32_t)f->si);
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
    if (linear_ip >= 0xC0000000U) {
        return 0;
    }
    if (copyin((const void *)linear_ip, insn, sizeof(insn)) != 0) {
        return 0;
    }
    return insn[0] == 0xCDU && insn[1] == X286_SYSCALL_VEC;
}

static int x286_handle_trap(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    struct x286_frame f;
    x286_callfn fn;
    void *saved_syscall_regs;
    int64_t ret;

    if (!regs || !current_process ||
        current_process->perso_id != PERS_SCO_X286 ||
        !current_process->ldt) {
        return 0;
    }
    /* An `int $5` from CPL 3 against a DPL 0 gate raises #GP with the IDT
     * bit set in the error code; #NP is possible if the gate were absent. */
    if (regs->int_no != 13 && regs->int_no != 11) {
        return 0;
    }
    if (!x286_is_syscall_int(regs)) {
        return 0;
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

struct personality personality_sco_x286 = {
    .name = "SCO-X/286",
    .id = PERS_SCO_X286,
    .syscall_table = NULL,   /* dispatched by x286_handle_trap, not int 0x80 */
    .syscall_names = NULL,
    .syscall_fmts = NULL,
    .syscall_count = 0,
    .path_prefix = "/perso/xenix286s",
    .sendsig = x286_sendsig,
    .handle_trap = x286_handle_trap,
};
