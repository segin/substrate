/*
 * sunos_user.c - the SunOS 4.0 (Sun386i) calls whose arguments or results
 * are not substrate's own.
 *
 * What each does is the section 2 manual page's account of it; what it is
 * passed is the Developer's Toolkit's header (sunos_user.h says which).
 * Signals are in sunos_sig.c.
 */

#include <stddef.h>
#include <string.h>

#include <exec/perso/compat.h>
#include <exec/perso/freebsd/freebsd_user.h>
#include <exec/perso/personality.h>
#include <exec/perso/sunos/sunos_user.h>
#include <exec/perso/svr4/svr4_tty.h>
#include <kern/sched.h>
#include <machine/idt.h>
#include <machine/vmparam.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/kern_syscalls.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/syscall_impl.h>
#include <sys/termios.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>
#include <vm/vm_map.h>

#define SUNOS_EFLAGS_CF 0x00000001U

/* A second result, for the calls that give two: it goes back in %edx. */
static int64_t two_results(uint32_t first, uint32_t second) {
    return (int64_t)(((uint64_t)second << 32) | first);
}

/* ---- processes ------------------------------------------------------- */

/*
 * fork(2).  libc's stub tells the child by %edx, which the kernel sets to
 * 1 there and 0 in the parent.  The child resumes from a copy of this
 * frame with only %eax forced to zero, so what marks it has to be in the
 * frame before the fork.
 */
static int64_t fork_marked(int (*do_fork)(void)) {
    registers_t *regs = current_thread
        ? (registers_t *)current_thread->syscall_regs : NULL;
    int pid;

    if (!regs) {
        return -EINVAL;
    }
    regs->edx = 1;
    regs->eflags &= ~SUNOS_EFLAGS_CF;
    pid = do_fork();
    if (pid < 0) {
        return pid;
    }
    return two_results((uint32_t)pid, 0);
}

int64_t sunos_sys_fork(void) {
    return fork_marked(sys_fork);
}

int64_t sunos_sys_vfork(void) {
    return fork_marked(sys_vfork);
}

/* getpid(2), getuid(2), getgid(2): getppid(), geteuid() and getegid() are
 * the same calls, and take %edx. */
int64_t sunos_sys_getpid(void) {
    return two_results((uint32_t)sys_getpid(), (uint32_t)sys_getppid());
}

int64_t sunos_sys_getuid(void) {
    return two_results((uint32_t)sys_getuid(), (uint32_t)sys_geteuid());
}

int64_t sunos_sys_getgid(void) {
    return two_results((uint32_t)sys_getgid(), (uint32_t)sys_getegid());
}

/* wait4(2): "With a pid argument of 0, it is equivalent to wait3". */
int sunos_sys_wait4(int pid, int *status, int options, void *rusage) {
    return freebsd_sys_wait4(pid == SUNOS_WAIT_ANY ? -1 : pid, status,
                             options, rusage);
}

/* getpgrp(2): of the process named, 0 being the caller. */
int sunos_sys_getpgrp(int pid) {
    return sys_getpgid(pid);
}

int sunos_sys_killpg(int pgrp, int sig) {
    if (pgrp <= 0) {
        return -EINVAL;
    }
    return freebsd_sys_kill(-pgrp, sig);
}

/* ---- files ----------------------------------------------------------- */

static int open_flags(int f) {
    int n = f & SUNOS_O_ACCMODE;

    if (f & (SUNOS_FNDELAY | SUNOS_FNBIO)) n |= O_NONBLOCK;
    if (f & SUNOS_FAPPEND) n |= O_APPEND;
    if (f & SUNOS_FCREAT)  n |= O_CREAT;
    if (f & SUNOS_FTRUNC)  n |= O_TRUNC;
    if (f & SUNOS_FEXCL)   n |= O_EXCL;
    if (f & SUNOS_FSYNC)   n |= O_SYNC;
    return n;
}

static int status_flags(int n) {
    int f = n & O_ACCMODE;

    if (n & O_NONBLOCK) f |= SUNOS_FNDELAY;
    if (n & O_APPEND)   f |= SUNOS_FAPPEND;
    if (n & O_SYNC)     f |= SUNOS_FSYNC;
    return f;
}

