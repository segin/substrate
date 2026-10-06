/*
 * perso_sysv386.c - the i386 system call convention, and the calls, that
 * Xenix/386 and UNIX System V/386 have in common.
 *
 * A libc stub on either system is
 *
 *     _open:  mov   $5,%eax
 *             lcall $7,$0            ; 9A 00 00 00 00 07 00
 *             jb    cerror
 *             ret
 *
 * so the number is in EAX and the arguments are the caller's cdecl words,
 * still on the stack above the stub's return address.  Carry reports
 * failure with the errno in EAX.  A second result comes back in EDX: the
 * parent's pid from getpid, the effective id from getuid and getgid, the
 * status from wait, the write end from pipe -- and from fork, zero in the
 * parent and non-zero in the child.
 *
 * Substrate has no call gate behind selector 7, so the lcall faults (#GP
 * or #NP) and a personality's handle_trap hook brings it here.  The same
 * goes for `lcall $0xf,$0`, by which libc's trampoline returns from a
 * signal handler.
 *
 * This is not a personality.  perso_xenix.c and perso_svr4.c are, and each
 * hands over a struct sysv386_abi saying what is its own (sysv386.h).
 * What is here is what does not depend on which of them is asking.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <machine/gdt.h>
#include <machine/idt.h>
#include <machine/vmparam.h>
#include <exec/perso/sysv386.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <pm/pm.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/kern_syscalls.h>
#include <sys/ldt.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/syscall_impl.h>
#include <sys/termios.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <vm/vm_kmem.h>

#define SYSV386_EFLAGS_CF   0x00000001U   /* carry */
#define SYSV386_EFLAGS_DF   0x00000400U   /* direction */
/* The flags a program may set for itself: CF PF AF ZF SF TF DF OF. */
#define SYSV386_EFLAGS_USER 0x00000DD5U
#define SYSV386_LCALL_LEN   7U            /* 9A off32 sel16 */
#define SYSV386_GATE_SEL    0x0007U       /* the system call gate */
#define SYSV386_SIGRET_SEL  0x000FU       /* the signal return gate */

#define SYSV386_PATH_MAX    1024U
#define SYSV386_MAX_VEC     256U          /* argv or envp entries */

/* kern_sigprocmask's `how`. */
#define SYSV386_MASK_BLOCK   1
#define SYSV386_MASK_UNBLOCK 2
#define SYSV386_MASK_SET     3

/* signal(2): dispositions, and the System V Release 3 variants carried in
 * the second byte of its first argument. */
#define SYSV_SIG_DFL       0U
#define SYSV_SIG_IGN       1U
#define SYSV_SIG_HOLDVAL   2U             /* SIG_HOLD */
#define SYSV_SIGNO_MASK    0x00FFU
#define SYSV_SIG_SET       0x0100U        /* sigset */
#define SYSV_SIG_HOLD      0x0200U        /* sighold */
#define SYSV_SIG_RELSE     0x0400U        /* sigrelse */
#define SYSV_SIG_IGNORE    0x0800U        /* sigignore */
#define SYSV_SIG_PAUSE     0x1000U        /* sigpause */

/* ---- user memory ----------------------------------------------------- */

int sysv386_span(uint32_t addr, uint32_t len) {
    if (addr >= USER32_VA_END || len > USER32_VA_END - addr) {
        return -EFAULT;
    }
    return 0;
}

static int sysv386_put(uint32_t dst, const void *src, uint32_t len) {
    if (sysv386_span(dst, len) != 0 ||
        copyout(src, (void *)(uintptr_t)dst, len) != 0) {
        return -EFAULT;
    }
    return 0;
}

static int sysv386_get(uint32_t src, void *dst, uint32_t len) {
    if (sysv386_span(src, len) != 0 ||
        copyin((const void *)(uintptr_t)src, dst, len) != 0) {
        return -EFAULT;
    }
    return 0;
}

void sysv386_free_string(char *s) {
    if (s) {
        kfree(s, strlen(s) + 1U);
    }
}

