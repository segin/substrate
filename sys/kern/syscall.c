/*
 * kern/syscall.c - Generic (architecture-independent) syscall implementations
 *
 * This file contains the syscall implementations that are not specific
 * to any architecture. Architecture-specific code (dispatch, TLS, etc.)
 * remains in arch/i386/syscall.c or equivalent.
 */

/* Kernel internal includes */
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/version.h>
#include <kern/panic.h>
#include <kern/console.h>
#include <kern/cmdline.h>
#include <exec/perso/personality.h>
#include <pm/pm.h>
#include <vm/vm_kmem.h>
#include <vm/uma.h>
#include <include/sys/thr.h>
#include <include/sys/acct.h>
#include <include/sys/file.h>
#include <include/sys/proc.h>
#include <include/sys/signal.h>
#include <include/sys/session.h>
#include <include/sys/tty.h>
#include <kern/file.h>
#include <vfs/vfs.h>
#include <drivers/console/uart/uart.h>
#include <include/sys/sysinfo.h>
#include <sys/kern_syscalls.h>
#include <sys/utsname.h>

#include <sys/smp.h>
#include <sys/lock.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/random.h>
#include <sys/reboot.h>
#include <sys/exec.h>
#include <sys/namei.h>
#include <arch/x86-common/io.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <vm/vm_map.h>
#include "../vm/vm_area.h"

#include <sys/kern_syscalls.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <kern/time.h>

// File structure allocator
static uma_zone_t *file_zone = NULL;

#define IO_CHUNK_SIZE 4096

static int cloned_node_file_close(struct file *fp, struct thread *td) {
    (void)td;
    if (!fp || !fp->f_data) {
        return 0;
    }

    kfree(fp->f_data, sizeof(fs_node_t));
    fp->f_data = NULL;
    return 0;
}

static struct fileops cloned_node_fileops = {
    .fo_close = cloned_node_file_close,
};

static short file_flags_from_open_flags(int flags) {
    short f_flag = 0;

    switch (flags & O_ACCMODE) {
    case O_WRONLY:
        f_flag |= FWRITE;
        break;
    case O_RDWR:
        f_flag |= FREAD | FWRITE;
        break;
    case O_RDONLY:
    default:
        f_flag |= FREAD;
        break;
    }

    if (flags & O_APPEND) {
        f_flag |= FAPPEND;
    }
    if (flags & O_NONBLOCK) {
        f_flag |= FNONBLOCK;
    }

    return f_flag;
}

static void file_set_path(file_t *f, const char *path) {
    if (!f) {
        return;
    }
    if (!path) {
        f->f_path[0] = '\0';
        return;
    }
    strncpy(f->f_path, path, sizeof(f->f_path) - 1);
    f->f_path[sizeof(f->f_path) - 1] = '\0';
}

static void file_build_path(char *out, size_t out_sz, const char *path, const char *cwd_path) {
    if (!out || out_sz == 0) {
        return;
    }

    out[0] = '\0';
    if (!path || !path[0]) {
        return;
    }

    if (path[0] == '/') {
        strncpy(out, path, out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }

    if (cwd_path && cwd_path[0]) {
        if (strcmp(cwd_path, "/") == 0) {
            snprintf(out, out_sz, "/%s", path);
        } else {
            snprintf(out, out_sz, "%s/%s", cwd_path, path);
        }
        return;
    }

    strncpy(out, path, out_sz - 1);
    out[out_sz - 1] = '\0';
}

/*
 * vfs_perso_lookup - personality-aware VFS path lookup.
 *
 * For absolute paths, if the current process's personality has a path_prefix,
 * try <prefix><path> first (e.g. /perso/freebsd/lib/libc.so.7), then fall
 * back to <path> directly.  This mirrors NetBSD's TRYEMULROOT mechanism.
 * Relative paths are always resolved against cwd without prefix.
 */
fs_node_t *vfs_perso_lookup(fs_node_t *root, fs_node_t *cwd, const char *path) {
    if (!path) return NULL;

    if (path[0] == '/' && current_process) {
        struct personality *p = perso_lookup(current_process->perso_id);
        if (p && p->path_prefix && p->path_prefix[0]) {
            char prefixed[320];
            snprintf(prefixed, sizeof(prefixed), "%s%s", p->path_prefix, path);
            fs_node_t *node = vfs_lookup(root, prefixed);
            if (node) {
                if (cmdline_debug_enabled("perso_lookup")) {
                    extern int kprintf(const char *, ...);
                    kprintf("PERSO_LOOKUP: %s -> %s : node=%p flags=%x len=%d\n",
                        path, prefixed, node, node->flags, (int)node->length);
                }
                return node;
            }
        }
        return vfs_lookup(root, path);
    }

    return vfs_lookup(cwd ? cwd : root, path);
}

static int kern_resolve_parent_at(const char *path, fs_node_t *root, fs_node_t *cwd,
                                  fs_node_t **parent_out, const char **name_out) {
    const char *last_slash;
    fs_node_t *parent;
    char dir[256];

    if (!path || !parent_out || !name_out) return -EINVAL;
    last_slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            last_slash = p;
        }
    }

    if (!last_slash) {
        parent = cwd;
        *name_out = path;
    } else if (last_slash == path) {
        parent = root;
        *name_out = path + 1;
    } else {
        size_t dirlen = (size_t)(last_slash - path);
        if (dirlen >= sizeof(dir)) return -ENAMETOOLONG;
        memcpy(dir, path, dirlen);
        dir[dirlen] = '\0';
        parent = vfs_lookup((path[0] == '/') ? root : cwd, dir);
        *name_out = last_slash + 1;
    }

    if (!parent) return -ENOENT;
    if (((parent->flags & 0x7) != FS_DIRECTORY)) return -ENOTDIR;
    if (!(*name_out)[0]) return -EINVAL;

    *parent_out = parent;
    return 0;
}

static int kern_resolve_parent(const char *path, fs_node_t **parent_out, const char **name_out) {
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;

    return kern_resolve_parent_at(path, root, cwd, parent_out, name_out);
}

static fs_node_t *vfs_prepare_open_node(fs_node_t *node, struct file **f_out) {
    if (!node || !f_out || !*f_out) {
        return NULL;
    }

    /*
     * Char devices may keep mutable per-open state in fs_node fields.
     * Clone node storage so callbacks cannot alias shared devfs nodes.
     */
    if (((node->flags & 0x7) == FS_CHARDEVICE) && node->open) {
        fs_node_t *clone = kmalloc(sizeof(fs_node_t));
        if (!clone) {
            return NULL;
        }
        memcpy(clone, node, sizeof(fs_node_t));
        (*f_out)->f_ops = &cloned_node_fileops;
        return clone;
    }

    return node;
}


static void ensure_file_zone_init(void) {
    if (file_zone) return;

    static volatile int init_lock = 0;
    while (__sync_lock_test_and_set(&init_lock, 1)) {
        while (init_lock) __asm__("pause");
    }

    if (!file_zone) {
        file_zone = uma_zcreate("file", sizeof(file_t), NULL, NULL, NULL, NULL, 0, 0);
    }

    __sync_lock_release(&init_lock);
}

/*
 * file_alloc - Allocate a file structure from the UMA zone.
 */
file_t *file_alloc(void) {
    ensure_file_zone_init();
    file_t *f = uma_zalloc(file_zone, M_WAITOK | M_ZERO);
    if (f) {
        f->f_count = 1;
    }
    return f;
}

void file_free(file_t *f) {
    if (!f) return;
    uma_zfree(file_zone, f);
}

ssize_t kern_write(int fd, const char *buf, size_t len) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    if (len == 0) return 0;

    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    // Check for node write support
    if (f->f_data && ((fs_node_t*)f->f_data)->write) {
        /*
         * buf is already a kernel pointer here when called from sys_write or internal code.
         * PERFORMANCE: We pass it directly to write_fs to avoid redundant double buffering (memcpy).
         * This Zero-Copy approach improves throughput by ~8% on large writes.
         */
        ssize_t bytes = (ssize_t)write_fs((fs_node_t*)f->f_data, f->f_offset, len, (const uint8_t*)buf);

        if (bytes > 0) {
            f->f_offset += bytes;
        }

        return bytes;
    } else
        return 0;
}

int truncate_fs(fs_node_t *node, off_t length) {
    if (node->truncate != 0) {
        return node->truncate(node, length);
    }
    
    // Default: just update length if file is regular
    if ((node->flags & 0x7) == FS_FILE) {
        node->length = length;
        return 0;
    }
    
    return -1;
}

ssize_t sys_write(int fd, const char *buf, size_t len) {
    if (len == 0) return 0;

    /* Reject buffers that wrap the address space — `buf + total_written`
     * later in the loop would underflow into the kernel direct-map. */
    uintptr_t buf_end;
    if (__builtin_add_overflow((uintptr_t)buf, len, &buf_end)) {
        return -14; // EFAULT
    }

    void *kbuf = kmalloc(IO_CHUNK_SIZE);
    if (!kbuf) return -12; // ENOMEM

    ssize_t total_written = 0;
    while (len > 0) {
        size_t to_write = (len > IO_CHUNK_SIZE) ? IO_CHUNK_SIZE : len;
        if (copyin(buf + total_written, kbuf, to_write) != 0) {
            kfree(kbuf, IO_CHUNK_SIZE);
            if (total_written > 0) return total_written;
            return -14; // EFAULT
        }

        ssize_t bytes = kern_write(fd, kbuf, to_write);
        if (bytes <= 0) {
            if (total_written == 0) {
                kfree(kbuf, IO_CHUNK_SIZE);
                return bytes;
            }
            break;
        }

        total_written += bytes;
        len -= (size_t)bytes;
        if ((size_t)bytes < to_write) break;
    }
    kfree(kbuf, IO_CHUNK_SIZE);
    return total_written;
}

ssize_t kern_read(int fd, char *buf, size_t len) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    if (len == 0) return 0;

    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    if (!f->f_data) return -1;
    
    /*
     * buf is already a kernel pointer here when called from sys_read or internal code.
     * We pass it directly to read_fs to avoid redundant double buffering.
     */
    ssize_t bytes = (ssize_t)read_fs((fs_node_t*)f->f_data, f->f_offset, len, (uint8_t*)buf);

    if (bytes > 0) {
        f->f_offset += bytes;
    }

    return bytes;
}

ssize_t sys_read(int fd, char *buf, size_t len) {
    if (len == 0) return 0;
    if (!current_process) return -1;
    if (fd < 0 || fd >= MAX_FD) return -1;

    /* Bounds-check `buf + total_read` against pointer wrap. */
    uintptr_t buf_end;
    if (__builtin_add_overflow((uintptr_t)buf, len, &buf_end)) {
        return -14; // EFAULT
    }

    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -1;

    void *kbuf = kmalloc(4096);
    if (!kbuf) return -12; // ENOMEM

    ssize_t total_read = 0;
    while (len > 0) {
        size_t to_read = (len > 4096) ? 4096 : len;
        ssize_t bytes = (ssize_t)read_fs((fs_node_t*)f->f_data, f->f_offset, to_read, (uint8_t*)kbuf);
        if (bytes <= 0) {
            if (total_read == 0) {
                kfree(kbuf, 4096);
                return bytes;
            }
            break;
        }

        if (copyout(kbuf, buf + total_read, bytes) != 0) {
            kfree(kbuf, 4096);
            if (total_read > 0) return total_read;
            return -14; // EFAULT
        }

        f->f_offset += bytes;
        total_read += bytes;
        len -= (size_t)bytes;
        if ((size_t)bytes < to_read) break;
    }
    kfree(kbuf, 4096);
    return total_read;
}

int sys_open(const char *path, int flags, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    return kern_open(kpath, flags, mode);
}