/*
 * A path argument, as the name this personality means by it: one under
 * /perso/sunos if the file is there or would be made there
 * (perso_tree_path).  Every call that takes a path takes it through here,
 * so that a file made as /tmp/x is the /tmp/x that is then found, listed
 * and removed.  The strings that are not names of files to this call --
 * what a symbolic link is to say, the arguments of a new program -- are
 * left as they are.
 */
struct sunos_path {
    char given[SUNOS_PATH_MAX];
    char tree[SUNOS_TREE_PATH_MAX];
    const char *name;
};

static int get_path(const char *upath, struct sunos_path *p) {
    COPYIN_STR(upath, p->given);
    p->name = perso_tree_path(p->given, p->tree, sizeof(p->tree))
        ? p->tree : p->given;
    return 0;
}

/* Run `call` on the path `upath`. */
#define SUNOS_WITH_PATH(upath, call) do {                               \
    struct sunos_path *sp_ = kmalloc(sizeof(*sp_));                     \
    int rc_;                                                            \
    if (!sp_) return -ENOMEM;                                           \
    rc_ = get_path((upath), sp_);                                       \
    if (rc_ == 0) { const char *path_ = sp_->name; rc_ = (call); }      \
    kfree(sp_, sizeof(*sp_));                                           \
    return rc_;                                                         \
} while (0)

/* And on two. */
#define SUNOS_WITH_PATHS(upath1, upath2, call) do {                     \
    struct sunos_path *sp_ = kmalloc(2 * sizeof(*sp_));                 \
    int rc_;                                                            \
    if (!sp_) return -ENOMEM;                                           \
    rc_ = get_path((upath1), &sp_[0]);                                  \
    if (rc_ == 0) rc_ = get_path((upath2), &sp_[1]);                    \
    if (rc_ == 0) {                                                     \
        const char *path1_ = sp_[0].name, *path2_ = sp_[1].name;        \
        rc_ = (call);                                                   \
    }                                                                   \
    kfree(sp_, 2 * sizeof(*sp_));                                       \
    return rc_;                                                         \
} while (0)

int sunos_sys_open(const char *path, int flags, int mode) {
    SUNOS_WITH_PATH(path, kern_open(path_, open_flags(flags), mode));
}

int sunos_sys_creat(const char *path, int mode) {
    SUNOS_WITH_PATH(path, kern_open(path_, O_WRONLY | O_CREAT | O_TRUNC,
                                    mode));
}

int sunos_sys_link(const char *from, const char *to) {
    SUNOS_WITH_PATHS(from, to, kern_link(path1_, path2_));
}

int sunos_sys_rename(const char *from, const char *to) {
    SUNOS_WITH_PATHS(from, to, kern_rename(path1_, path2_));
}

int sunos_sys_unlink(const char *path) {
    SUNOS_WITH_PATH(path, kern_unlink(path_));
}

int sunos_sys_chdir(const char *path) {
    SUNOS_WITH_PATH(path, kern_chdir(path_));
}

int sunos_sys_mkdir(const char *path, int mode) {
    SUNOS_WITH_PATH(path, kern_mkdir(path_, mode));
}

int sunos_sys_rmdir(const char *path) {
    SUNOS_WITH_PATH(path, kern_rmdir(path_));
}

int sunos_sys_mknod(const char *path, int mode, int dev) {
    SUNOS_WITH_PATH(path, kern_mknod(path_, mode, dev));
}

int sunos_sys_chmod(const char *path, int mode) {
    SUNOS_WITH_PATH(path, kern_chmodat(AT_FDCWD, path_, mode, 0));
}

/* chown(2): an owner or group of -1 is left as it is, and a symbolic
 * link is followed. */
int sunos_sys_chown(const char *path, int uid, int gid) {
    SUNOS_WITH_PATH(path, kern_fchownat(AT_FDCWD, path_, uid, gid, 0));
}

int sunos_sys_access(const char *path, int mode) {
    SUNOS_WITH_PATH(path, kern_access(path_, mode));
}

/* symlink(2): what the link says is the program's business; where the
 * link is made is a path. */