int sysv386_string(uint32_t addr, char **out) {
    size_t len = 0;
    char *copy;

    *out = NULL;
    if (addr == 0 || sysv386_span(addr, 1) != 0) {
        return -EFAULT;
    }
    if (copyinstr((const void *)(uintptr_t)addr, NULL, SYSV386_PATH_MAX,
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
    /* Freed by its string length, so that has to be the length it has. */
    if (strlen(copy) + 1U != len) {
        char *exact = kmalloc(strlen(copy) + 1U);

        if (exact) {
            memcpy(exact, copy, strlen(copy) + 1U);
        }
        kfree(copy, len);
        if (!exact) {
            return -ENOMEM;
        }
        copy = exact;
    }
    *out = copy;
    return 0;
}

int64_t sysv386_pair(uint32_t first, uint32_t second) {
    return (int64_t)((uint64_t)first | ((uint64_t)second << 32));
}

/* Call `fn` on the path at `addr`. */
static int64_t sysv386_path1(uint32_t addr, int (*fn)(const char *)) {
    char *path = NULL;
    int rc = sysv386_string(addr, &path);

    if (rc == 0) {
        rc = fn(path);
        sysv386_free_string(path);
    }
    return rc;
}

/* ---- termio ---------------------------------------------------------- */

/* VMIN and VTIME sit on VEOF and VEOL in termio; substrate follows the
 * Linux layout, where they have slots of their own. */
#define SYSV_VEOF 4
#define SYSV_VEOL 5

void sysv_termios_to_termio(struct sysv_termio *dst,
                            const struct termios *src) {
    unsigned int i;

    memset(dst, 0, sizeof(*dst));
    dst->c_iflag = (uint16_t)src->c_iflag;
    dst->c_oflag = (uint16_t)src->c_oflag;
    dst->c_cflag = (uint16_t)src->c_cflag;
    dst->c_lflag = (uint16_t)src->c_lflag;
    dst->c_line  = (char)src->c_line;
    for (i = 0; i < SYSV_NCC; i++) {
        dst->c_cc[i] = src->c_cc[i];
    }
    if (!(src->c_lflag & ICANON)) {
        dst->c_cc[SYSV_VEOF] = src->c_cc[VMIN];
        dst->c_cc[SYSV_VEOL] = src->c_cc[VTIME];
    }
}

void sysv_termio_to_termios(struct termios *dst,
                            const struct sysv_termio *src) {
    unsigned int i;

    /* Keep the high halves and the c_cc slots termio has no room for
     * (VSTART, VSTOP, VSUSP, ...). */
    dst->c_iflag = (dst->c_iflag & 0xFFFF0000U) | src->c_iflag;
    dst->c_oflag = (dst->c_oflag & 0xFFFF0000U) | src->c_oflag;
    dst->c_cflag = (dst->c_cflag & 0xFFFF0000U) | src->c_cflag;
    dst->c_lflag = (dst->c_lflag & 0xFFFF0000U) | src->c_lflag;
    dst->c_line  = (cc_t)src->c_line;
    for (i = 0; i < SYSV_NCC; i++) {
        dst->c_cc[i] = src->c_cc[i];
    }
    if (!(src->c_lflag & ICANON)) {
        dst->c_cc[VMIN]  = src->c_cc[SYSV_VEOF];
        dst->c_cc[VTIME] = src->c_cc[SYSV_VEOL];
    }
}

/* ---- the calls ------------------------------------------------------- */

static int64_t sysv386_sys_exit(struct sysv386_frame *f) {
    return sys_exit((int)f->a[0]);
}

static int64_t sysv386_sys_fork(struct sysv386_frame *f) {
    registers_t *regs = f->regs;
    uint32_t saved_edx = regs->edx;
    uint32_t saved_eflags = regs->eflags;
    int pid;

    /* The child resumes from a copy of this frame with only EAX forced to
     * zero, so what tells it that it is the child has to be in the frame
     * before the fork: EDX non-zero, carry clear. */
    regs->edx = 1;
    regs->eflags &= ~SYSV386_EFLAGS_CF;
    pid = sys_fork();
    regs->edx = saved_edx;
    regs->eflags = saved_eflags;

    if (pid < 0) {
        return pid;
    }
    return (int64_t)(uint32_t)pid;   /* EDX = 0: the parent */
}

static int64_t sysv386_sys_read(struct sysv386_frame *f) {
    int fd = (int)f->a[0];
    int64_t rv;

    if (sysv386_span(f->a[1], f->a[2]) != 0) {
        return -EFAULT;
    }
    if (f->abi->read_dir && f->abi->read_dir(fd, f->a[1], f->a[2], &rv)) {
        return rv;
    }
    rv = kern_read(fd, (char *)(uintptr_t)f->a[1], (size_t)f->a[2]);
    /* O_NDELAY reports "nothing yet" as a zero-length read. */
    return rv == -EAGAIN ? 0 : rv;
}

static int64_t sysv386_sys_write(struct sysv386_frame *f) {
    if (sysv386_span(f->a[1], f->a[2]) != 0) {
        return -EFAULT;
    }
    return kern_write((int)f->a[0], (const char *)(uintptr_t)f->a[1],
                      (size_t)f->a[2]);
}

static int64_t sysv386_sys_open(struct sysv386_frame *f) {
    char *path = NULL;
    int rc = sysv386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_open(path, f->abi->open_flags(f->a[1]), (int)f->a[2]);
    sysv386_free_string(path);
    return rc;
}

static int64_t sysv386_sys_close(struct sysv386_frame *f) {
    return kern_close((int)f->a[0]);
}

static int64_t sysv386_sys_wait(struct sysv386_frame *f) {
    int status = 0;
    int pid;

    (void)f;
    pid = kern_waitpid(-1, &status, 0);
    if (pid < 0) {
        return pid;
    }
    return sysv386_pair((uint32_t)pid, (uint32_t)(status & 0xFFFF));
}

static int64_t sysv386_sys_creat(struct sysv386_frame *f) {
    char *path = NULL;
    int rc = sysv386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_open(path, O_WRONLY | O_CREAT | O_TRUNC, (int)f->a[1]);
    sysv386_free_string(path);
    return rc;
}

static int64_t sysv386_sys_link(struct sysv386_frame *f) {
    char *oldp = NULL, *newp = NULL;
    int rc = sysv386_string(f->a[0], &oldp);

    if (rc == 0) {
        rc = sysv386_string(f->a[1], &newp);
    }
    if (rc == 0) {
        rc = kern_link(oldp, newp);
    }
    sysv386_free_string(oldp);
    sysv386_free_string(newp);
    return rc;
}

static int64_t sysv386_sys_unlink(struct sysv386_frame *f) {
    return sysv386_path1(f->a[0], kern_unlink);
}

static int64_t sysv386_sys_chdir(struct sysv386_frame *f) {
    return sysv386_path1(f->a[0], kern_chdir);
}

static int64_t sysv386_sys_chroot(struct sysv386_frame *f) {
    return sysv386_path1(f->a[0], kern_chroot);
}

static int64_t sysv386_sys_time(struct sysv386_frame *f) {
    (void)f;
    return (int64_t)(uint32_t)kern_time(NULL);
}

static int64_t sysv386_sys_mknod(struct sysv386_frame *f) {
    char *path = NULL;
    int rc = sysv386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = sys_mknod(path, (int)f->a[1], (int)f->a[2]);
    sysv386_free_string(path);
    return rc;
}

static int64_t sysv386_sys_chmod(struct sysv386_frame *f) {
    char *path = NULL;
    int rc = sysv386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = sys_chmod(path, (int)f->a[1]);
    sysv386_free_string(path);
    return rc;
}

static int64_t sysv386_sys_chown(struct sysv386_frame *f) {
    char *path = NULL;
    int rc = sysv386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = sys_chown(path, (int)f->a[1], (int)f->a[2]);
    sysv386_free_string(path);
    return rc;
}

static int64_t sysv386_sys_access(struct sysv386_frame *f) {
    char *path = NULL;
    int rc = sysv386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_access(path, (int)f->a[1]);
    sysv386_free_string(path);
    return rc;
}

/*
 * brk(2) takes the new break as an address and returns 0.  sys_brk reports
 * failure the native way, by leaving the break where it was.
 */
static int64_t sysv386_sys_brk(struct sysv386_frame *f) {
    uint32_t want = f->a[0];
    uint32_t got = (uint32_t)(uintptr_t)sys_brk((void *)(uintptr_t)want);

    return got == want ? 0 : -ENOMEM;
}

/* struct stat: seven 16-bit fields, two bytes of padding, four longs. */
struct sysv386_stat {
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

static int64_t sysv386_put_stat(const struct stat *native, uint32_t dst) {
    struct sysv386_stat out;

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
    return sysv386_put(dst, &out, sizeof(out));
}

static int64_t sysv386_sys_stat(struct sysv386_frame *f) {
    char *path = NULL;
    struct stat native;
    int rc = sysv386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = kern_stat(path, &native);
    sysv386_free_string(path);
    return rc != 0 ? rc : sysv386_put_stat(&native, f->a[1]);
}

static int64_t sysv386_sys_fstat(struct sysv386_frame *f) {
    struct stat native;
    int rc = kern_fstat((int)f->a[0], &native);

    return rc != 0 ? rc : sysv386_put_stat(&native, f->a[1]);
}

static int64_t sysv386_sys_lseek(struct sysv386_frame *f) {
    int64_t off = (int64_t)(int32_t)f->a[1];   /* off_t is a signed long */
    int64_t rc = sys_lseek((int)f->a[0], (uint32_t)off,
                           (uint32_t)((uint64_t)off >> 32), (int)f->a[2]);

    return rc < 0 ? rc : (int64_t)(uint32_t)rc;
}

static int64_t sysv386_sys_getpid(struct sysv386_frame *f) {
    (void)f;
    return sysv386_pair((uint32_t)sys_getpid(), (uint32_t)sys_getppid());
}

static int64_t sysv386_sys_getuid(struct sysv386_frame *f) {
    (void)f;
    return sysv386_pair((uint32_t)sys_getuid() & 0xFFFFU,
                        (uint32_t)sys_geteuid() & 0xFFFFU);
}

static int64_t sysv386_sys_getgid(struct sysv386_frame *f) {
    (void)f;
    return sysv386_pair((uint32_t)sys_getgid() & 0xFFFFU,
                        (uint32_t)sys_getegid() & 0xFFFFU);
}

static int64_t sysv386_sys_setuid(struct sysv386_frame *f) {
    return sys_setuid((int)f->a[0]);
}

static int64_t sysv386_sys_setgid(struct sysv386_frame *f) {
    return sys_setgid((int)f->a[0]);
}

static int64_t sysv386_sys_alarm(struct sysv386_frame *f) {
    return (int64_t)(uint32_t)sys_alarm((unsigned int)f->a[0]);
}

static int64_t sysv386_sys_pause(struct sysv386_frame *f) {
    (void)f;
    return sys_pause();
}

static int64_t sysv386_sys_nice(struct sysv386_frame *f) {
    return sys_nice((int)f->a[0]);
}

static int64_t sysv386_sys_sync(struct sysv386_frame *f) {
    (void)f;
    return sys_sync();
}

static int64_t sysv386_sys_kill(struct sysv386_frame *f) {
    int sig = 0;

    if (f->a[1] != 0) {
        sig = f->abi->signo(f->a[1]);
        if (sig < 0) {
            return sig;
        }
    }
    return sys_kill((int)f->a[0], sig);
}

static int64_t sysv386_sys_setpgrp(struct sysv386_frame *f) {
    /* setpgrp(flag): non-zero makes the caller a group leader; either way
     * the result is the process group. */
    if (f->a[0] != 0) {
        (void)sys_setpgid(0, 0);
    }
    return sys_getpgrp();
}

static int64_t sysv386_sys_dup(struct sysv386_frame *f) {
    return sys_dup((int)f->a[0]);
}

static int64_t sysv386_sys_pipe(struct sysv386_frame *f) {
    int fds[2] = { -1, -1 };
    int rc;

    (void)f;
    rc = kern_pipe(fds);
    if (rc < 0) {
        return rc;
    }
    return sysv386_pair((uint32_t)fds[0], (uint32_t)fds[1]);
}

/* struct tms: four longs. */
struct sysv386_tms {
    int32_t tms_utime;
    int32_t tms_stime;
    int32_t tms_cutime;
    int32_t tms_cstime;
};

static int64_t sysv386_sys_times(struct sysv386_frame *f) {
    struct tms native;
    struct sysv386_tms out;
    clock_t rc;

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
    if (sysv386_put(f->a[0], &out, sizeof(out)) != 0) {
        return -EFAULT;
    }
    return (int64_t)(uint32_t)rc;
}

static int64_t sysv386_sys_umask(struct sysv386_frame *f) {
    return sys_umask((int)f->a[0]);
}

/*
 * ulimit(cmd, newlimit).  1 and 2 get and set the file size limit and are
 * the native call's.  3 asks for the highest break the process may have:
 * programs size their working memory from the difference between that and
 * the break they have now (sort(1) takes all of it), so the answer is the
 * current break and a fixed allowance, not the end of the address space.
 * 4 is the number of files it may have open.
 */
#define SYSV_UL_GMEMLIM   3
#define SYSV_UL_GDESLIM   4
#define SYSV386_BRK_ROOM  (16U * 1024U * 1024U)

static int64_t sysv386_sys_ulimit(struct sysv386_frame *f) {
    int rc;

    switch (f->a[0]) {
    case SYSV_UL_GMEMLIM: {
        uint32_t brk = (uint32_t)(uintptr_t)sys_brk(NULL);

        if (brk > USER32_VA_END - SYSV386_BRK_ROOM) {
            return (int64_t)USER32_VA_END;
        }
        return (int64_t)(brk + SYSV386_BRK_ROOM);
    }
    case SYSV_UL_GDESLIM:
        return MAX_FD;
    default:
        rc = sys_ulimit((int)f->a[0], (long)(int32_t)f->a[1]);
        return rc < 0 ? rc : (int64_t)(uint32_t)rc;
    }
}

/* The command numbers are substrate's; F_GETFL and F_SETFL carry open
 * flags, which are the personality's. */
static int64_t sysv386_sys_fcntl(struct sysv386_frame *f) {
    int fd = (int)f->a[0];
    int rc;

    switch (f->a[1]) {
    case F_DUPFD:
    case F_GETFD:
    case F_SETFD:
        return sys_fcntl(fd, (int)f->a[1], (int)f->a[2]);
    case F_GETFL:
        rc = sys_fcntl(fd, F_GETFL, 0);
        return rc < 0 ? rc : (int64_t)f->abi->from_open_flags(rc);
    case F_SETFL:
        return sys_fcntl(fd, F_SETFL, f->abi->open_flags(f->a[2]));
    default:
        return -EINVAL;
    }
}

/*
 * ioctl(2): the termio requests, which are the same everywhere.  Any other
 * request is the personality's to translate, and reaches here only if it
 * did not: it fails as it would on something that is not a terminal.
 */
static int64_t sysv386_sys_ioctl(struct sysv386_frame *f) {
    int fd = (int)f->a[0];
    uint32_t cmd = f->a[1];
    uint32_t arg = f->a[2];
    struct termios native;
    struct sysv_termio user;
    int rc;

    switch (cmd) {
    case SYSV_TCGETA:
        rc = kern_ioctl(fd, TCGETS, &native);
        if (rc != 0) {
            return rc;
        }
        sysv_termios_to_termio(&user, &native);
        return sysv386_put(arg, &user, sizeof(user));
    case SYSV_TCSETA:
    case SYSV_TCSETAW:
    case SYSV_TCSETAF:
        /* termio cannot express everything termios holds: start from what
         * the terminal has. */
        rc = kern_ioctl(fd, TCGETS, &native);
        if (rc != 0) {
            return rc;
        }
        rc = sysv386_get(arg, &user, sizeof(user));
        if (rc != 0) {
            return rc;
        }
        sysv_termio_to_termios(&native, &user);
        return kern_ioctl(fd, cmd == SYSV_TCSETA ? TCSETS :
                              cmd == SYSV_TCSETAW ? TCSETSW : TCSETSF,
                          &native);
    case SYSV_TCSBRK:
        return kern_ioctl(fd, TCSBRK, (void *)(uintptr_t)arg);
    case SYSV_TCXONC:
        return kern_ioctl(fd, TCXONC, (void *)(uintptr_t)arg);
    case SYSV_TCFLSH:
        return kern_ioctl(fd, TCFLSH, (void *)(uintptr_t)arg);
    default:
        return -ENOTTY;
    }
}

/* struct utsname as utssys(2) fills it: five nine-byte names. */
#define SYSV_NMLN 9
struct sysv386_utsname {
    char sysname[SYSV_NMLN];
    char nodename[SYSV_NMLN];
    char release[SYSV_NMLN];
    char version[SYSV_NMLN];
    char machine[SYSV_NMLN];
};

/* utssys(buf, mv, type): type 0 is uname. */
static int64_t sysv386_sys_utssys(struct sysv386_frame *f) {
    struct utsname native;
    struct sysv386_utsname out;
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
    strlcpy(out.sysname, f->abi->sysname, sizeof(out.sysname));
    strlcpy(out.nodename, native.nodename, sizeof(out.nodename));
    strlcpy(out.release, f->abi->release, sizeof(out.release));
    strlcpy(out.version, f->abi->version, sizeof(out.version));
    strlcpy(out.machine, f->abi->machine, sizeof(out.machine));
    return sysv386_put(f->a[0], &out, sizeof(out));
}

static void sysv386_free_vector(char **vec, size_t slots) {
    size_t i;

    if (!vec) {
        return;
    }
    for (i = 0; i < slots; i++) {
        sysv386_free_string(vec[i]);
    }
    kfree(vec, slots * sizeof(char *));
}

/* A NULL-terminated array of 32-bit pointers to strings. */
static int sysv386_copy_vector(uint32_t addr, char ***out, size_t *slots_out) {
    char **vec;
    size_t n = 0, i;

    *out = NULL;
    *slots_out = 0;
    if (addr == 0) {
        return 0;
    }
    for (;;) {
        uint32_t p;

        if (n >= SYSV386_MAX_VEC) {
            return -E2BIG;
        }
        if (sysv386_get(addr + (uint32_t)n * 4U, &p, sizeof(p)) != 0) {
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
        int rc = sysv386_get(addr + (uint32_t)i * 4U, &p, sizeof(p));

        if (rc == 0) {
            rc = sysv386_string(p, &vec[i]);
        }
        if (rc != 0) {
            sysv386_free_vector(vec, n + 1U);
            return rc;
        }
    }
    *out = vec;
    *slots_out = n + 1U;
    return 0;
}

static int64_t sysv386_sys_execve(struct sysv386_frame *f) {
    char *path = NULL;
    char **argv = NULL, **envp = NULL;
    size_t argv_slots = 0, envp_slots = 0;
    int rc = sysv386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    rc = sysv386_copy_vector(f->a[1], &argv, &argv_slots);
    if (rc == 0) {
        rc = sysv386_copy_vector(f->a[2], &envp, &envp_slots);
    }
    if (rc == 0) {
        rc = kern_execve(path, argv, envp);
    }
    sysv386_free_vector(argv, argv_slots);
    sysv386_free_vector(envp, envp_slots);
    sysv386_free_string(path);
    return rc;
}

static int64_t sysv386_sys_exec(struct sysv386_frame *f) {
    /* exec(path, argv), from before there was an environment to pass. */
    struct sysv386_frame local = *f;

    local.a[2] = 0;
    return sysv386_sys_execve(&local);
}

/*
 * signal(2), and the calls that share its number: sigset, sighold,
 * sigrelse, sigignore and sigpause.
 *
 * The stub hands over more than its arguments: EDX holds the address of
 * libc's return trampoline,
 *
 *     add   $4,%esp          ; drop the signal number
 *     lcall $0xf,$0          ; and return from the signal
 *
 * which is where a handler has to return to.  It is kept per signal, in
 * the slot the Linux personality uses for the same purpose (sa_restorer).
 */
static int64_t sysv386_sys_signal(struct sysv386_frame *f) {
    uint32_t variant = f->a[0] & ~SYSV_SIGNO_MASK;
    int sig = f->abi->signo(f->a[0] & SYSV_SIGNO_MASK);
    struct sigaction act, old;
    uint32_t bit;
    int rc;

    if (sig < 0) {
        return sig;
    }
    bit = 1U << (sig - 1);

    switch (variant) {
    case SYSV_SIG_HOLD:
        return kern_sigprocmask(SYSV386_MASK_BLOCK, &bit, NULL);
    case SYSV_SIG_RELSE:
        return kern_sigprocmask(SYSV386_MASK_UNBLOCK, &bit, NULL);
    case SYSV_SIG_PAUSE: {
        uint32_t mask = 0;

        (void)kern_sigprocmask(SYSV386_MASK_BLOCK, NULL, &mask);
        mask &= ~bit;
        return kern_sigsuspend(&mask);
    }
    case SYSV_SIG_IGNORE:
        memset(&act, 0, sizeof(act));
        act.sa_handler = (void *)SIG_IGN;
        return kern_sigaction(sig, &act, NULL);
    case 0:
    case SYSV_SIG_SET:
        break;
    default:
        return -EINVAL;
    }

    memset(&act, 0, sizeof(act));
    memset(&old, 0, sizeof(old));
    if (variant == SYSV_SIG_SET && f->a[1] == SYSV_SIG_HOLDVAL) {
        /* sigset(sig, SIG_HOLD): block it, leave the disposition. */
        rc = kern_sigaction(sig, NULL, &old);
        if (rc == 0) {
            rc = kern_sigprocmask(SYSV386_MASK_BLOCK, &bit, NULL);
        }
    } else {
        /* SIG_DFL and SIG_IGN are 0 and 1 here as they are natively. */
        act.sa_handler = (void *)(uintptr_t)f->a[1];
        /* signal(): the disposition reverts as the handler is entered.
         * sigset(): it stays, and the signal is held while it runs. */
        if (variant == 0 && f->a[1] > SYSV_SIG_IGN) {
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
    return (int64_t)(uint32_t)(uintptr_t)old.sa_handler;
}

typedef int64_t (*sysv386_handler)(struct sysv386_frame *);

static const sysv386_handler sysv386_calls[SYSV_CALL_MAX] = {
    [SYSV_SYS_exit]    = sysv386_sys_exit,
    [SYSV_SYS_fork]    = sysv386_sys_fork,
    [SYSV_SYS_read]    = sysv386_sys_read,
    [SYSV_SYS_write]   = sysv386_sys_write,
    [SYSV_SYS_open]    = sysv386_sys_open,
    [SYSV_SYS_close]   = sysv386_sys_close,
    [SYSV_SYS_wait]    = sysv386_sys_wait,
    [SYSV_SYS_creat]   = sysv386_sys_creat,
    [SYSV_SYS_link]    = sysv386_sys_link,
    [SYSV_SYS_unlink]  = sysv386_sys_unlink,
    [SYSV_SYS_exec]    = sysv386_sys_exec,
    [SYSV_SYS_chdir]   = sysv386_sys_chdir,
    [SYSV_SYS_time]    = sysv386_sys_time,
    [SYSV_SYS_mknod]   = sysv386_sys_mknod,
    [SYSV_SYS_chmod]   = sysv386_sys_chmod,
    [SYSV_SYS_chown]   = sysv386_sys_chown,
    [SYSV_SYS_brk]     = sysv386_sys_brk,
    [SYSV_SYS_stat]    = sysv386_sys_stat,
    [SYSV_SYS_lseek]   = sysv386_sys_lseek,
    [SYSV_SYS_getpid]  = sysv386_sys_getpid,
    [SYSV_SYS_setuid]  = sysv386_sys_setuid,
    [SYSV_SYS_getuid]  = sysv386_sys_getuid,
    [SYSV_SYS_alarm]   = sysv386_sys_alarm,
    [SYSV_SYS_fstat]   = sysv386_sys_fstat,
    [SYSV_SYS_pause]   = sysv386_sys_pause,
    [SYSV_SYS_access]  = sysv386_sys_access,
    [SYSV_SYS_nice]    = sysv386_sys_nice,
    [SYSV_SYS_sync]    = sysv386_sys_sync,
    [SYSV_SYS_kill]    = sysv386_sys_kill,
    [SYSV_SYS_setpgrp] = sysv386_sys_setpgrp,
    [SYSV_SYS_dup]     = sysv386_sys_dup,
    [SYSV_SYS_pipe]    = sysv386_sys_pipe,
    [SYSV_SYS_times]   = sysv386_sys_times,
    [SYSV_SYS_setgid]  = sysv386_sys_setgid,
    [SYSV_SYS_getgid]  = sysv386_sys_getgid,
    [SYSV_SYS_signal]  = sysv386_sys_signal,
    [SYSV_SYS_ioctl]   = sysv386_sys_ioctl,
    [SYSV_SYS_utssys]  = sysv386_sys_utssys,
    [SYSV_SYS_execve]  = sysv386_sys_execve,
    [SYSV_SYS_umask]   = sysv386_sys_umask,
    [SYSV_SYS_chroot]  = sysv386_sys_chroot,
    [SYSV_SYS_fcntl]   = sysv386_sys_fcntl,
    [SYSV_SYS_ulimit]  = sysv386_sys_ulimit,
};

static const char *const sysv386_names[SYSV_CALL_MAX] = {
    [SYSV_SYS_exit] = "exit",       [SYSV_SYS_fork] = "fork",
    [SYSV_SYS_read] = "read",       [SYSV_SYS_write] = "write",
    [SYSV_SYS_open] = "open",       [SYSV_SYS_close] = "close",
    [SYSV_SYS_wait] = "wait",       [SYSV_SYS_creat] = "creat",
    [SYSV_SYS_link] = "link",       [SYSV_SYS_unlink] = "unlink",
    [SYSV_SYS_exec] = "exec",       [SYSV_SYS_chdir] = "chdir",
    [SYSV_SYS_time] = "time",       [SYSV_SYS_mknod] = "mknod",
    [SYSV_SYS_chmod] = "chmod",     [SYSV_SYS_chown] = "chown",
    [SYSV_SYS_brk] = "brk",         [SYSV_SYS_stat] = "stat",
    [SYSV_SYS_lseek] = "lseek",     [SYSV_SYS_getpid] = "getpid",
    [SYSV_SYS_setuid] = "setuid",   [SYSV_SYS_getuid] = "getuid",
    [SYSV_SYS_alarm] = "alarm",     [SYSV_SYS_fstat] = "fstat",
    [SYSV_SYS_pause] = "pause",     [SYSV_SYS_access] = "access",
    [SYSV_SYS_nice] = "nice",       [SYSV_SYS_sync] = "sync",
    [SYSV_SYS_kill] = "kill",       [SYSV_SYS_setpgrp] = "setpgrp",
    [SYSV_SYS_dup] = "dup",         [SYSV_SYS_pipe] = "pipe",
    [SYSV_SYS_times] = "times",     [SYSV_SYS_setgid] = "setgid",
    [SYSV_SYS_getgid] = "getgid",   [SYSV_SYS_signal] = "signal",
    [SYSV_SYS_ioctl] = "ioctl",     [SYSV_SYS_utssys] = "utssys",
    [SYSV_SYS_execve] = "execve",   [SYSV_SYS_umask] = "umask",
    [SYSV_SYS_chroot] = "chroot",   [SYSV_SYS_fcntl] = "fcntl",
    [SYSV_SYS_ulimit] = "ulimit",
};

int64_t sysv386_call(struct sysv386_frame *f, int *known) {
    sysv386_handler fn = (f->nr < SYSV_CALL_MAX) ? sysv386_calls[f->nr]
                                                  : NULL;

    *known = fn != NULL;
    return fn ? fn(f) : -EINVAL;
}

/* ---- entry ----------------------------------------------------------- */

/*
 * The linear address of selector:offset.  An x.out program's selectors are
 * LDT entries; an ELF program's are the flat GDT ones.
 */
static int sysv386_linear(uint16_t selector, uint32_t offset,
                          uintptr_t *linear_out) {
    const gdt_entry_t *entry;
    unsigned int index;

    if (!current_process) {
        return -EINVAL;
    }
    if ((selector & 0x4U) == 0) {
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
    entry = &((const gdt_entry_t *)current_process->ldt)[index];
    if ((entry->access & 0x80U) == 0 || (entry->access & 0x10U) == 0) {
        return -EINVAL;   /* not present, or not a code/data segment */
    }
    if (offset > ldt_entry_limit(entry)) {
        return -EFAULT;
    }
    *linear_out = (uintptr_t)ldt_entry_base(entry) + (uintptr_t)offset;
    return 0;
}

/* The gate selector of the lcall at CS:EIP, or 0 if that is not one. */
static uint16_t sysv386_lcall_gate(registers_t *regs) {
    uintptr_t linear_ip;
    uint8_t insn[SYSV386_LCALL_LEN];

    if (sysv386_linear((uint16_t)regs->cs, regs->eip, &linear_ip) != 0 ||
        linear_ip >= USER32_VA_END - sizeof(insn)) {
        return 0;
    }
    /* Read directly: the instruction has just been fetched, so it is
     * mapped, and copyin() refuses the first page of the address space --
     * which is where a small x.out program's stubs are, text starting at
     * address 0. */
    memcpy(insn, (const void *)linear_ip, sizeof(insn));
    if (insn[0] != 0x9AU) {           /* far CALL ptr16:32 */
        return 0;
    }
    return (uint16_t)((uint16_t)insn[5] | ((uint16_t)insn[6] << 8));
}

static int sysv386_syscall(registers_t *regs, const struct sysv386_abi *abi) {
    struct sysv386_frame f;
    uintptr_t linear_sp;
    int known = 1;
    void *saved_syscall_regs;
    int64_t ret;

    memset(&f, 0, sizeof(f));
    f.regs = regs;
    f.abi  = abi;
    f.nr   = regs->eax & 0xFFU;
    f.sub  = (regs->eax >> 8) & 0xFFU;

    /* The arguments are the caller's, above the stub's return address. */
    if (sysv386_linear((uint16_t)regs->ss, regs->useresp, &linear_sp) == 0 &&
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
    regs->eip += SYSV386_LCALL_LEN;

    /* fork finds the frame to clone through syscall_regs, which only the
     * native entry path sets. */
    saved_syscall_regs = current_thread ? current_thread->syscall_regs : NULL;
    if (current_thread) {
        current_thread->syscall_regs = regs;
    }

    ret = abi->call(&f, &known);
    if (!known) {
        known = 1;
        ret = sysv386_call(&f, &known);
    }
    if (!known) {
        ret = -abi->nosys;
    }

    if (current_thread) {
        current_thread->syscall_regs = saved_syscall_regs;
    }

    if (abi->trace && abi->trace()) {
        char buf[160];
        const char *name = abi->call_name ? abi->call_name(f.nr, f.sub)
                                          : NULL;

        if (!name && f.nr < SYSV_CALL_MAX) {
            name = sysv386_names[f.nr];
        }
        snprintf(buf, sizeof(buf),
                 "%s: [%d] %s/%u.%u(%#x, %#x, %#x, %#x) = %lld%s\n", abi->tag,
                 current_process ? (int)current_process->pid : -1,
                 name ? name : "sys", f.nr, f.sub, f.a[0], f.a[1], f.a[2],
                 f.a[3], (long long)(ret < 0 ? ret : (int64_t)(uint32_t)ret),
                 known ? "" : " [unimplemented]");
        kprint(buf);
    }

    if (ret < 0 && abi->fix_errno) {
        ret = abi->fix_errno(ret);
    }
    /* Carry and an errno, or the result in EAX with its second half, if it
     * has one, in EDX. */
    if (ret < 0) {
        regs->eax = (uint32_t)(-ret);
        regs->eflags |= SYSV386_EFLAGS_CF;
    } else {
        regs->eax = (uint32_t)ret;
        regs->edx = (uint32_t)((uint64_t)ret >> 32);
        regs->eflags &= ~SYSV386_EFLAGS_CF;
    }
    return 1;
}

/* ---- signals --------------------------------------------------------- */

/*
 * A handler is entered as handler(signo, ...) with libc's trampoline for a
 * return address, and above the arguments -- where the trampoline's
 * `lcall $0xf,$0` finds it once the signal number is popped and the other
 * arguments skipped -- what is needed to put the interrupted program back:
 *
 *      ESP+0             trampoline
 *      ESP+4             signo
 *      ESP+8 ...         further arguments, null (abi->sig_args - 1)
 *      then              struct sysv386_sigcontext
 *
 * The context is this kernel's own business: libc never looks inside it.
 */
struct sysv386_sigcontext {
    uint32_t magic;
    uint32_t eip, eflags, esp;
    uint32_t eax, ecx, edx, ebx, ebp, esi, edi;
    uint32_t mask;
};
#define SYSV386_SIGCTX_MAGIC 0x58334753U   /* "SG3X" */
#define SYSV386_SIG_MAXARGS  3U

void sysv386_sendsig(const struct sysv386_abi *abi, void *handler, int sig,
                     uint32_t mask, registers_t *regs) {
    uint32_t words[1U + SYSV386_SIG_MAXARGS];
    struct sysv386_sigcontext ctx;
    uint32_t tramp = 0, head, sp;

    if (!regs || !current_process || abi->sig_args < 1U ||
        abi->sig_args > SYSV386_SIG_MAXARGS) {
        return;
    }
    if (sig >= 1 && sig <= NSIG) {
        tramp = (uint32_t)(uintptr_t)
                current_process->linux_sig_restorer[sig - 1];
    }
    /* No trampoline means no way back out of the handler. */
    if (tramp == 0) {
        sigexit(current_process, SIGILL);
        return;
    }

    memset(words, 0, sizeof(words));
    words[0] = tramp;
    words[1] = abi->signo_from(sig);

    ctx.magic  = SYSV386_SIGCTX_MAGIC;
    ctx.eip    = regs->eip;
    ctx.eflags = regs->eflags;
    ctx.esp    = regs->useresp;
    ctx.eax    = regs->eax;
    ctx.ecx    = regs->ecx;
    ctx.edx    = regs->edx;
    ctx.ebx    = regs->ebx;
    ctx.ebp    = regs->ebp;
    ctx.esi    = regs->esi;
    ctx.edi    = regs->edi;
    ctx.mask   = mask;

    head = (1U + abi->sig_args) * (uint32_t)sizeof(uint32_t);
    sp = (regs->useresp - head - (uint32_t)sizeof(ctx)) & ~3U;
    if (sysv386_put(sp, words, head) != 0 ||
        sysv386_put(sp + head, &ctx, sizeof(ctx)) != 0) {
        sigexit(current_process, SIGSEGV);
        return;
    }

    regs->useresp = sp;
    regs->eip = (uint32_t)(uintptr_t)handler;
    regs->eflags &= ~SYSV386_EFLAGS_DF;   /* clear on entry to C */
}

static int sysv386_sigreturn(registers_t *regs,
                             const struct sysv386_abi *abi) {
    struct sysv386_sigcontext ctx;
    /* The trampoline has popped the signal number; the rest remain. */
    uint32_t at = regs->useresp +
                  (abi->sig_args - 1U) * (uint32_t)sizeof(uint32_t);

    if (sysv386_get(at, &ctx, sizeof(ctx)) != 0 ||
        ctx.magic != SYSV386_SIGCTX_MAGIC) {
        sigexit(current_process, SIGSEGV);
        return 1;
    }
    regs->eip = ctx.eip;
    regs->useresp = ctx.esp;
    regs->eflags = (regs->eflags & ~SYSV386_EFLAGS_USER) |
                   (ctx.eflags & SYSV386_EFLAGS_USER);
    regs->eax = ctx.eax;
    regs->ecx = ctx.ecx;
    regs->edx = ctx.edx;
    regs->ebx = ctx.ebx;
    regs->ebp = ctx.ebp;
    regs->esi = ctx.esi;
    regs->edi = ctx.edi;
    (void)kern_sigprocmask(SYSV386_MASK_SET, &ctx.mask, NULL);
    return 1;
}

int sysv386_handle_trap(registers_t *regs, const struct sysv386_abi *abi) {
    if (!regs || !current_process) {
        return 0;
    }
    /* An lcall through a selector with nothing behind it: #GP or #NP. */
    if (regs->int_no != 11 && regs->int_no != 13) {
        return 0;
    }
    switch (sysv386_lcall_gate(regs)) {
    case SYSV386_GATE_SEL:
        return sysv386_syscall(regs, abi);
    case SYSV386_SIGRET_SEL:
        return sysv386_sigreturn(regs, abi);
    default:
        return 0;
    }
}