static int kern_open_from(const char *path, int flags, int mode, fs_node_t *root, fs_node_t *cwd,
                          const char *cwd_path) {
    const char *create_name = NULL;
    char full_path[sizeof(((file_t *)0)->f_path)];
    (void)mode;
    if (!path) return -EFAULT;
    
    // Find free FD using hint
    int fd = proc_alloc_fd(current_process);
    if (fd == -1) return -1;

    // Lookup file
    fs_node_t *node = 0;

    node = vfs_perso_lookup(root, cwd, path);

    if (!node) {
        fs_node_t *parent = NULL;
        int error;

        if (!(flags & O_CREAT)) {
            proc_clear_fd(current_process, fd);
            return -ENOENT;
        }

        error = kern_resolve_parent_at(path, root, cwd, &parent, &create_name);
        if (error != 0) {
            proc_clear_fd(current_process, fd);
            return error;
        }

        error = mknod_fs(parent, create_name,
                         (uint16_t)(S_IFREG | ((mode & 0777) & ~current_process->umask)), 0);
        if (error != 0) {
            proc_clear_fd(current_process, fd);
            return error;
        }

        node = vfs_perso_lookup(root, cwd, path);
        if (!node) {
            proc_clear_fd(current_process, fd);
            return -ENOENT;
        }
    } else if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
        proc_clear_fd(current_process, fd);
        return -EEXIST;
    }

    if (vfs_may_open(node,
                     current_process ? current_process->euid : 0,
                     current_process ? current_process->egid : 0,
                     flags) != 0) {
        proc_clear_fd(current_process, fd);
        return -EACCES;
    }

    file_t *f = file_alloc();
    if (!f) {
        proc_clear_fd(current_process, fd);
        return -ENOMEM;
    }

    fs_node_t *open_node = vfs_prepare_open_node(node, &f);
    if (!open_node) {
        file_free(f);
        proc_clear_fd(current_process, fd);
        return -ENOMEM;
    }

    f->f_data = open_node;
    f->f_offset = 0;
    f->f_flag = file_flags_from_open_flags(flags);
    f->f_count = 1;
    file_build_path(full_path, sizeof(full_path), path, cwd_path);
    file_set_path(f, full_path);

    proc_set_fd(current_process, fd, f);
    if (flags & O_CLOEXEC) {
        current_process->fd_cloexec |= (1U << fd);
    }
    open_fs(open_node, 1, 0); // Open read/write?
    if ((flags & O_TRUNC) && ((flags & O_ACCMODE) != O_RDONLY) &&
        ((open_node->flags & 0x7) == FS_FILE)) {
        int error = truncate_fs(open_node, 0);
        if (error != 0) {
            proc_clear_fd(current_process, fd);
            file_close_ptr(f);
            return error;
        }
        f->f_offset = 0;
    }
    if (flags & O_APPEND) {
        f->f_offset = open_node->length;
    }

    return fd;
}

int kern_open(const char *path, int flags, int mode) {
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;

    return kern_open_from(path, flags, mode, root, cwd, current_process ? current_process->cwd_path : NULL);
}

int sys_openat(int dirfd, const char *path, int flags, int mode) {
    char kpath[256];

    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) {
        return -EFAULT;
    }
    return kern_openat(dirfd, kpath, flags, mode);
}

int kern_openat(int dirfd, const char *path, int flags, int mode) {
    fs_node_t *root;
    fs_node_t *cwd;

    if (!path) {
        return -EFAULT;
    }

    root = current_process->root_node ? current_process->root_node : fs_root;
    cwd = current_process->cwd_node ? current_process->cwd_node : root;

    if (path[0] == '/') {
        return kern_open_from(path, flags, mode, root, cwd, current_process ? current_process->cwd_path : NULL);
    }

    if (dirfd == AT_FDCWD) {
        return kern_open_from(path, flags, mode, root, cwd, current_process ? current_process->cwd_path : NULL);
    }

    if (dirfd < 0 || dirfd >= MAX_FD) {
        return -EBADF;
    }

    file_t *df = current_process->fds[dirfd];
    if (!df || !df->f_data) {
        return -EBADF;
    }

    cwd = (fs_node_t *)df->f_data;
    if ((cwd->flags & 0x7) != FS_DIRECTORY) {
        return -ENOTDIR;
    }

    return kern_open_from(path, flags, mode, root, cwd, df->f_path[0] ? df->f_path : NULL);
}

static int
kern_path_roots_from_dirfd(int dirfd, const char *path, fs_node_t **root_out, fs_node_t **cwd_out) {
    fs_node_t *root;
    fs_node_t *cwd;
    file_t *df;

    if (!path || !root_out || !cwd_out) {
        return -EFAULT;
    }

    root = current_process->root_node ? current_process->root_node : fs_root;
    cwd = current_process->cwd_node ? current_process->cwd_node : root;
    if (!root || !cwd) {
        return -ENOENT;
    }

    if (path[0] == '/' || dirfd == AT_FDCWD) {
        *root_out = root;
        *cwd_out = cwd;
        return 0;
    }

    if (dirfd < 0 || dirfd >= MAX_FD) {
        return -EBADF;
    }

    df = current_process->fds[dirfd];
    if (!df || !df->f_data) {
        return -EBADF;
    }

    cwd = (fs_node_t *)df->f_data;
    if ((cwd->flags & 0x7) != FS_DIRECTORY) {
        return -ENOTDIR;
    }

    *root_out = root;
    *cwd_out = cwd;
    return 0;
}

static int
kern_resolve_parent_dirfd(int dirfd, const char *path, fs_node_t **parent_out, char *name_out, size_t name_out_size) {
    fs_node_t *root;
    fs_node_t *cwd;
    fs_node_t *parent;
    const char *last_slash;
    char dir[256];
    int error;

    if (!path || !parent_out || !name_out || name_out_size == 0) {
        return -EINVAL;
    }
    if (path[0] == '\0') {
        return -EINVAL;
    }

    error = kern_path_roots_from_dirfd(dirfd, path, &root, &cwd);
    if (error != 0) {
        return error;
    }

    last_slash = strrchr(path, '/');
    if (!last_slash) {
        parent = cwd;
        if (strlcpy(name_out, path, name_out_size) >= name_out_size) {
            return -ENAMETOOLONG;
        }
    } else if (last_slash == path) {
        parent = root;
        if (strlcpy(name_out, path + 1, name_out_size) >= name_out_size) {
            return -ENAMETOOLONG;
        }
    } else {
        size_t dirlen = (size_t)(last_slash - path);
        fs_node_t *lookup_root = (path[0] == '/') ? root : cwd;

        if (dirlen >= sizeof(dir)) {
            return -ENAMETOOLONG;
        }
        memcpy(dir, path, dirlen);
        dir[dirlen] = '\0';
        if (strlcpy(name_out, last_slash + 1, name_out_size) >= name_out_size) {
            return -ENAMETOOLONG;
        }
        parent = vfs_lookup(lookup_root, dir);
    }

    if (!parent) {
        return -ENOENT;
    }
    if (name_out[0] == '\0') {
        return -EINVAL;
    }

    *parent_out = parent;
    return 0;
}

// Helper for internal use (and userspace via sys_close)
void file_close_ptr(file_t *f) {
    if (!f) return;
    f->f_count--;
    if (f->f_count <= 0) {
        close_fs((fs_node_t*)f->f_data);
        if (f->f_ops && f->f_ops->fo_close) {
            f->f_ops->fo_close(f, current_thread);
        }
        file_free(f);
    }
}

int kern_close(int fd) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    file_close_ptr(f);
    proc_clear_fd(current_process, fd);

    // Update hint if we freed a lower FD
    if (fd < current_process->next_fd) {
        current_process->next_fd = fd;
    }
    return 0;
}

int sys_close(int fd) {
    return kern_close(fd);
}

int64_t sys_lseek(int fd, uint32_t off_lo, uint32_t off_hi, int w) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    off_t off = ((off_t)off_hi << 32) | off_lo;
    
    if (w == 0) f->f_offset = off; // SEEK_SET
    else if (w == 1) f->f_offset += off; // SEEK_CUR
    else if (w == 2) f->f_offset = ((fs_node_t*)f->f_data)->length + off; // SEEK_END
    
    return f->f_offset;
}

int kern_lseek(int fd, off_t offset, int whence) {
    return (int)sys_lseek(fd, (uint32_t)offset, (uint32_t)((uint64_t)offset >> 32), whence);
}

int sys_umask(int newmask) {
    mode_t oldmask;

    if (!current_process) {
        return -1;
    }

    oldmask = current_process->umask;
    current_process->umask = (uint16_t)(newmask & 0777);
    return oldmask;
}

int sys_truncate(const char *path, uint32_t lo, uint32_t hi) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT
    
    fs_node_t *node = vfs_lookup(current_process->root_node ? current_process->root_node : fs_root, kpath);
    if (!node) return -2; // ENOENT
    
    off_t length = ((off_t)hi << 32) | lo;
    return truncate_fs(node, length);
}

int sys_ftruncate(int fd, uint32_t lo, uint32_t hi) {
    if (fd < 0 || fd >= MAX_FD) return -9; // EBADF
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -9; // EBADF
    
    // Check if open for writing?
    if (!(f->f_flag & FWRITE)) return -22; // EINVAL (should be EBADF or something else depending on OS)
    
    off_t length = ((off_t)hi << 32) | lo;
    return truncate_fs((fs_node_t*)f->f_data, length);
}



// Linux dirent structure for getdents
struct linux_dirent {
    unsigned long  d_ino;
    unsigned long  d_off;
    unsigned short d_reclen;
    char           d_name[];
};