int sunos_sys_symlink(const char *text, const char *path) {
    struct sunos_path *sp = kmalloc(sizeof(*sp));
    char *target = kmalloc(SUNOS_PATH_MAX);
    int rc;

    if (!sp || !target) {
        rc = -ENOMEM;
    } else {
        rc = get_path(path, sp);
        if (rc == 0) {
            rc = -copyinstr(text, target, SUNOS_PATH_MAX, NULL);
        }
        if (rc == 0) {
            rc = kern_symlink(target, sp->name);
        }
    }
    if (target) kfree(target, SUNOS_PATH_MAX);
    if (sp) kfree(sp, sizeof(*sp));
    return rc;
}

int sunos_sys_readlink(const char *path, char *buf, int size) {
    struct sunos_path *sp;
    char *text;
    int rc;

    if (size <= 0) {
        return -EINVAL;
    }
    if (size > SUNOS_PATH_MAX) {
        size = SUNOS_PATH_MAX;
    }
    sp = kmalloc(sizeof(*sp));
    text = kmalloc(SUNOS_PATH_MAX);
    if (!sp || !text) {
        rc = -ENOMEM;
    } else {
        rc = get_path(path, sp);
        if (rc == 0) {
            rc = kern_readlink(sp->name, text, (size_t)size);
        }
        if (rc > 0 && copyout(text, buf, (size_t)rc) != 0) {
            rc = -EFAULT;
        }
    }
    if (text) kfree(text, SUNOS_PATH_MAX);
    if (sp) kfree(sp, sizeof(*sp));
    return rc;
}

/* stat(2), lstat(2): 4.3BSD's struct stat. */
static int put_stat(const struct stat *n, struct freebsd_ostat *buf) {
    struct freebsd_ostat s;

    memset(&s, 0, sizeof(s));
    s.st_dev = (uint16_t)n->st_dev;
    s.st_ino = (uint32_t)n->st_ino;
    s.st_mode = (uint16_t)n->st_mode;
    s.st_nlink = (uint16_t)n->st_nlink;
    s.st_uid = (uint16_t)n->st_uid;
    s.st_gid = (uint16_t)n->st_gid;
    s.st_rdev = (uint16_t)n->st_rdev;
    s.st_size = (int32_t)n->st_size;
    s.st_atim_sec = (int32_t)n->st_atime;
    s.st_mtim_sec = (int32_t)n->st_mtime;
    s.st_ctim_sec = (int32_t)n->st_ctime;
    s.st_blksize = (int32_t)n->st_blksize;
    s.st_blocks = (int32_t)n->st_blocks;
    return copyout(&s, buf, sizeof(s)) != 0 ? -EFAULT : 0;
}

static int stat_path(const char *path, struct freebsd_ostat *buf,
                     int (*how)(const char *, struct stat *)) {
    struct sunos_path *sp = kmalloc(sizeof(*sp));
    struct stat n;
    int rc;

    if (!sp) {
        return -ENOMEM;
    }
    rc = get_path(path, sp);
    if (rc == 0) {
        rc = how(sp->name, &n);
    }
    kfree(sp, sizeof(*sp));
    return rc != 0 ? rc : put_stat(&n, buf);
}

int sunos_sys_stat(const char *path, struct freebsd_ostat *buf) {
    return stat_path(path, buf, kern_stat);
}

int sunos_sys_lstat(const char *path, struct freebsd_ostat *buf) {
    return stat_path(path, buf, kern_lstat);
}

/*
 * dup(2).  dup2() was once this call with a bit set in the descriptor and
 * the wanted one as a second argument, and the Bourne shell still makes
 * it that way.
 */
int sunos_sys_dup(int fd, int to) {
    if (fd & SUNOS_DUP_TO) {
        return sys_dup2(fd & ~SUNOS_DUP_TO, to);
    }
    return sys_dup(fd);
}