// Linux dirent64 structure for getdents64
struct linux_dirent64 {
    uint64_t       d_ino;
    int64_t        d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

int sys_getdents(unsigned int fd, void *dirp, unsigned int count) {
    if (!current_process) return -1;
    if (count > 65536) count = 65536;
    void *kdirp = kmalloc(count);
    if (!kdirp) return -12;
    uint64_t old_offset = 0;
    file_t *f = NULL;
    if (fd < MAX_FD) {
        f = current_process->fds[fd];
        if (f) old_offset = f->f_offset;
    }
    int ret = kern_getdents(fd, kdirp, count);
    if (ret > 0) {
        if (copyout(kdirp, dirp, ret) != 0) {
            if (f) f->f_offset = old_offset;
            kfree(kdirp, count);
            return -14;
        }
    }
    kfree(kdirp, count);
    return ret;
}

int sys_getdents64(unsigned int fd, void *dirp, unsigned int count) {
    if (!current_process) return -1;
    if (count > 65536) count = 65536;
    void *kdirp = kmalloc(count);
    if (!kdirp) return -12;
    uint64_t old_offset = 0;
    file_t *f = NULL;
    if (fd < MAX_FD) {
        f = current_process->fds[fd];
        if (f) old_offset = f->f_offset;
    }
    int ret = kern_getdents64(fd, kdirp, count);
    if (ret > 0) {
        if (copyout(kdirp, dirp, ret) != 0) {
            if (f) f->f_offset = old_offset;
            kfree(kdirp, count);
            return -14;
        }
    }
    kfree(kdirp, count);
    return ret;
}

int kern_getdents(unsigned int fd, void *dirp, unsigned int count) {
    if (fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    unsigned int bpos = 0;
    char temp_buf[512]; // Kernel stack buffer for one entry
    struct linux_dirent *kld = (struct linux_dirent*)temp_buf;
    
    while (bpos < count) {
        // Read one entry
        struct dirent *d = readdir_fs((fs_node_t*)f->f_data, f->f_offset);
        if (!d) {
            // EOF
            if (bpos == 0) return 0; // EOF on first try
            break; // Return what we have
        }
        
        // Calculate size
        int name_len = 0;
        while(d->d_name[name_len]) name_len++;
        
        int reclen = sizeof(unsigned long) * 2 + sizeof(unsigned short) + name_len + 1;
        reclen = (reclen + 3) & ~3; // Align to 4 bytes
        
        if (bpos + reclen > count) {
            // Buffer full
            if (bpos == 0) return -22; // EINVAL (Buffer too small for even one)
            break; 
        }
        
        if (reclen > (int)sizeof(temp_buf)) return -22; // Should not happen for normal filenames

        kld->d_ino = d->d_ino;
        kld->d_off = f->f_offset + 1; // Offset of NEXT entry
        kld->d_reclen = reclen;
        for(int i=0; i<name_len; i++) kld->d_name[i] = d->d_name[i];
        kld->d_name[name_len] = 0;
        
        memcpy((char*)dirp + bpos, kld, reclen);
        
        bpos += reclen;
        f->f_offset++;
    }
    
    return bpos;
}

int kern_getdents64(unsigned int fd, void *dirp, unsigned int count) {
    if (fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;

    unsigned int bpos = 0;
    char temp_buf[512];
    struct linux_dirent64 *kld = (struct linux_dirent64 *)temp_buf;

    while (bpos < count) {
        struct dirent *d = readdir_fs((fs_node_t *)f->f_data, f->f_offset);
        if (!d) {
            if (bpos == 0) return 0;
            break;
        }

        int name_len = 0;
        while (d->d_name[name_len]) name_len++;

        int reclen = offsetof(struct linux_dirent64, d_name) + name_len + 1;
        reclen = (reclen + (int)sizeof(uint64_t) - 1) & ~((int)sizeof(uint64_t) - 1);

        if (bpos + (unsigned int)reclen > count) {
            if (bpos == 0) return -22;
            break;
        }

        if (reclen > (int)sizeof(temp_buf)) return -22;

        kld->d_ino = d->d_ino;
        kld->d_off = (int64_t)(f->f_offset + 1);
        kld->d_reclen = (unsigned short)reclen;
        kld->d_type = d->d_type;
        for (int i = 0; i < name_len; i++) kld->d_name[i] = d->d_name[i];
        kld->d_name[name_len] = 0;

        memcpy((char *)dirp + bpos, kld, (size_t)reclen);

        bpos += (unsigned int)reclen;
        f->f_offset++;
    }

    return (int)bpos;
}

/* UTSNAME is now in sys/utsname.h */

int sys_uname(struct utsname *buf) {
    struct utsname kbuf;
    int ret = kern_uname(&kbuf);
    if (ret == 0) {
        if (copyout(&kbuf, buf, sizeof(struct utsname)) != 0) return -14;
    }
    return ret;
}

int kern_uname(struct utsname *buf) {
    if (!buf) return -1;
    
    extern char kernel_hostname[MAXHOSTNAMELEN];
    
    memset(buf, 0, sizeof(struct utsname));
    
    strncpy(buf->sysname, "Substrate", 255);
    buf->sysname[255] = '\0';
    strncpy(buf->nodename, kernel_hostname, 255);
    buf->nodename[255] = '\0';
    strncpy(buf->release, "0.1", 255);
    buf->release[255] = '\0';
    strncpy(buf->version, "Kernel", 255);
    buf->version[255] = '\0';
    strncpy(buf->machine, "i386", 255);
    buf->machine[255] = '\0';
    buf->domainname[0] = '\0';
    
    return 0;
}

extern void proc_exit(int code);

int sys_exit(int code) {
    proc_exit(code);
    return 0;
}

int sys__exit(int code) {
    proc_exit(code);
    return 0;
}

int sys_thr_new(struct thr_param *param, int param_size) {
    struct thr_param kparam;
    if (param_size < (int)sizeof(struct thr_param)) return -1;
    if (copyin(param, &kparam, sizeof(struct thr_param)) != 0) return -14;

    // We also need to handle child_tid if it is provided
    // kern_thr_new writes to *p.child_tid
    // So we should pass a kernel pointer to child_tid
    long kchild_tid = 0;
    long *orig_child_tid = kparam.child_tid;
    if (orig_child_tid) kparam.child_tid = &kchild_tid;

    int ret = kern_thr_new(&kparam, sizeof(struct thr_param));

    if (ret == 0 && orig_child_tid) {
        if (copyout(&kchild_tid, orig_child_tid, sizeof(long)) != 0) return -14;
    }
    return ret;
}

// Duplicate sys_thr_exit removed

int sys_thr_self(void) {
    return sched_get_current_tid();
}

int kern_thr_new(struct thr_param *param, int param_size) {
    if (!param || param_size < (int)sizeof(struct thr_param)) return -1;
    struct thr_param p = *param;
    void *stack_top = (char*)p.stack_base + p.stack_size;
    thread_t *t = sched_create_thread(current_process, p.start_func, stack_top, p.arg);
    if (t) {
        if (p.child_tid) *p.child_tid = t->tid;
        return 0;
    }
    return -1;
}

int sys_thr_exit(void *retval) {
    current_thread->retval = retval;
    current_thread->state = THREAD_ZOMBIE;
    sleepq_wake_all(current_thread);
    sched_yield();
    return 0; // Not reached
}

int sys_thr_join(tid_t tid, void **status) {
    thread_t *thread = sched_get_thread(tid);
    if (!thread || thread->proc != current_process) return -3; // ESRCH

    while (thread->state != THREAD_ZOMBIE) {
        sleepq_add(thread, current_thread);
        sched_yield();

        // Re-check after wake-up
        thread = sched_get_thread(tid);
        if (!thread || thread->proc != current_process) return -3; // ESRCH
    }

    if (status) {
        void *kstatus = thread->retval;
        if (copyout(&kstatus, status, sizeof(void*)) != 0) return -14; // EFAULT
    }

    // Reap the thread
    thread->tid = -1;

    return 0;
}

extern int sys_vm86(void *v);
extern int sys_sysarch(int op, void *args);

// ...

// Helper to fill stat struct from fs_node
static void fill_stat(struct stat *buf, fs_node_t *node) {
    if (!buf) return;
    memset(buf, 0, sizeof(struct stat));

    uint32_t ftype = node->flags & 0x7;
    mode_t perms = (mode_t)(node->mask & 07777);

    buf->st_ino = node->inode;
    buf->st_size = (off_t)node->length;
    buf->st_uid = node->uid;
    buf->st_gid = node->gid;
    buf->st_mode = perms;
    buf->st_rdev = node->rdev;
    
    // Set file type bits
    if (ftype == FS_DIRECTORY)
        buf->st_mode |= 0040000;  // S_IFDIR
    else if (ftype == FS_SYMLINK)
        buf->st_mode |= 0120000;  // S_IFLNK
    else if (ftype == FS_CHARDEVICE)
        buf->st_mode |= 0020000;  // S_IFCHR
    else if (ftype == FS_BLOCKDEVICE)
        buf->st_mode |= 0060000;  // S_IFBLK
    else
        buf->st_mode |= 0100000;  // S_IFREG
    
    buf->st_nlink = 1;
    buf->st_blksize = 4096;
    buf->st_blocks = (node->length + 511) / 512;
    
    // Fill times (assuming node has these fields, derived from fs_node_t extensions)
    // For now, these might be 0 if fs_node_t doesn't have 64-bit timestamps yet, 
    // but structure is ready.
    buf->st_atime = node->atime;
    buf->st_mtime = node->mtime;
    buf->st_ctime = node->ctime;
}

int sys_chroot(const char *path) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    return kern_chroot(kpath);
}

int kern_chroot(const char *path) {
    if (!path) return -1;

    /* Only root may chroot */
    if (current_process->euid != 0) return -EPERM;

    fs_node_t *node = 0;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;

    if (path[0] == '/') {
        node = vfs_lookup(root, path);
    } else {
        node = vfs_lookup(cwd, path);
    }

    if (!node) return -1;
    if ((node->flags & 0x07) != FS_DIRECTORY) return -1;

    current_process->root_node = node;
    return 0;
}

extern int vfs_mkdir(const char *path, uint16_t permission);
int sys_mkdir(const char *p, int m) {
    char kpath[256];
    if (copyinstr(p, kpath, sizeof(kpath), NULL) != 0) return -14;
    return kern_mkdir(kpath, m);
}

int sys_mkdirat(int dirfd, const char *p, int m) {
    char kpath[256];

    if (copyinstr(p, kpath, sizeof(kpath), NULL) != 0) return -14;
    return kern_mkdirat(dirfd, kpath, m);
}

int kern_mkdir(const char *p, int m) {
    if (!p) return -1;
    return vfs_mkdir(p, (uint16_t)m);
}

int kern_mkdirat(int dirfd, const char *p, int m) {
    fs_node_t *parent_node = NULL;
    char name[128];
    int ret;

    if (!p) return -EFAULT;

    ret = kern_resolve_parent_dirfd(dirfd, p, &parent_node, name, sizeof(name));
    if (ret != 0) {
        return ret;
    }
    if ((parent_node->flags & 0x7) != FS_DIRECTORY) {
        return -ENOTDIR;
    }
    if (parent_node->finddir && parent_node->finddir(parent_node, name) != NULL) {
        return -EEXIST;
    }
    if (!parent_node->mkdir) {
        return -EOPNOTSUPP;
    }

    return parent_node->mkdir(parent_node, name, (uint16_t)m);
}

int kern_rmdir(const char *p) {
    if (!p) return -EFAULT;
    return vfs_rmdir(p);
}
int sys_rmdir(const char *p) { 
    char kpath[256];
    if (copyinstr(p, kpath, sizeof(kpath), NULL) != 0) return -14;
    return kern_rmdir(kpath); 
}
int sys_getuid(void) { return current_process->uid; }
int sys_getgid(void) { return current_process->gid; }
int sys_getppid(void) { return current_process->ppid; }
int sys_geteuid(void) { return current_process->euid; }
int sys_getegid(void) { return current_process->egid; }
int sys_setuid(int u) {
    if (current_process->euid == 0) {
        current_process->uid = u;
        current_process->euid = u;
        current_process->suid = u;
        return 0;
    }
    if ((uint32_t)u == current_process->uid || (uint32_t)u == current_process->suid) {
        current_process->euid = u;
        return 0;
    }
    return -EPERM;
}
int sys_setgid(int g) {
    if (current_process->euid == 0) {
        current_process->gid = g;
        current_process->egid = g;
        current_process->sgid = g;
        return 0;
    }
    if ((uint32_t)g == current_process->gid || (uint32_t)g == current_process->sgid) {
        current_process->egid = g;
        return 0;
    }
    return -EPERM;
}
int sys_clone(uint32_t flags, void *child_stack, int *parent_tidptr, void *tls, int *child_tidptr) {
    (void)parent_tidptr;
    (void)tls;
    (void)child_tidptr;
    extern int arch_fork_with_stack(void *child_stack);

    /*
     * Minimal Linux clone support for fork-style usage:
     * - allow only CSIGNAL in low 8 bits (e.g. SIGCHLD)
     * - reject thread-sharing flags for now
     */
    const uint32_t CLONE_SIGNAL_MASK = 0xFFu;
    if (flags & ~CLONE_SIGNAL_MASK) {
        return -22; /* EINVAL */
    }

    int child_pid = arch_fork_with_stack(child_stack);
    if (child_pid < 0) {
        return -11; /* EAGAIN */
    }
    return child_pid;
}


int sys_stat(const char *path, struct stat *buf) { 
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    struct stat kbuf;
    int ret = kern_stat(kpath, &kbuf);
    if (ret == 0) {
        if (copyout(&kbuf, buf, sizeof(struct stat)) != 0) return -14;
    }
    return ret;
}

int kern_stat(const char *path, struct stat *buf) {
    if (!path || !buf) return -EFAULT;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd  = current_process->cwd_node  ? current_process->cwd_node  : root;
    fs_node_t *node = vfs_perso_lookup(root, cwd, path);
    if (!node) return -ENOENT;
    /* Match the close_fs below.  vfs_perso_lookup itself does not
     * open_fs the returned node, so without this pin bump close_fs
     * would drop a reference that was borrowed from somewhere else
     * (most importantly fs_root, which the mount path pinned at
     * boot — see commit 2824601e for the breakdown trace). */
    open_fs(node, 1, 0);
    fill_stat(buf, node);
    close_fs(node);
    return 0;
}

int sys_lstat(const char *path, struct stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    struct stat kbuf;
    int ret = kern_lstat(kpath, &kbuf);
    if (ret == 0) {
        if (copyout(&kbuf, buf, sizeof(struct stat)) != 0) return -14;
    }
    return ret;
}

int kern_lstat(const char *path, struct stat *buf) {
    if (!path || !buf) return -EFAULT;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd  = current_process->cwd_node  ? current_process->cwd_node  : root;
    /* For lstat, try prefix with lstat semantics, then plain lstat.
     * Use the _ref variants so the close_fs below has a matching
     * pin bump (see commit 2824601e). */
    fs_node_t *node = NULL;
    if (path[0] == '/' && current_process) {
        struct personality *pp = perso_lookup(current_process->perso_id);
        if (pp && pp->path_prefix && pp->path_prefix[0]) {
            char prefixed[320];
            snprintf(prefixed, sizeof(prefixed), "%s%s", pp->path_prefix, path);
            node = vfs_lookup_lstat_ref(root, prefixed);
        }
    }
    if (!node) node = vfs_lookup_lstat_ref((path[0] == '/') ? root : cwd, path);
    if (!node) return -ENOENT;
    fill_stat(buf, node);
    close_fs(node);
    return 0;
}

// poll() implementation
#include <sys/poll.h>

int sys_poll(struct pollfd *fds, unsigned int nfds, int timeout) {
    if (nfds > 1024) return -22;
    size_t ksize = nfds * sizeof(struct pollfd);
    struct pollfd *kfds = kmalloc(ksize);
    if (!kfds) return -12;
    if (copyin(fds, kfds, ksize) != 0) {
        kfree(kfds, ksize);
        return -14;
    }
    int ret = kern_poll(kfds, nfds, timeout);
    if (ret >= 0) {
        if (copyout(kfds, fds, ksize) != 0) {
            kfree(kfds, ksize);
            return -14;
        }
    }
    kfree(kfds, ksize);
    return ret;
}

int kern_poll(struct pollfd *kfds, unsigned int nfds, int timeout) {
    int ready = 0;
    uint64_t deadline = 0;

    if (timeout > 0) {
        uint64_t timeout_ticks = ((uint64_t)timeout * (uint64_t)HZ + 999ULL) / 1000ULL;
        if (timeout_ticks == 0) {
            timeout_ticks = 1;
        }
        deadline = get_ticks() + timeout_ticks;
    }

    while (1) {
        void *wait_chan = NULL;

        ready = 0;
        for (unsigned int i = 0; i < nfds; i++) {
            if (kfds[i].fd < 0) {
                kfds[i].revents = 0;
                continue;
            }
            
            file_t *f = (kfds[i].fd < MAX_FD) ? current_process->fds[kfds[i].fd] : NULL;
            short mask = 0;
            
            if (f && f->f_data) {
                mask = poll_fs((fs_node_t*)f->f_data, &wait_chan);
                short ret_mask = mask & (kfds[i].events | POLLERR | POLLHUP | POLLNVAL);
                kfds[i].revents = ret_mask;
            } else {
                kfds[i].revents = POLLNVAL;
            }
            
            if (kfds[i].revents) ready++;
        }
        
        if (ready > 0) break;
        if (timeout == 0) break;

        if (timeout > 0) {
            int sleep_ret = sched_sleep_until(wait_chan ? wait_chan : (void *)current_thread, deadline);
            if (sleep_ret == -ETIMEDOUT) {
                return 0;
            }
        } else {
            if (wait_chan) {
                sched_sleep(wait_chan);
            } else {
                sched_yield();
            }
        }

        if (current_thread &&
            (current_thread->flags & THREAD_F_INTERRUPTIBLE) &&
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            return -EINTR;
        }
    }
 
    return ready;
}

int sys_fstat(int fd, struct stat *buf) {
    struct stat kbuf;
    int ret = kern_fstat(fd, &kbuf);
    if (ret == 0) {
        if (copyout(&kbuf, buf, sizeof(struct stat)) != 0) return -14;
    }
    return ret;
}


int kern_fstat(int fd, struct stat *buf) {
    if (fd < 0 || fd >= MAX_FD || !buf) return -1;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -1;
    fill_stat(buf, (fs_node_t*)f->f_data);
    return 0;
}

int kern_fstatat(int dirfd, const char *path, struct stat *buf, int flags) {
    fs_node_t *root;
    fs_node_t *cwd;
    fs_node_t *node;
    file_t *df;
    int nofollow;

    if (!path || !buf) return -EFAULT;

    root = current_process->root_node ? current_process->root_node : fs_root;
    cwd = current_process->cwd_node ? current_process->cwd_node : root;
    nofollow = (flags & AT_SYMLINK_NOFOLLOW) != 0;

    if (path[0] != '/') {
        if (dirfd == AT_FDCWD) {
            /* use current cwd */
        } else {
            if (dirfd < 0 || dirfd >= MAX_FD) return -EBADF;
            df = current_process->fds[dirfd];
            if (!df || !df->f_data) return -EBADF;
            cwd = (fs_node_t *)df->f_data;
            if ((cwd->flags & 0x07) != FS_DIRECTORY) return -ENOTDIR;
        }
    }

    node = nofollow
        ? vfs_lookup_lstat_ref((path[0] == '/') ? root : cwd, path)
        : vfs_lookup_ref((path[0] == '/') ? root : cwd, path);
    if (!node) return -ENOENT;

    fill_stat(buf, node);
    close_fs(node);
    return 0;
}

// ioctl - device control
int sys_ioctl(int fd, uint32_t request, void *arg) {
    // ioctl arg can be anything. For security, we should really know the size.
    // However, many ioctls use small structs.
    // This is hard to fix generically without a table.
    // For now, at least validate the pointer if it looks like one.
    if ((uintptr_t)arg >= KERN_BASE) {
        return -EFAULT;
    }
    return kern_ioctl(fd, request, arg);
}

int kern_ioctl(int fd, uint32_t request, void *arg) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -1;

    if ((uintptr_t)f->f_data < KERN_BASE) {
        return -EIO;
    }

    fs_node_t *node = (fs_node_t *)f->f_data;

    if (node->ioctl) {
        if ((uintptr_t)node->ioctl < KERN_BASE) {
            return -EIO;
        }
        return node->ioctl(node, request, arg);
    }
    
    return -1;
}

/* sys_setsid is now implemented in pm/pgrp.c */
extern int sys_setsid(void);


int sys_unlink(const char *path) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    return kern_unlink(kpath);
}