int sunos_sys_fcntl(int fd, int cmd, int arg) {
    int rc;

    switch (cmd) {
    case SUNOS_F_DUPFD:
    case SUNOS_F_GETFD:
    case SUNOS_F_SETFD:
        return sys_fcntl(fd, cmd, arg);
    case SUNOS_F_GETFL:
        rc = sys_fcntl(fd, F_GETFL, 0);
        return rc < 0 ? rc : status_flags(rc);
    case SUNOS_F_SETFL:
        return sys_fcntl(fd, F_SETFL, open_flags(arg));
    case SUNOS_F_GETOWN:
        return sys_fcntl(fd, F_GETOWN, arg);
    case SUNOS_F_SETOWN:
        return sys_fcntl(fd, F_SETOWN, arg);
    default:
        /* The record locks: struct flock's types are numbered otherwise
         * and nothing run so far takes one. */
        return -EINVAL;
    }
}

/* utimes(2): access and modification times as two timevals, or the
 * present for both if there are none. */
int sunos_sys_utimes(const char *path, const void *tvp) {
    struct timespec ts[2];
    int32_t tv[4];

    if (tvp) {
        if (copyin(tvp, tv, sizeof(tv)) != 0) {
            return -EFAULT;
        }
        ts[0].tv_sec = tv[0];
        ts[0].tv_nsec = (long)tv[1] * 1000L;
        ts[1].tv_sec = tv[2];
        ts[1].tv_nsec = (long)tv[3] * 1000L;
    }
    SUNOS_WITH_PATH(path, kern_utimensat(AT_FDCWD, path_, tvp ? ts : NULL, 0));
}

static int truncate_path(const char *path, uint32_t length) {
    int fd = kern_open(path, O_WRONLY, 0);
    int rc;

    if (fd < 0) {
        return fd;
    }
    rc = sys_ftruncate(fd, length, 0);
    kern_close(fd);
    return rc;
}

int sunos_sys_truncate(const char *path, int32_t length) {
    if (length < 0) {
        return -EINVAL;
    }
    SUNOS_WITH_PATH(path, truncate_path(path_, (uint32_t)length));
}

int sunos_sys_ftruncate(int fd, int32_t length) {
    if (length < 0) {
        return -EINVAL;
    }
    return sys_ftruncate(fd, (uint32_t)length, 0);
}

/*
 * getdents(2): "puts directory entries from the directory referenced by
 * the file descriptor fd into the buffer pointed to by buf, in a
 * filesystem independent format".  Each is a struct dirent cut to its
 * name and rounded to a long; d_off is where the next one starts.
 */
int sunos_sys_getdents(int fd, char *buf, uint32_t nbytes) {
    struct sunos_dirent out;
    struct dirent dent;
    file_t *f;
    char *kbuf;
    uint32_t pos = 0;

    if (fd < 0 || fd >= MAX_FD || !current_process) {
        return -EBADF;
    }
    f = current_process->fds[fd];
    if (!f || !f->f_data) {
        return -EBADF;
    }
    if (nbytes > 65536U) {
        nbytes = 65536U;
    }
    kbuf = kmalloc(nbytes);
    if (!kbuf) {
        return -ENOMEM;
    }
    for (;;) {
        struct dirent *d = readdir_fs((fs_node_t *)f->f_data,
                                      (uint32_t)f->f_offset, &dent);
        uint64_t here, next;
        uint32_t namlen, reclen;

        if (!d) {
            break;
        }
        /* The directory's own cursor, or the next index where the
         * filesystem numbers its entries and leaves d_off alone. */
        here = (uint64_t)f->f_offset;
        next = d->d_off > here ? d->d_off : here + 1;
        namlen = (uint32_t)strnlen(d->d_name, 255);
        reclen = (SUNOS_DIRENT_HDR + namlen + 1U + 3U) & ~3U;
        if (pos + reclen > nbytes) {
            if (pos == 0) {
                kfree(kbuf, nbytes);
                return -EINVAL;
            }
            break;
        }
        memset(&out, 0, sizeof(out));
        out.d_off = (int32_t)next;
        out.d_fileno = (uint32_t)d->d_ino;
        out.d_reclen = (uint16_t)reclen;
        out.d_namlen = (uint16_t)namlen;
        memcpy(out.d_name, d->d_name, namlen);
        memcpy(kbuf + pos, &out, reclen);
        pos += reclen;
        f->f_offset = (off_t)next;
    }
    if (pos != 0 && copyout(kbuf, buf, pos) != 0) {
        kfree(kbuf, nbytes);
        return -EFAULT;
    }
    kfree(kbuf, nbytes);
    return (int)pos;
}