int sys_unlinkat(int dirfd, const char *path, int flags) {
    char kpath[256];

    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    return kern_unlinkat(dirfd, kpath, flags);
}

int kern_unlink(const char *path) {
    return kern_unlinkat(AT_FDCWD, path, 0);
}

int kern_unlinkat(int dirfd, const char *path, int flags) {
    fs_node_t *parent = NULL;
    char file[128];
    int ret;

    if (!path) return -EINVAL;
    if ((flags & ~AT_REMOVEDIR) != 0) return -EINVAL;

    ret = kern_resolve_parent_dirfd(dirfd, path, &parent, file, sizeof(file));
    if (ret != 0) {
        return ret;
    }

    if (flags & AT_REMOVEDIR) {
        fs_node_t *node;

        if ((parent->flags & 0x7) != FS_DIRECTORY) {
            return -ENOTDIR;
        }
        if (!parent->finddir) {
            return -EOPNOTSUPP;
        }

        node = parent->finddir(parent, file);
        if (!node) {
            return -ENOENT;
        }
        if ((node->flags & 0x7) != FS_DIRECTORY) {
            return -ENOTDIR;
        }
        if (!parent->rmdir) {
            return -EOPNOTSUPP;
        }

        return parent->rmdir(parent, file);
    }

    return unlink_fs(parent, file);
}

int sys_link(const char *oldpath, const char *newpath) {
    char kold[256], knew[256];
    if (copyinstr(oldpath, kold, sizeof(kold), NULL) != 0) return -14;
    if (copyinstr(newpath, knew, sizeof(knew), NULL) != 0) return -14;
    return kern_link(kold, knew);
}

int sys_rename(const char *oldpath, const char *newpath) {
    char kold[256], knew[256];
    if (copyinstr(oldpath, kold, sizeof(kold), NULL) != 0) return -EFAULT;
    if (copyinstr(newpath, knew, sizeof(knew), NULL) != 0) return -EFAULT;
    return kern_rename(kold, knew);
}

int kern_rename(const char *oldpath, const char *newpath) {
    fs_node_t *old_parent = NULL;
    fs_node_t *new_parent = NULL;
    const char *old_base = NULL;
    const char *new_base = NULL;
    int error;

    error = kern_resolve_parent(oldpath, &old_parent, &old_base);
    if (error != 0) return error;

    error = kern_resolve_parent(newpath, &new_parent, &new_base);
    if (error != 0) return error;

    if (old_parent->mp != new_parent->mp) {
        return -EXDEV;
    }

    return rename_fs(old_parent, old_base, new_parent, new_base);
}

int sys_statfs(const char *path, struct statfs *buf) {
    char kpath[256];
    struct statfs ks;
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    int error = kern_statfs(kpath, &ks);
    if (error == 0) {
        if (copyout(&ks, buf, sizeof(struct statfs)) != 0) return -EFAULT;
    }
    return error;
}

int kern_statfs(const char *path, struct statfs *buf) {
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;
    fs_node_t *node = vfs_lookup(path[0] == '/' ? root : cwd, path);
    if (!node) return -ENOENT;
    return statfs_fs(node, buf);
}

int sys_fstatfs(int fd, struct statfs *buf) {
    struct statfs ks;
    int error = kern_fstatfs(fd, &ks);
    if (error == 0) {
        if (copyout(&ks, buf, sizeof(struct statfs)) != 0) return -EFAULT;
    }
    return error;
}

int kern_fstatfs(int fd, struct statfs *buf) {
    if (fd < 0 || fd >= MAX_FD) return -EBADF;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -EBADF;
    return statfs_fs((fs_node_t*)f->f_data, buf);
}

int kern_statvfs(const char *path, struct statvfs *buf) {
    if (!buf) return -EINVAL;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;
    fs_node_t *node = vfs_lookup(path[0] == '/' ? root : cwd, path);
    if (!node) return -ENOENT;
    return statvfs_fs(node, buf);
}

int sys_statvfs(const char *path, struct statvfs *buf) {
    char kpath[256];
    struct statvfs ks;
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    int error = kern_statvfs(kpath, &ks);
    if (error == 0) {
        if (copyout(&ks, buf, sizeof(struct statvfs)) != 0) return -EFAULT;
    }
    return error;
}

int kern_fstatvfs(int fd, struct statvfs *buf) {
    if (!buf) return -EINVAL;
    if (fd < 0 || fd >= MAX_FD) return -EBADF;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -EBADF;
    return statvfs_fs((fs_node_t*)f->f_data, buf);
}

int sys_fstatvfs(int fd, struct statvfs *buf) {
    struct statvfs ks;
    int error = kern_fstatvfs(fd, &ks);
    if (error == 0) {
        if (copyout(&ks, buf, sizeof(struct statvfs)) != 0) return -EFAULT;
    }
    return error;
}