static int32_t clamp_long(uint64_t v) {
    return v > 0x7fffffffULL ? 0x7fffffff : (int32_t)v;
}

static int put_statfs(const struct statfs *n, struct sunos_statfs *buf) {
    struct sunos_statfs s;

    memset(&s, 0, sizeof(s));
    s.f_bsize = clamp_long(n->f_bsize);
    s.f_blocks = clamp_long(n->f_blocks);
    s.f_bfree = clamp_long(n->f_bfree);
    s.f_bavail = clamp_long(n->f_bavail);
    s.f_files = clamp_long(n->f_files);
    s.f_ffree = clamp_long(n->f_ffree);
    s.f_fsid[0] = (int32_t)n->f_fsid;
    return copyout(&s, buf, sizeof(s)) != 0 ? -EFAULT : 0;
}

/*
 * statfs(2): "returns information about a mounted file system.  path is
 * the path name of any file within the mounted filesystem."  Substrate's
 * filesystems answer for their directories only, so for any other file
 * the answer is its directory's, which is in the same filesystem.
 */
static int statfs_path(const char *path, struct sunos_statfs *buf) {
    struct statfs n;
    int rc = kern_statfs(path, &n);

    if (rc == -ENOSYS) {
        size_t len = strlen(path) + 1U;
        char *dir = kmalloc(len);
        char *slash;

        if (!dir) {
            return -ENOMEM;
        }
        memcpy(dir, path, len);
        slash = strrchr(dir, '/');
        if (!slash) {
            rc = kern_statfs(".", &n);
        } else {
            slash[slash == dir ? 1 : 0] = '\0';
            rc = kern_statfs(dir, &n);
        }
        kfree(dir, len);
    }
    return rc != 0 ? rc : put_statfs(&n, buf);
}

int sunos_sys_statfs(const char *path, struct sunos_statfs *buf) {
    SUNOS_WITH_PATH(path, statfs_path(path_, buf));
}

/* fstatfs(2), of an open file: where the file is not a directory there
 * is no name to find its directory by, and what a program wants of it --
 * a block size to do its I/O in -- is the file's own. */
int sunos_sys_fstatfs(int fd, struct sunos_statfs *buf) {
    struct statfs n;
    struct stat st;
    int rc = kern_fstatfs(fd, &n);

    if (rc == -ENOSYS) {
        rc = kern_fstat(fd, &st);
        if (rc == 0) {
            memset(&n, 0, sizeof(n));
            n.f_bsize = (uint64_t)st.st_blksize;
        }
    }
    return rc != 0 ? rc : put_statfs(&n, buf);
}

/* ---- memory ---------------------------------------------------------- */

/* brk(2): "returns 0 on success".  The kernel's own answers with where
 * the break now is. */
int sunos_sys_brk(void *addr) {
    void *now = sys_brk(addr);

    return now == addr ? 0 : -ENOMEM;
}

int sunos_sys_getpagesize(void) {
    return PAGE_SIZE;
}

/*
 * mmap(2).  The share type and MAP_FIXED are numbered as substrate's are;
 * the other flags are advice.  "A successful mmap() returns the address
 * at which the mapping was placed" -- when libc's mmap() made the call,
 * which says so with _MAP_NEW; the interface before it returned 0.
 */
int64_t sunos_sys_mmap(void *addr, uint32_t len, int prot, uint32_t flags,
                       int fd, int32_t off) {
    int native = (int)(flags & (SUNOS_MAP_TYPE | SUNOS_MAP_FIXED));
    void *at;

    /*
     * Where the system chooses, it chooses well above the program.  A
     * Sun386i program starts at 0x1000 with its heap growing up behind
     * it, and the lowest free address -- what the map offers by default --
     * is the page at 0, which must stay unmapped, or the heap's way.
     */
    if (!(flags & SUNOS_MAP_FIXED)) {
        uintptr_t where = 0;
        size_t span = ((size_t)len + PAGE_SIZE - 1) & ~((size_t)PAGE_SIZE - 1);

        if (len == 0 || !current_process || !current_process->vm_map) {
            return -EINVAL;
        }
        if (vm_map_find_space_from(current_process->vm_map, SUNOS_MMAP_BASE,
                                   &where, span) != 0) {
            return -ENOMEM;
        }
        addr = (void *)where;
        native |= SUNOS_MAP_FIXED;
    }
    at = sys_mmap_off32(addr, len, prot & 7, native, fd, off);

    if ((uintptr_t)at >= (uintptr_t)USER32_VA_END) {
        int32_t err = (int32_t)(intptr_t)at;

        /* A failure is -errno, or the bare -1 some paths still give. */
        return (err < 0 && err > -4096 && err != -1) ? err : -ENOMEM;
    }
    return (flags & SUNOS_MAP_NEW) ? (int64_t)(uint32_t)(uintptr_t)at : 0;
}

/* ---- the system ------------------------------------------------------ */

int sunos_sys_gethostname(char *name, int len) {
    char host[65];
    int rc, n;

    if (len <= 0) {
        return -EINVAL;
    }
    memset(host, 0, sizeof(host));
    rc = kern_hostname(host, sizeof(host) - 1);
    if (rc != 0) {
        return rc;
    }
    n = (int)strnlen(host, sizeof(host) - 1) + 1;
    if (n > len) {
        n = len;
    }
    return copyout(host, name, (size_t)n) != 0 ? -EFAULT : 0;
}

/* getdomainname(2): the NIS domain.  There is none, which is what a
 * machine outside any domain answers: the empty string. */
int sunos_sys_getdomainname(char *name, int len) {
    char empty = '\0';

    if (len <= 0) {
        return -EINVAL;
    }
    return copyout(&empty, name, 1) != 0 ? -EFAULT : 0;
}

/* getrlimit(2), setrlimit(2): the six resources 4.3BSD has, which are
 * substrate's first six.  Its "no limit" is all ones; SunOS's is the
 * largest positive long. */
int sunos_sys_getrlimit(int resource, void *rlp) {
    struct rlimit kl;
    int32_t out[2];

    if (resource < 0 || resource >= SUNOS_RLIM_NLIMITS) {
        return -EINVAL;
    }
    if (!current_process) {
        return -EINVAL;
    }
    kl = current_process->rlimits[resource];
    out[0] = kl.rlim_cur > (rlim_t)SUNOS_RLIM_INFINITY
        ? SUNOS_RLIM_INFINITY : (int32_t)kl.rlim_cur;
    out[1] = kl.rlim_max > (rlim_t)SUNOS_RLIM_INFINITY
        ? SUNOS_RLIM_INFINITY : (int32_t)kl.rlim_max;
    return copyout(out, rlp, sizeof(out)) != 0 ? -EFAULT : 0;
}

int sunos_sys_setrlimit(int resource, const void *rlp) {
    struct rlimit kl;
    int32_t in[2];

    if (resource < 0 || resource >= SUNOS_RLIM_NLIMITS) {
        return -EINVAL;
    }
    if (copyin(rlp, in, sizeof(in)) != 0) {
        return -EFAULT;
    }
    kl.rlim_cur = in[0] == SUNOS_RLIM_INFINITY ? RLIM_INFINITY : (rlim_t)in[0];
    kl.rlim_max = in[1] == SUNOS_RLIM_INFINITY ? RLIM_INFINITY : (rlim_t)in[1];
    return kern_native_setrlimit(resource, &kl);
}

/* A call that has nothing to do here and succeeds. */
int sunos_sys_zero(void) {
    return 0;
}

/* ---- ioctl(2) -------------------------------------------------------- */