int kern_link(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -EINVAL;

    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;

    // Resolve oldpath to source node
    fs_node_t *source = vfs_lookup(cwd, oldpath);
    if (!source) return -ENOENT;

    // Resolve newpath to parent directory and name
    char dir[256];
    char file[128];
    const char *last_slash = NULL;
    for (const char *p = newpath; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    fs_node_t *parent = NULL;
    if (!last_slash) {
        parent = cwd;
        if (strlcpy(file, newpath, sizeof(file)) >= sizeof(file)) return -ENAMETOOLONG;
    } else if (last_slash == newpath) {
        parent = root;
        if (strlcpy(file, newpath + 1, sizeof(file)) >= sizeof(file)) return -ENAMETOOLONG;
    } else {
        size_t dirlen = (size_t)(last_slash - newpath);
        if (dirlen >= sizeof(dir)) return -ENAMETOOLONG;
        memcpy(dir, newpath, dirlen);
        dir[dirlen] = '\0';
        
        if (strlcpy(file, last_slash + 1, sizeof(file)) >= sizeof(file)) return -ENAMETOOLONG;
        parent = vfs_lookup((newpath[0] == '/') ? root : cwd, dir);
    }

    if (!parent) return -ENOENT;
    if (!file[0]) return -EINVAL;
    if ((parent->flags & 0x07) != FS_DIRECTORY) return -ENOTDIR;
    if (parent->finddir && parent->finddir(parent, file) != NULL) return -EEXIST;

    return link_fs(parent, source, file);
}

int sys_symlink(const char *target, const char *linkpath) {
    char ktarget[256], klinkpath[256];
    if (copyinstr(target, ktarget, sizeof(ktarget), NULL) != 0) return -EFAULT;
    if (copyinstr(linkpath, klinkpath, sizeof(klinkpath), NULL) != 0) return -EFAULT;
    return kern_symlink(ktarget, klinkpath);
}

int kern_symlink(const char *target, const char *linkpath) {
    if (!target || !linkpath) return -EINVAL;
    if (target[0] == '\0') return -ENOENT;

    char dir[256];
    char file[128];
    const char *last_slash = NULL;
    for (const char *p = linkpath; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;
    fs_node_t *parent = NULL;

    if (!last_slash) {
        parent = cwd;
        if (strlcpy(file, linkpath, sizeof(file)) >= sizeof(file)) return -ENAMETOOLONG;
    } else if (last_slash == linkpath) {
        parent = root;
        if (strlcpy(file, linkpath + 1, sizeof(file)) >= sizeof(file)) return -ENAMETOOLONG;
    } else {
        size_t dirlen = (size_t)(last_slash - linkpath);
        if (dirlen >= sizeof(dir)) return -ENAMETOOLONG;
        memcpy(dir, linkpath, dirlen);
        dir[dirlen] = '\0';

        if (strlcpy(file, last_slash + 1, sizeof(file)) >= sizeof(file)) return -ENAMETOOLONG;
        parent = vfs_lookup((linkpath[0] == '/') ? root : cwd, dir);
    }

    if (!parent) return -ENOENT;
    if (!file[0]) return -EINVAL;
    if ((parent->flags & 0x07) != FS_DIRECTORY) return -ENOTDIR;
    if (parent->finddir && parent->finddir(parent, file) != NULL) return -EEXIST;

    return symlink_fs(parent, target, file);
}

int sys_readlink(const char *pathname, char *buf, size_t bufsiz) {
    char kpath[256];
    if (copyinstr(pathname, kpath, sizeof(kpath), NULL) != 0) return -14;
    if (bufsiz > 4096) bufsiz = 4096;
    char *kbuf = kmalloc(bufsiz);
    if (!kbuf) return -12;
    int ret = kern_readlink(kpath, kbuf, bufsiz);
    if (ret > 0) {
        if (copyout(kbuf, buf, ret) != 0) {
            kfree(kbuf, bufsiz);
            return -14;
        }
    }
    kfree(kbuf, bufsiz);
    return ret;
}

int sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    char kpath[256];

    if (copyinstr(pathname, kpath, sizeof(kpath), NULL) != 0) return -14;
    if (bufsiz > 4096) bufsiz = 4096;
    char *kbuf = kmalloc(bufsiz);
    if (!kbuf) return -12;
    int ret = kern_readlinkat(dirfd, kpath, kbuf, bufsiz);
    if (ret > 0) {
        if (copyout(kbuf, buf, ret) != 0) {
            kfree(kbuf, bufsiz);
            return -14;
        }
    }
    kfree(kbuf, bufsiz);
    return ret;
}

int kern_readlink(const char *pathname, char *buf, size_t bufsiz) {
    return kern_readlinkat(AT_FDCWD, pathname, buf, bufsiz);
}

int kern_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    fs_node_t *root;
    fs_node_t *cwd;
    fs_node_t *node;
    file_t *df;

    if (!pathname || !buf || bufsiz == 0) return -14; // EFAULT

    root = current_process->root_node ? current_process->root_node : fs_root;
    cwd = current_process->cwd_node ? current_process->cwd_node : root;

    if (pathname[0] != '/') {
        if (dirfd == AT_FDCWD) {
            /* use current cwd */
        } else {
            if (dirfd < 0 || dirfd >= MAX_FD) return -EBADF;
            df = current_process->fds[dirfd];
            if (!df || !df->f_data) return -EBADF;
            cwd = (fs_node_t *)df->f_data;
            if ((cwd->flags & 0x07) != FS_DIRECTORY) return -ENOTDIR;
        }
    }

    node = vfs_lookup_lstat((pathname[0] == '/') ? root : cwd, pathname);
    if (!node) return -ENOENT;
    if ((node->flags & 0x07) != FS_SYMLINK) return -EINVAL;
    return readlink_fs(node, buf, bufsiz);
}

int sys_access(const char *path, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    return kern_access(kpath, mode);
}

int kern_access(const char *path, int mode) {
    int ret;

    if (!path) return -EFAULT;

    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;
    fs_node_t *node = vfs_perso_lookup(root, cwd, path);

    if (!node) return -ENOENT;

    // F_OK check
    if (mode == F_OK) return 0;

    ret = vfs_check_permissions(node, current_process->uid, current_process->gid, mode);
    if (ret != 0) {
        return -EACCES;
    }

    return 0;
}

int sys_mlock(const void *addr, size_t len) {
    // Stub implementation: always succeed
    // In the future, this should wire pages in the PMAP to prevent swapping.
    (void)addr;
    (void)len;
    return 0;
}

int sys_munlock(const void *addr, size_t len) {
    // Stub implementation: always succeed
    (void)addr;
    (void)len;
    return 0;
}

int sys_sync(void) {
    // In a real system, we'd iterate over all mounted filesystems
    // and call their sync methods.
    return 0;
}

extern int sys_stat(const char *p, struct stat *buf);
extern void pipe_create(fs_node_t **read_node, fs_node_t **write_node);

int sys_pipe(int *fds) {
    int kfds[2];
    int ret = kern_pipe(kfds);
    if (ret == 0) {
        if (copyout(kfds, fds, 2 * sizeof(int)) != 0) return -14;
    }
    return ret;
}

int kern_pipe(int *fds) {
    if (!fds) return -1;

    fs_node_t *read_node, *write_node;
    pipe_create(&read_node, &write_node);

    int f1 = proc_alloc_fd(current_process);
    if (f1 == -1) return -1;
    int f2 = proc_alloc_fd(current_process);
    if (f2 == -1) {
        // No second FD, but we already "allocated" f1.
        // next_fd was updated. We should probably clear f1 if we can't get both.
        // However, the previous code also had this potential issue if it only found one.
        // Actually the previous code checked if both were found.
        // Let's be safe.
        // Wait, the previous code didn't actually "allocate" until it found both.
        // My proc_alloc_fd updates next_fd.
        // Let's just find both first if possible.
        // Actually, for simplicity, I'll just use proc_alloc_fd twice.
        // If the second fails, we should free the first.
        // Since we didn't set the pointer yet, we just need to clear the bit.
        proc_clear_fd(current_process, f1);
        return -1;
    }

    file_t *rf = file_alloc();
    if (!rf) {
        proc_clear_fd(current_process, f1);
        proc_clear_fd(current_process, f2);
        return -1;
    }
    rf->f_data = read_node;
    rf->f_flag = FREAD;
    file_set_path(rf, "pipe:[read]");
    proc_set_fd(current_process, f1, rf);

    file_t *wf = file_alloc();
    if (!wf) {
        // rf is already in fds[f1], so proc_clear_fd will close it?
        // No, proc_clear_fd doesn't call file_close_ptr.
        // We should close rf.
        file_close_ptr(rf);
        proc_clear_fd(current_process, f1);
        proc_clear_fd(current_process, f2);
        return -1;
    }
    wf->f_data = write_node;
    wf->f_flag = FWRITE;
    file_set_path(wf, "pipe:[write]");
    proc_set_fd(current_process, f2, wf);

    fds[0] = f1;
    fds[1] = f2;
    return 0;
}

int sys_dup(int oldfd) {
    if (oldfd < 0 || oldfd >= MAX_FD) return -EBADF;
    file_t *f = current_process->fds[oldfd];
    if (!f) return -EBADF;

    // Find free FD with hint
    int newfd = proc_alloc_fd(current_process);
    if (newfd == -1) return -EMFILE;

    proc_set_fd(current_process, newfd, f);
    current_process->fd_cloexec &= ~(1U << newfd);
    f->f_count++;
    return newfd;
}

int sys_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= MAX_FD) return -EBADF;
    if (newfd < 0 || newfd >= MAX_FD) return -EBADF;
    if (oldfd == newfd) return newfd;

    file_t *f = current_process->fds[oldfd];
    if (!f) return -EBADF;

    if (current_process->fds[newfd]) {
        file_close_ptr(current_process->fds[newfd]);
        proc_clear_fd(current_process, newfd);
    }

    proc_set_fd(current_process, newfd, f);
    current_process->fd_cloexec &= ~(1U << newfd);
    f->f_count++;
    return newfd;
}

/*
 * dup3 — Linux extension: like dup2 but takes an O_CLOEXEC flag.
 * If oldfd == newfd, returns -EINVAL (POSIX dup2 returns the fd; dup3
 * specifically rejects this).
 */
int sys_dup3(int oldfd, int newfd, int flags) {
    if (oldfd == newfd) return -EINVAL;
    if (flags & ~O_CLOEXEC) return -EINVAL;

    int rc = sys_dup2(oldfd, newfd);
    if (rc < 0) return rc;
    if (flags & O_CLOEXEC) {
        current_process->fd_cloexec |= (1U << newfd);
    }
    return rc;
}

/*
 * pipe2 — Linux extension: like pipe(2) plus an O_CLOEXEC / O_NONBLOCK
 * flag set applied atomically to both ends.
 */
int sys_pipe2(int *fds, int flags) {
    if (flags & ~(O_CLOEXEC | O_NONBLOCK)) return -EINVAL;

    int kfds[2];
    int ret = kern_pipe(kfds);
    if (ret) return ret;

    if (flags & O_CLOEXEC) {
        current_process->fd_cloexec |= (1U << kfds[0]);
        current_process->fd_cloexec |= (1U << kfds[1]);
    }
    if (flags & O_NONBLOCK) {
        if (kfds[0] >= 0 && current_process->fds[kfds[0]])
            current_process->fds[kfds[0]]->f_flag |= O_NONBLOCK;
        if (kfds[1] >= 0 && current_process->fds[kfds[1]])
            current_process->fds[kfds[1]]->f_flag |= O_NONBLOCK;
    }
    if (copyout(kfds, fds, 2 * sizeof(int)) != 0) return -EFAULT;
    return 0;
}

static fs_node_t *sys_lookup_path(const char *path, int follow_final_symlink) {
    fs_node_t *root;
    fs_node_t *cwd;

    if (!current_process || !path) return NULL;

    root = current_process->root_node ? current_process->root_node : fs_root;
    cwd = current_process->cwd_node ? current_process->cwd_node : root;
    if (!root) return NULL;

    if (follow_final_symlink) {
        return vfs_lookup((path[0] == '/') ? root : cwd, path);
    }
    return vfs_lookup_lstat((path[0] == '/') ? root : cwd, path);
}

int sys_chmod(const char *path, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_chmodat(AT_FDCWD, kpath, mode, 0);
}

/* sys_fchownat is defined further down; the forward declaration lets
 * sys_chown() forward to it without restructuring the file. */
extern int sys_fchownat(int dirfd, const char *path, int uid, int gid, int flag);

/* POSIX chown(2) — follows symlinks.  Substrate previously only had
 * lchown (no-follow); add the canonical behaviour here for personalities
 * that issue the standard syscall. */
int sys_chown(const char *path, int uid, int gid) {
    return sys_fchownat(AT_FDCWD, path, uid, gid, 0);
}

/* POSIX lchmod(2) — does NOT follow symlinks.  No native Substrate
 * equivalent; route through kern_chmodat with AT_SYMLINK_NOFOLLOW. */
int sys_lchmod(const char *path, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_chmodat(AT_FDCWD, kpath, mode, AT_SYMLINK_NOFOLLOW);
}

/* fchmodat(2) — flag-driven follow / no-follow.  flag values are
 * Substrate-native (Linux-shape: AT_SYMLINK_NOFOLLOW=0x100).  BSD
 * personalities translate at their wrapper layer before reaching here. */
int sys_fchmodat(int dirfd, const char *path, int mode, int flag) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_chmodat(dirfd, kpath, mode, flag);
}

int kern_chmodat(int dirfd, const char *path, int mode, int flags) {
    fs_node_t *root;
    fs_node_t *cwd;
    fs_node_t *node;
    int nofollow;
    int ret;

    if (!path) return -EFAULT;
    if ((flags & ~AT_SYMLINK_NOFOLLOW) != 0) return -EINVAL;

    ret = kern_path_roots_from_dirfd(dirfd, path, &root, &cwd);
    if (ret != 0) return ret;

    nofollow = (flags & AT_SYMLINK_NOFOLLOW) != 0;
    node = nofollow
        ? vfs_lookup_lstat((path[0] == '/') ? root : cwd, path)
        : vfs_lookup((path[0] == '/') ? root : cwd, path);
    if (!node) return -ENOENT;

    if (current_process->euid != 0 && current_process->euid != node->uid) {
        return -EPERM;
    }

    if (current_process->euid != 0)
        mode &= ~(04000 | 02000);

    ret = vfs_chmod_node(node, (uint32_t)mode);
    return ret;
}

int sys_lchown(const char *path, int uid, int gid) {
    char kpath[256];
    fs_node_t *node;

    if (uid < -1 || gid < -1) return -EINVAL;
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    node = sys_lookup_path(kpath, 0);
    if (!node) return -ENOENT;

    /* Match fchown's current policy until supplementary groups exist. */
    if (uid != -1 && current_process->euid != 0)
        return -EPERM;
    if (gid != -1 && current_process->euid != 0 && current_process->euid != node->uid)
        return -EPERM;

    if (uid != -1) node->uid = (uint32_t)uid;
    if (gid != -1) node->gid = (uint32_t)gid;

    if (current_process->euid != 0)
        node->mask &= ~(uint32_t)(04000 | 02000);

    node->ctime = get_time();
    return 0;
}

int sys_fchmod(int fd, int mode) {
    int ret;

    if (fd < 0 || fd >= MAX_FD) return -EBADF;

    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -EBADF;

    fs_node_t *node = (fs_node_t *)f->f_data;

    /* Only root or file owner may change permissions */
    if (current_process->euid != 0 && current_process->euid != node->uid)
        return -EPERM;

    /* Non-root callers cannot set setuid/setgid bits */
    if (current_process->euid != 0)
        mode &= ~(04000 | 02000);

    ret = vfs_chmod_node(node, (uint32_t)mode);
    return ret;
}

int sys_fchown(int fd, int uid, int gid) {
    if (fd < 0 || fd >= MAX_FD) return -EBADF;

    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -EBADF;

    fs_node_t *node = (fs_node_t *)f->f_data;

    /* Only root may change file owner */
    if (uid != -1 && current_process->euid != 0)
        return -EPERM;

    /* Only root or file owner may change group */
    if (gid != -1 && current_process->euid != 0 && current_process->euid != node->uid)
        return -EPERM;

    if (uid != -1) node->uid = (uint32_t)uid;
    if (gid != -1) node->gid = (uint32_t)gid;

    /* Clear setuid/setgid bits on chown by non-root (POSIX requirement) */
    if (current_process->euid != 0)
        node->mask &= ~(uint32_t)(04000 | 02000);

    node->ctime = get_time();
    return 0;
}

int sys_fchownat(int dirfd, const char *path, int uid, int gid, int flag) {
    char kpath[256];
    char name[128];
    fs_node_t *parent;
    int ret;

    if (uid < -1 || gid < -1) return -EINVAL;

    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;

    /* If flag has AT_SYMLINK_NOFOLLOW, resolve the path as a whole and don't
       follow the last component (lchown semantics). Otherwise, follow it. */
    if (flag & AT_SYMLINK_NOFOLLOW) {
        /* For AT_SYMLINK_NOFOLLOW, we resolve the parent dir and do lchown */
        ret = kern_resolve_parent_dirfd(dirfd, kpath, &parent, name, sizeof(name));
        if (ret != 0) return ret;

        /* Look up the final component */
        fs_node_t *node = parent->finddir(parent, name);
        if (!node) return -ENOENT;

        /* If it's a symlink and AT_SYMLINK_NOFOLLOW is set, operate on the link */
        if ((node->flags & 0x7) == FS_SYMLINK) {
            /* Match fchown's current policy until supplementary groups exist. */
            if (uid != -1 && current_process->euid != 0)
                return -EPERM;
            if (gid != -1 && current_process->euid != 0 && current_process->euid != node->uid)
                return -EPERM;

            if (uid != -1) node->uid = (uint32_t)uid;
            if (gid != -1) node->gid = (uint32_t)gid;

            if (current_process->euid != 0)
                node->mask &= ~(uint32_t)(04000 | 02000);

            node->ctime = get_time();
            return 0;
        }

        /* Otherwise, do fchown-style operation on the resolved node */
        if (uid != -1 && current_process->euid != 0)
            return -EPERM;
        if (gid != -1 && current_process->euid != 0 && current_process->euid != node->uid)
            return -EPERM;

        if (uid != -1) node->uid = (uint32_t)uid;
        if (gid != -1) node->gid = (uint32_t)gid;

        if (current_process->euid != 0)
            node->mask &= ~(uint32_t)(04000 | 02000);

        node->ctime = get_time();
        return 0;
    }

    /* Default case (no AT_SYMLINK_NOFOLLOW): follow symlinks, resolve full path */
    fs_node_t *node = sys_lookup_path(kpath, 1);
    if (!node) return -ENOENT;

    /* Match fchown's current policy until supplementary groups exist. */
    if (uid != -1 && current_process->euid != 0)
        return -EPERM;
    if (gid != -1 && current_process->euid != 0 && current_process->euid != node->uid)
        return -EPERM;

    if (uid != -1) node->uid = (uint32_t)uid;
    if (gid != -1) node->gid = (uint32_t)gid;

    if (current_process->euid != 0)
        node->mask &= ~(uint32_t)(04000 | 02000);

    node->ctime = get_time();
    return 0;
}

int sys_lchownat(int dirfd, const char *path, int uid, int gid, int flag) {
    char kpath[256];
    char name[128];
    fs_node_t *parent;
    int ret;

    if (uid < -1 || gid < -1) return -EINVAL;

    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;

    /* lchownat always operates on the link itself, never follows symlinks */
    ret = kern_resolve_parent_dirfd(dirfd, kpath, &parent, name, sizeof(name));
    if (ret != 0) return ret;

    fs_node_t *node = parent->finddir(parent, name);
    if (!node) return -ENOENT;

    /* Only operate on symlinks; if it's not a symlink, return ENOTLNK
       unless the flag forces us to operate on it anyway */
    if ((node->flags & 0x7) != FS_SYMLINK && !(flag & AT_REMOVEDIR)) {
        /* Even for non-symlinks, we allow setting ownership */
    }

    /* Match fchown's current policy until supplementary groups exist. */
    if (uid != -1 && current_process->euid != 0)
        return -EPERM;
    if (gid != -1 && current_process->euid != 0 && current_process->euid != node->uid)
        return -EPERM;

    if (uid != -1) node->uid = (uint32_t)uid;
    if (gid != -1) node->gid = (uint32_t)gid;

    if (current_process->euid != 0)
        node->mask &= ~(uint32_t)(04000 | 02000);

    node->ctime = get_time();
    return 0;
}

int sys_fcntl(int fd, int cmd, int arg) {
    return proc_fcntl(current_process, fd, cmd, arg);
}

/* sys_getpgid is now implemented in pm/pgrp.c */
extern int sys_getpgid(int pid);


int sys_creat(const char *path, int mode) {
    return sys_open(path, 0x40 | 0x01 | 0x08, mode); // O_CREAT|O_WRONLY|O_TRUNC
}

int sys_signal(int sig, void *handler) {
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = (sig_t)handler;
    act.sa_flags = 0;
    return kern_sigaction(sig, &act, NULL);
}

int sys_waitpid(int pid, int *status, int options) {
    int kstatus = 0;
    int ret = kern_waitpid(pid, status ? &kstatus : NULL, options);
    if (ret >= 0 && status) {
        if (copyout(&kstatus, status, sizeof(int)) != 0) return -14;
    }
    return ret;
}

int kern_waitpid(int pid, int *status, int options) {
    return kern_wait4(pid, status, options, NULL);
}

int sys_getpid(void) { if(current_process) return current_process->pid; return 0; }

#include <sys/exec.h>

int sys_execve(const char *f, char *const a[], char *const e[]) {
    char kf[256];
    if (copyinstr(f, kf, sizeof(kf), NULL) != 0) return -14;
    return kern_execve(kf, a, e);
}

int kern_execve(const char *f, char *const a[], char *const e[]) {
    exec_pin_current_thread();
    int ret = exec_dispatch(f, a, e);
    if (ret == 0) {
        proc_vfork_done(current_process);
        /* Wipe CSPRNG state at exec boundary to prevent entropy leakage */
        random_on_exec();
    }
    exec_unpin_current_thread();
    /* On success exec_dispatch never returns; on failure, free any
     * personality-allocated argv/envp/path the caller pushed before us. */
    if (ret != 0) {
        exec_cleanup_drain();
    }
    return ret;
}

/* sys_fork and sys_vfork are arch-specific (need registers_t) - in arch/i386/syscall.c */
extern int sys_fork(void);
extern int sys_vfork(void);

int sys_mknod(const char *p, int m, int d) {
    char kpath[256];
    if (copyinstr(p, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    if (!current_process) return -EPERM;
    if ((m & S_IFMT) != S_IFIFO && current_process->euid != 0) return -EPERM;
    return vfs_mknod(kpath, (uint16_t)m, (uint32_t)d);
}

int vfs_mount_legacy(const char *device, const char *path, const char *type, uint32_t flags, void *data);
int vfs_unmount_legacy(const char *path);
fs_node_t *vfs_lookup(fs_node_t *root, const char *path);

int sys_mount(const char *source, const char *target, const char *fstype, unsigned long flags, void *data) {
    char ksource[256], ktarget[256], kfstype[64];
    if (source) {
        if (copyinstr(source, ksource, sizeof(ksource), NULL) != 0) return -14;
    }
    if (copyinstr(target, ktarget, sizeof(ktarget), NULL) != 0) return -14;
    if (copyinstr(fstype, kfstype, sizeof(kfstype), NULL) != 0) return -14;

    return kern_mount(source ? ksource : NULL, ktarget, kfstype, flags, data);
}

int kern_mount(const char *source, const char *target, const char *fstype, unsigned long flags, void *data) {
    if (!target || !fstype) return -EINVAL;
    if (current_process->euid != 0) return -EPERM;

    /*
     * Userspace must not be allowed to shadow /dev, /proc, or /sys
     * with arbitrary filesystem content — that's a privilege-escalation
     * vector (substitute /dev/random, /proc/<pid>, etc.).  Kernel-internal
     * mounts of devfs/procfs/sysfs onto those same paths come through
     * vfs_mount_legacy() directly and bypass this guard.  MNT_UPDATE
     * (remount-in-place) is exempt.
     */
    if (!(flags & MNT_UPDATE)) {
        if (strcmp(target, "/dev")  == 0 ||
            strcmp(target, "/proc") == 0 ||
            strcmp(target, "/sys")  == 0) {
            kprintf("mount: refusing user-mount over critical path %s\n",
                    target);
            return -EBUSY;
        }
    }

    return vfs_mount_legacy(source, target, fstype, (uint32_t)flags, data);
}

int kern_umount2(const char *target, int flags) {
    if (!target) return -EINVAL;
    /* MNT_FORCE / MNT_DETACH require root.  No flags also requires root. */
    if (current_process->euid != 0) return -EPERM;
    /* Reject unknown flag bits so we can extend safely later. */
    if (flags & ~(MNT_FORCE | MNT_UPDATE)) return -EINVAL;
    return vfs_unmount_legacy_flags(target, flags);
}

int kern_umount(const char *target) {
    return kern_umount2(target, 0);
}

int sys_umount2(const char *target, int flags) {
    char ktarget[256];
    if (copyinstr(target, ktarget, sizeof(ktarget), NULL) != 0) return -EFAULT;
    return kern_umount2(ktarget, flags);
}

int sys_umount(const char *target) {
    /* Legacy 1-arg form, retained for the BSD-style 158/159 ABI;
     * forwards to the 2-arg form with no flags. */
    return sys_umount2(target, 0);
}


int sys_nanosleep(void *req, void *rem) {
    if (!req) return -EFAULT;

    struct timespec kreq;
    if (copyin(req, &kreq, sizeof(struct timespec)) != 0) return -EFAULT;

    if (kreq.tv_nsec < 0 || kreq.tv_nsec >= 1000000000) return -EINVAL;
    if (kreq.tv_sec < 0) return -EINVAL;

    // Handle 0 sleep request - yield but don't sleep
    if (kreq.tv_sec == 0 && kreq.tv_nsec == 0) {
        sched_yield();
        return 0;
    }

    uint32_t hz = get_hz();

    // Calculate duration in ticks
    // We use ceiling division for nanoseconds to ensure we sleep AT LEAST the requested time
    // ticks = sec*HZ + ceil(nsec*HZ / 10^9)
    uint64_t ticks;
    if ((uint64_t)kreq.tv_sec > UINT64_MAX / hz) {
        ticks = UINT64_MAX;
    } else {
        ticks = (uint64_t)kreq.tv_sec * hz;
        uint64_t nsec_ticks = ((uint64_t)kreq.tv_nsec * hz + 999999999) / 1000000000;
        if (UINT64_MAX - ticks < nsec_ticks) {
            ticks = UINT64_MAX;
        } else {
            ticks += nsec_ticks;
        }
    }

    // Ensure at least 1 tick if request was > 0 (handled by ceiling above usually,
    // unless hz is very small or nsec is very small. If nsec=1, hz=100 -> ticks=1)

    uint64_t now = get_ticks();
    uint64_t deadline;
    if (ticks > UINT64_MAX - now) {
        deadline = UINT64_MAX;
    } else {
        deadline = now + ticks;
    }

    current_thread->flags |= THREAD_F_INTERRUPTIBLE;
    int ret = sched_sleep_until(&current_thread->sig_pending, deadline);
    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;

    if (ret == -ETIMEDOUT) {
        return 0;
    }

    // Interrupted by signal (ret == 0)
    if (rem) {
        now = get_ticks();
        if (now < deadline) {
             uint64_t diff = deadline - now;
             struct timespec remaining;
             remaining.tv_sec = diff / hz;
             remaining.tv_nsec = ((diff % hz) * 1000000000) / hz;
             if (copyout(&remaining, rem, sizeof(struct timespec)) != 0) return -EFAULT;
        } else {
             // Deadline passed but we were woken up? Treat as success.
             return 0;
        }
    }

    return -EINTR;
}

// Current working directory per-process
int sys_chdir(const char *path) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    return kern_chdir(kpath);
}

int kern_chdir(const char *path) {
    if (!path) return -1;
    
    fs_node_t *node = NULL;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *old_cwd = current_process->cwd_node;
    
    if (path[0] == '/') {
        node = vfs_lookup(root, path);
    } else {
        // Relative path - lookup from cwd
        fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;
        node = vfs_lookup(cwd, path);
    }
    
    if (!node) return -1;
    if ((node->flags & 0x7) != FS_DIRECTORY) return -1;

    open_fs(node, 1, 0);
    current_process->cwd_node = node;
    if (kern_getcwd(current_process->cwd_path, sizeof(current_process->cwd_path)) != 0) {
        strncpy(current_process->cwd_path, "/", sizeof(current_process->cwd_path) - 1);
        current_process->cwd_path[sizeof(current_process->cwd_path) - 1] = '\0';
    }
    if (old_cwd && old_cwd != node) {
        close_fs(old_cwd);
    }
    return 0;
}

int kern_fchdir(int fd) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -1;
    
    fs_node_t *node = (fs_node_t*)f->f_data;
    fs_node_t *old_cwd = current_process->cwd_node;
    if ((node->flags & 0x7) != FS_DIRECTORY) return -1;

    open_fs(node, 1, 0);
    current_process->cwd_node = node;
    if (kern_getcwd(current_process->cwd_path, sizeof(current_process->cwd_path)) != 0) {
        strncpy(current_process->cwd_path, "/", sizeof(current_process->cwd_path) - 1);
        current_process->cwd_path[sizeof(current_process->cwd_path) - 1] = '\0';
    }
    if (old_cwd && old_cwd != node) {
        close_fs(old_cwd);
    }
    return 0;
}