/* Control characters: SunOS's position, substrate's. */
static const uint8_t cc_map[][2] = {
    { SUNOS_VINTR, VINTR },       { SUNOS_VQUIT, VQUIT },
    { SUNOS_VERASE, VERASE },     { SUNOS_VKILL, VKILL },
    { SUNOS_VEOL2, VEOL2 },       { SUNOS_VSTART, VSTART },
    { SUNOS_VSTOP, VSTOP },       { SUNOS_VSUSP, VSUSP },
    { SUNOS_VREPRINT, VREPRINT }, { SUNOS_VDISCARD, VDISCARD },
    { SUNOS_VWERASE, VWERASE },   { SUNOS_VLNEXT, VLNEXT },
};
#define CC_MAP_LEN (sizeof(cc_map) / sizeof(cc_map[0]))

/* The local modes: the low twelve bits are the same; FLUSHO and PENDIN
 * are not where substrate has them. */
#define SUNOS_LFLAG_SAME 0x00000fffU
#define SUNOS_FLUSHO     0x00002000U
#define SUNOS_PENDIN     0x00004000U

static void termios_out(const struct termios *n, struct sunos_termios *s) {
    size_t i;

    memset(s, 0, sizeof(*s));
    s->c_iflag = n->c_iflag & SUNOS_TERMIOS_FLAGS;
    s->c_oflag = n->c_oflag & SUNOS_TERMIOS_FLAGS;
    s->c_cflag = n->c_cflag & SUNOS_TERMIOS_FLAGS;
    /* The line's speed is in the low bits of c_cflag here, and 0 there
     * means "hang up".  Substrate's terminals have no speed; say 9600. */
    if ((s->c_cflag & SUNOS_CBAUD) == 0) {
        s->c_cflag |= SUNOS_B9600;
    }
    s->c_lflag = n->c_lflag & SUNOS_LFLAG_SAME;
    if (n->c_lflag & FLUSHO) s->c_lflag |= SUNOS_FLUSHO;
    if (n->c_lflag & PENDIN) s->c_lflag |= SUNOS_PENDIN;
    for (i = 0; i < CC_MAP_LEN; i++) {
        s->c_cc[cc_map[i][0]] = n->c_cc[cc_map[i][1]];
    }
    /* VMIN and VTIME share VEOF's and VEOL's places. */
    if (n->c_lflag & ICANON) {
        s->c_cc[SUNOS_VEOF] = n->c_cc[VEOF];
        s->c_cc[SUNOS_VEOL] = n->c_cc[VEOL];
    } else {
        s->c_cc[SUNOS_VEOF] = n->c_cc[VMIN];
        s->c_cc[SUNOS_VEOL] = n->c_cc[VTIME];
    }
}

static void termios_in(const struct sunos_termios *s, struct termios *n) {
    size_t i;

    n->c_iflag = (n->c_iflag & ~SUNOS_TERMIOS_FLAGS) |
                 (s->c_iflag & SUNOS_TERMIOS_FLAGS);
    n->c_oflag = (n->c_oflag & ~SUNOS_TERMIOS_FLAGS) |
                 (s->c_oflag & SUNOS_TERMIOS_FLAGS);
    /* All of c_cflag but the speed, which is not kept there. */
    n->c_cflag = (n->c_cflag & ~(SUNOS_TERMIOS_FLAGS & ~SUNOS_CBAUD)) |
                 (s->c_cflag & SUNOS_TERMIOS_FLAGS & ~SUNOS_CBAUD);
    n->c_lflag = (n->c_lflag & ~(SUNOS_LFLAG_SAME | FLUSHO | PENDIN)) |
                 (s->c_lflag & SUNOS_LFLAG_SAME);
    if (s->c_lflag & SUNOS_FLUSHO) n->c_lflag |= FLUSHO;
    if (s->c_lflag & SUNOS_PENDIN) n->c_lflag |= PENDIN;
    for (i = 0; i < CC_MAP_LEN; i++) {
        n->c_cc[cc_map[i][1]] = s->c_cc[cc_map[i][0]];
    }
    if (s->c_lflag & ICANON) {
        n->c_cc[VEOF] = s->c_cc[SUNOS_VEOF];
        n->c_cc[VEOL] = s->c_cc[SUNOS_VEOL];
    } else {
        n->c_cc[VMIN] = s->c_cc[SUNOS_VEOF];
        n->c_cc[VTIME] = s->c_cc[SUNOS_VEOL];
    }
}

/* TCGETS and TCGETA, TCSETS and TCSETA and their W and F forms: termio is
 * termios with shorts and the first eight characters. */