int sys_fchdir(int fd) {
    return kern_fchdir(fd);
}
// Helper to find name of an inode in a directory
// Returns allocated string (caller must free) or NULL
static char *find_name_by_inode(fs_node_t *dir, uint64_t inode) {
    if (!dir || !dir->readdir) return NULL;

    // We have to iterate linearly.
    // Assuming standard "readdir" semantics: index 0, 1, 2...
    // We check every entry.

    // Safety limit to prevent infinite loops in broken FS
    uint64_t index = 0;
    while (index < 100000) {
        struct dirent *d = readdir_fs(dir, index);
        if (!d) break; // End of directory

        if (d->d_ino == inode) {
            // Match found
            // Skip . and ..
            if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) {
                 index++;
                 continue;
            }

            // Allocate and copy
            int len = strlen(d->d_name);
            char *name = kmalloc(len + 1);
            if (!name) return NULL;
            memcpy(name, d->d_name, len);
            name[len] = '\0';
            return name;
        }
        index++;
    }
    return NULL;
}

int sys_getcwd(char *buf, size_t size) {
    if (size == 0) return -22;
    if (size > 4096) size = 4096;
    char *kbuf = kmalloc(size);
    if (!kbuf) return -12;
    int ret = kern_getcwd(kbuf, size);
    if (ret == 0) {
        if (copyout(kbuf, buf, strlen(kbuf) + 1) != 0) {
            kfree(kbuf, size);
            return -14;
        }
    }
    kfree(kbuf, size);
    return ret;
}

int kern_getcwd(char *buf, size_t size) {
    if (!buf || size < 2) return -1;

    // Allocate temp kernel buffer for reverse walk
    char *kbuf = kmalloc(4096);
    if (!kbuf) return -12;

    char *ptr = kbuf + 4096 - 1;
    *ptr = '\0';

    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : fs_root;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;

    fs_node_t *curr = cwd;

    if (curr == root || (curr->inode == root->inode)) {
        *(--ptr) = '/';
    } else {
        while (1) {
            if (curr == root || (curr->inode == root->inode)) {
                if (*ptr == '\0') *(--ptr) = '/';
                break;
            }

            fs_node_t *parent = finddir_fs(curr, "..");
            if (!parent) {
                kfree(kbuf, 4096);
                return -2;
            }

            if (parent->inode == curr->inode) {
                if (*ptr == '\0') *(--ptr) = '/';
                break;
            }

            char *name = find_name_by_inode(parent, curr->inode);
            if (!name) {
                 kfree(kbuf, 4096);
                 return -2;
            }

            int len = strlen(name);
            if (ptr - kbuf < len + 1) {
                kfree(name, len + 1);
                kfree(kbuf, 4096);
                return -36;
            }

            ptr -= len;
            memcpy(ptr, name, len);
            *(--ptr) = '/';
            kfree(name, len + 1);
            curr = parent;
        }
    }

    size_t len = (kbuf + 4096 - 1) - ptr;
    if (len >= size) {
        kfree(kbuf, 4096);
        return -34;
    }

    memcpy(buf, ptr, len + 1);
    kfree(kbuf, 4096);
    return 0;
}

// sys_proc_info - Get detailed info for a single process
int sys_proc_info(pid_t pid, sys_procinfo_t *info) {
    sys_procinfo_t kinfo;
    int ret = kern_proc_info(pid, &kinfo);
    if (ret == 0) {
        if (copyout(&kinfo, info, sizeof(sys_procinfo_t)) != 0) return -14;
    }
    return ret;
}

int kern_proc_info(pid_t pid, sys_procinfo_t *info) {
    if (!info) return -1;
    
    process_t *target = NULL;
    if (pid == 0) {
        target = current_process;
    } else {
        target = proc_find(pid);
    }
    
    if (!target) return -1;
    
    memset(info, 0, sizeof(sys_procinfo_t));
    info->pid = target->pid;
    info->ppid = target->ppid;
    
    if (target->p_pgrp) {
        info->pgid = target->p_pgrp->pg_id;
        if (target->p_pgrp->pg_session) {
            info->sid = target->p_pgrp->pg_session->s_sid;
        }
    }
    
    info->uid = target->uid;
    info->gid = target->gid;
    info->euid = target->euid;
    info->egid = target->egid;
    info->state = target->state;
    info->bitness = target->bitness;
    info->perso_id = (int16_t)target->perso_id;
    info->tty = target->tty ? (int16_t)target->tty->index : (int16_t)-1;
    info->nice = 0;
    info->start_time = target->start_time;
    info->user_time = target->utime;
    info->sys_time = target->stime;
    info->vsize = target->vm_map ? (uint32_t)target->vm_map->size : 0;
    info->rss = target->rusage.ru_maxrss > 0 ? (uint32_t)(target->rusage.ru_maxrss / 4) : 0;
    
    strncpy(info->name, target->comm, sizeof(info->name)-1);
    return 0;
}

// sys_proc_list - List all active PIDs
// Returns count of PIDs written, or total count if pids==NULL
int sys_proc_list(pid_t *pids, size_t count) {
    if (pids == NULL || count == 0) return kern_proc_list(NULL, 0);
    if (count > 1024) count = 1024;

    pid_t *kpids = kmalloc(count * sizeof(pid_t));
    if (!kpids) return -12;

    int ret = kern_proc_list(kpids, count);
    if (ret > 0) {
        if (copyout(kpids, pids, ret * sizeof(pid_t)) != 0) {
            kfree(kpids, count * sizeof(pid_t));
            return -14;
        }
    }
    kfree(kpids, count * sizeof(pid_t));
    return ret;
}

int kern_proc_list(pid_t *pids, size_t count) {
    int total_procs = 0;
    for (size_t i = 0; i < proc_slot_count(); i++) {
        process_t *proc = proc_slot(i);
        if (proc && proc->pid != -1) total_procs++;
    }
    
    if (!pids || count == 0) return total_procs;
    
    int copied = 0;
    for (size_t i = 0; i < proc_slot_count() && copied < (int)count; i++) {
        process_t *proc = proc_slot(i);
        if (proc && proc->pid != -1) {
            pids[copied] = proc->pid;
            copied++;
        }
    }
    return copied;
}

// sys_proc_count - Get total number of active processes
int sys_proc_count(void) {
    int count = 0;
    for (size_t i = 0; i < proc_slot_count(); i++) {
        process_t *proc = proc_slot(i);
        if (proc && proc->pid != -1) {
            count++;
        }
    }
    return count;
}

// sys_cpu_count - Get number of CPUs
// Returns: online CPU count
int sys_cpu_count(void) {
    return smp_get_cpu_count();
}

// sys_hostname - Get system hostname
// Returns: 0 on success, -1 on error
int sys_hostname(char *buf, size_t len) {
    if (len > 256) len = 256;
    char kbuf[256];
    int ret = kern_hostname(kbuf, len);
    if (ret == 0) {
        if (copyout(kbuf, buf, strlen(kbuf) + 1) != 0) return -14;
    }
    return ret;
}


int kern_hostname(char *buf, size_t len) {
    if (!buf || len == 0) return -1;
    extern char kernel_hostname[MAXHOSTNAMELEN];
    
    size_t hlen = 0;
    while (kernel_hostname[hlen] && hlen < MAXHOSTNAMELEN - 1) hlen++;
    
    if (len < hlen + 1) {
        memcpy(buf, kernel_hostname, len - 1);
        buf[len - 1] = '\0';
    } else {
        memcpy(buf, kernel_hostname, hlen + 1);
    }
    return 0;
}

/* sys_proc_* introspection: probe-and-fill API.
 * - On entry, *count is the caller's array capacity (0 = probe).
 * - On exit, *count is the actual number of entries the process has;
 *   if that exceeds capacity, the array is filled to capacity and the
 *   caller is expected to retry with a larger buffer.
 * Returns 0 on success, -ESRCH if pid is unknown, -EFAULT on copy. */

struct proc_thread_collect {
    process_t *proc;
    tid_t     *out_array;       /* user pointer (copyout target) */
    size_t     cap;             /* caller capacity */
    size_t     n;               /* total threads matched */
    int        copy_err;        /* -EFAULT if any copyout failed */
};

static void proc_thread_count_cb(thread_t *t, void *arg) {
    struct proc_thread_collect *c = arg;
    if (!t || t->proc != c->proc) return;
    if (c->out_array && c->n < c->cap) {
        tid_t tid = (tid_t)t->tid;
        if (copyout(&tid, &c->out_array[c->n], sizeof(tid_t)) != 0)
            c->copy_err = -14;
    }
    c->n++;
}

int sys_proc_threads(pid_t pid, tid_t *tids, size_t *count) {
    process_t *target = (pid == 0) ? current_process : proc_find(pid);
    if (!target) return -3;

    size_t cap = 0;
    if (count && copyin(count, &cap, sizeof(cap)) != 0) return -14;

    struct proc_thread_collect c = {
        .proc = target, .out_array = tids, .cap = cap, .n = 0, .copy_err = 0
    };
    sched_iterate_threads(proc_thread_count_cb, &c);
    if (c.copy_err) return c.copy_err;

    if (count && copyout(&c.n, count, sizeof(c.n)) != 0) return -14;
    return 0;
}