static int termios_ioctl(int fd, uint32_t key, void *arg) {
    struct termios n;
    struct sunos_termios s;
    struct sunos_termio o;
    int is_termio = key >= SUNOS_TCGETA && key <= SUNOS_TCSETAF;
    uint32_t set;
    int rc;

    rc = kern_ioctl(fd, TCGETS, &n);
    if (rc != 0) {
        return rc;
    }
    if (key == SUNOS_TCGETS || key == SUNOS_TCGETA) {
        termios_out(&n, &s);
        if (!is_termio) {
            return copyout(&s, arg, sizeof(s)) != 0 ? -EFAULT : 0;
        }
        memset(&o, 0, sizeof(o));
        o.c_iflag = (uint16_t)s.c_iflag;
        o.c_oflag = (uint16_t)s.c_oflag;
        o.c_cflag = (uint16_t)s.c_cflag;
        o.c_lflag = (uint16_t)s.c_lflag;
        memcpy(o.c_cc, s.c_cc, SUNOS_NCC);
        return copyout(&o, arg, sizeof(o)) != 0 ? -EFAULT : 0;
    }
    if (is_termio) {
        if (copyin(arg, &o, sizeof(o)) != 0) {
            return -EFAULT;
        }
        termios_out(&n, &s);
        s.c_iflag = (s.c_iflag & ~0xffffU) | o.c_iflag;
        s.c_oflag = (s.c_oflag & ~0xffffU) | o.c_oflag;
        s.c_cflag = (s.c_cflag & ~0xffffU) | o.c_cflag;
        s.c_lflag = (s.c_lflag & ~0xffffU) | o.c_lflag;
        memcpy(s.c_cc, o.c_cc, SUNOS_NCC);
        set = TCSETS + (key - SUNOS_TCSETA);
    } else {
        if (copyin(arg, &s, sizeof(s)) != 0) {
            return -EFAULT;
        }
        set = TCSETS + (key - SUNOS_TCSETS);
    }
    termios_in(&s, &n);
    return kern_ioctl(fd, set, &n);
}

/*
 * ioctl(2).  The requests that are the same thing under another number
 * are renumbered; the terminal's old interface -- TIOCGETP and the rest
 * of <sys/ttold.h>, which the shells and stty(1) still use -- is what
 * System V Release 4's ttcompat module provides, and is served by the
 * code that provides it there, the requests being numbered alike.
 */
int sunos_sys_ioctl(int fd, uint32_t request, void *arg) {
    uint32_t key = SUNOS_IOC_KEY(request);
    int64_t result = 0;

    switch (key) {
    case SUNOS_TCGETS: case SUNOS_TCSETS: case SUNOS_TCSETSW:
    case SUNOS_TCSETSF:
    case SUNOS_TCGETA: case SUNOS_TCSETA: case SUNOS_TCSETAW:
    case SUNOS_TCSETAF:
        return termios_ioctl(fd, key, arg);
    case SUNOS_TIOCGPGRP:
        return sys_ioctl(fd, TIOCGPGRP, arg);
    case SUNOS_TIOCSPGRP:
        return sys_ioctl(fd, TIOCSPGRP, arg);
    case SUNOS_TIOCGWINSZ:
        return sys_ioctl(fd, TIOCGWINSZ, arg);
    case SUNOS_TIOCSWINSZ:
        return sys_ioctl(fd, TIOCSWINSZ, arg);
    case SUNOS_FIONREAD:
        return sys_ioctl(fd, FIONREAD, arg);
    case SUNOS_FIONBIO:
        return sys_ioctl(fd, FIONBIO, arg);
    case SUNOS_FIOCLEX:
        return sys_fcntl(fd, F_SETFD, FD_CLOEXEC);
    case SUNOS_FIONCLEX:
        return sys_fcntl(fd, F_SETFD, 0);
    default:
        break;
    }
    if (SUNOS_IOC_GROUP(request) == 't' &&
        svr4_ttcompat(fd, key, (uint32_t)(uintptr_t)arg, &result)) {
        return (int)result;
    }
    return -ENOTTY;
}