int sys_proc_fds(pid_t pid, sys_fd_t *fds, size_t *count) {
    process_t *target = (pid == 0) ? current_process : proc_find(pid);
    if (!target) return -3;

    size_t cap = 0;
    if (count && copyin(count, &cap, sizeof(cap)) != 0) return -14;

    size_t n = 0;
    for (int i = 0; i < MAX_FD; i++) {
        if (!target->fds[i]) continue;
        if (fds && n < cap) {
            sys_fd_t kbuf;
            memset(&kbuf, 0, sizeof(kbuf));
            kbuf.fd = i;
            kbuf.flags = (uint32_t)target->fds[i]->f_flag;
            const char *p = target->fds[i]->f_path;
            size_t plen = strlen(p);
            if (plen >= sizeof(kbuf.path)) plen = sizeof(kbuf.path) - 1;
            memcpy(kbuf.path, p, plen);
            kbuf.path[plen] = '\0';
            if (copyout(&kbuf, &fds[n], sizeof(kbuf)) != 0) return -14;
        }
        n++;
    }

    if (count && copyout(&n, count, sizeof(n)) != 0) return -14;
    return 0;
}

int sys_proc_maps(pid_t pid, sys_map_t *maps, size_t *count) {
    process_t *target = (pid == 0) ? current_process : proc_find(pid);
    if (!target) return -3;

    size_t cap = 0;
    if (count && copyin(count, &cap, sizeof(cap)) != 0) return -14;

    size_t n = 0;
    for (vm_area_t *a = target->vm_areas; a; a = a->next) {
        if (maps && n < cap) {
            sys_map_t kbuf;
            memset(&kbuf, 0, sizeof(kbuf));
            kbuf.start = a->vm_start;
            kbuf.end   = a->vm_end;
            /* Pack prot bits in low byte, vm_flags shifted into next byte
             * so a userspace tool can recover both halves with shift+mask. */
            kbuf.flags = (a->vm_prot & 0xFF) | ((a->vm_flags & 0xFF) << 8);
            const char *name = "[anon]";
            if (a->vm_file && a->vm_file->name[0]) name = a->vm_file->name;
            size_t nlen = strlen(name);
            if (nlen >= sizeof(kbuf.name)) nlen = sizeof(kbuf.name) - 1;
            memcpy(kbuf.name, name, nlen);
            kbuf.name[nlen] = '\0';
            if (copyout(&kbuf, &maps[n], sizeof(kbuf)) != 0) return -14;
        }
        n++;
    }

    if (count && copyout(&n, count, sizeof(n)) != 0) return -14;
    return 0;
}

int sys_proc_cwd(pid_t pid, char *buf, size_t len) {
    if (!buf || len == 0) return -14;
    
    process_t *target = (pid == 0) ? current_process : proc_find(pid);
    
    if (!target) return -3; // ESRCH
    
    size_t path_len = strlen(target->cwd_path) + 1;
    if (len < path_len) return -34; // ERANGE
    
    if (copyout(target->cwd_path, buf, path_len) != 0) return -14;
    return 0;
}

int sys_proc_exe(pid_t pid, char *buf, size_t len) {
    if (!buf || len == 0) return -14;
    
    process_t *target = (pid == 0) ? current_process : proc_find(pid);
    
    if (!target) return -3; // ESRCH
    
    size_t path_len = strlen(target->exec_path) + 1;
    if (len < path_len) return -34; // ERANGE
    
    if (copyout(target->exec_path, buf, path_len) != 0) return -14;
    return 0;
}

/* sys_proc_cmdline: copy out the cmdline_tail blob (NUL-separated
 * arguments, as captured at exec) into `argv` interpreted as a flat
 * char buffer of size `*argc` bytes.  *argc is always set to the
 * required byte count; if the user buffer was too small, what we
 * wrote is truncated but `*argc` reflects the true size so the
 * caller can re-allocate and retry.  The kernel doesn't reconstruct
 * the per-arg pointer array — userland tools (ps, /proc/N/cmdline
 * helpers) split on NUL themselves. */
int sys_proc_cmdline(pid_t pid, char **argv, size_t *argc) {
    process_t *target = (pid == 0) ? current_process : proc_find(pid);
    if (!target) return -3;

    size_t need = (size_t)target->cmdline_tail_len;
    size_t cap = 0;
    if (argc && copyin(argc, &cap, sizeof(cap)) != 0) return -14;

    if (argv && cap > 0 && need > 0) {
        size_t n = need < cap ? need : cap;
        if (copyout(target->cmdline_tail, (char *)argv, n) != 0) return -14;
    }

    if (argc && copyout(&need, argc, sizeof(need)) != 0) return -14;
    return 0;
}

/* sys_proc_environ: Substrate currently doesn't snapshot envp at exec
 * the way it does cmdline_tail, so this returns 0 entries.  When the
 * environ-snapshot lands, the implementation matches the cmdline path
 * verbatim — until then, callers see "no environ available" rather
 * than ENOSYS, which keeps `ps -e` and procfs from erroring out. */
int sys_proc_environ(pid_t pid, char **envp, size_t *envc) {
    (void)envp;
    process_t *target = (pid == 0) ? current_process : proc_find(pid);
    if (!target) return -3;

    if (envc) {
        size_t zero = 0;
        if (copyout(&zero, envc, sizeof(size_t)) != 0) return -14;
    }
    return 0;
}

/*
 * sys_proc_pers_name(perso_id, buf, len) — copy out the human-readable
 * personality name for `perso_id` (e.g. "Linux", "FreeBSD", "Native").
 * Used by `sys/proc.h` introspection helpers and by tools that render
 * `ps`-style personality columns.  Returns the byte length written
 * (including NUL) on success, 0 if `len` was 0, or -EFAULT on copyout
 * failure / -EINVAL if `buf` is NULL with non-zero `len`.
 */
int sys_proc_pers_name(int perso_id, char *buf, size_t len) {
    extern const char *perso_name(int id);
    const char *name = perso_name(perso_id);
    if (!name) name = "unknown";
    size_t need = strlen(name) + 1;
    if (len == 0) return (int)need;          /* probe: tell caller buffer size */
    if (!buf) return -EINVAL;
    size_t n = need <= len ? need : len;
    if (copyout((void *)name, buf, n) != 0) return -EFAULT;
    if (n < need) {
        /* Force NUL terminator at the truncated end. */
        char nul = '\0';
        if (copyout(&nul, buf + n - 1, 1) != 0) return -EFAULT;
    }
    return (int)need;
}

int sys_reboot(int cmd) {
    if (current_process->euid != 0) {
        return -EPERM;
    }

    switch (cmd) {
    case RB_AUTOBOOT:
    case RB_HALT_SYSTEM:
    case RB_POWER_OFF:
        // For now, all these perform a hard reset.
        // In the future, we would differentiate between halt, poweroff, and reboot.
        break;
    default:
        return -EINVAL;
    }

    // Keyboard controller reset
    while (inb(0x64) & 0x02);
    outb(0x64, 0xFE);
    
    // Fallback if that fails: Triple fault
    // (by loading 0-length IDT and causing exception)
    __asm__ volatile("lidt %0; int3"::"m"((uint16_t[3]){0,0,0}));
    
    return 0;
}

/* sys_setpriority - Set program scheduling priority (nice value) */
int sys_setpriority(int which, int who, int prio) {
    if (which > PRIO_USER || which < PRIO_PROCESS) return -EINVAL;
    if (prio < -20 || prio > 19) return -EINVAL;

    int found = 0;
    int affected = 0;
    int error = 0;

    int target_id = who;
    if (target_id == 0) {
        if (which == PRIO_PROCESS) target_id = current_process->pid;
        else if (which == PRIO_PGRP) target_id = current_process->p_pgrp ? current_process->p_pgrp->pg_id : 0;
        else if (which == PRIO_USER) target_id = current_process->uid;
    }

    /* Iterate over processes using hardcoded limit matching syscall.c conventions */
    for (size_t i = 0; i < proc_slot_count(); i++) {
        process_t *p = proc_slot(i);
        if (!p || p->pid == -1) continue;

        bool match = false;
        if (which == PRIO_PROCESS && p->pid == target_id) match = true;
        else if (which == PRIO_PGRP && p->p_pgrp && p->p_pgrp->pg_id == target_id) match = true;
        else if (which == PRIO_USER && (int)p->uid == target_id) match = true;

        if (!match) continue;
        found++;

        /* Permission check:
         * Root can change anything.
         * Unprivileged users can only change their own processes (uid matches euid/uid of target).
         * AND unprivileged users can only INCREASE nice value (lower priority).
         */
        if (current_process->euid != 0) {
            if (current_process->euid != p->uid && current_process->euid != p->euid) {
                error = -EPERM;
                continue;
            }
        }

        /* Determine current nice value of the target process */
        int current_nice = 0;
        int thread_prio = 20; /* Default */

        /* Find a thread belonging to p to get its priority */
        /* Use MAX_THREADS from sched.h */
        for (size_t t = 0; t < sched_thread_slot_count(); t++) {
            thread_t *thread = sched_thread_slot(t);
            if (thread && thread->tid != -1 && thread->proc == p) {
                thread_prio = thread->base_priority;
                break;
            }
        }
        current_nice = 20 - thread_prio;

        /* Check nice value direction for unprivileged users */
        if (current_process->euid != 0 && prio < current_nice) {
            error = -EACCES;
            continue;
        }

        /* Apply new priority */
        int new_base_prio = 20 - prio;
        if (new_base_prio < 1) new_base_prio = 1;
        if (new_base_prio > 40) new_base_prio = 40;

        for (size_t t = 0; t < sched_thread_slot_count(); t++) {
            thread_t *thread = sched_thread_slot(t);
            if (thread && thread->tid != -1 && thread->proc == p) {
                sched_set_priority(thread->tid, thread->sched_class, new_base_prio);
            }
        }
        affected++;
    }

    if (found == 0) return -ESRCH;
    if (affected == 0 && error != 0) return error;
    return 0;
}

/* sys_getpriority - Get program scheduling priority */
int sys_getpriority(int which, int who) {
    int target_id = who;
    if (target_id == 0) {
        if (which == PRIO_PROCESS) target_id = current_process->pid;
        else if (which == PRIO_PGRP) target_id = current_process->p_pgrp ? current_process->p_pgrp->pg_id : 0;
        else if (which == PRIO_USER) target_id = current_process->uid;
    }

    int found = 0;
    int best_nice = 20; /* Start with lowest priority (highest nice) */

    for (size_t i = 0; i < proc_slot_count(); i++) {
        process_t *p = proc_slot(i);
        if (!p || p->pid == -1) continue;

        bool match = false;
        if (which == PRIO_PROCESS && p->pid == target_id) match = true;
        else if (which == PRIO_PGRP && p->p_pgrp && p->p_pgrp->pg_id == target_id) match = true;
        else if (which == PRIO_USER && (int)p->uid == target_id) match = true;

        if (!match) continue;
        found++;

        int thread_prio = 20;
        for (size_t t = 0; t < sched_thread_slot_count(); t++) {
            thread_t *thread = sched_thread_slot(t);
            if (thread && thread->tid != -1 && thread->proc == p) {
                thread_prio = thread->base_priority;
                break;
            }
        }
        int nice = 20 - thread_prio;
        if (nice < best_nice) best_nice = nice;
    }

    if (found == 0) return -ESRCH;

    /* Return nice + 20 to avoid negative return values */
    return best_nice + 20;
}
int sys_yield(void) {
    sched_yield();
    return 0;
}

int sys_fsync(int fd) {
    (void)fd;
    return 0; // Stub
}

int sys_select(int nfds, void *rfds, void *wfds, void *efds, void *timeout) {
    (void)nfds; (void)rfds; (void)wfds; (void)efds; (void)timeout;
    return -ENOSYS;
}

int freebsd_sys_uname(void *ubuf) {
    (void)ubuf;
    return -ENOTSUP;
}

int sys_fstatat(int dirfd, const char *path, void *buf, int flags) {
    char kpath[256];
    struct stat kbuf;
    int ret;

    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    ret = kern_fstatat(dirfd, kpath, &kbuf, flags);
    if (ret == 0) {
        if (copyout(&kbuf, buf, sizeof(struct stat)) != 0) return -14;
    }
    return ret;
}
