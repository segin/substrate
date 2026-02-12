/*
 * kern/syscall.c - Generic (architecture-independent) syscall implementations
 *
 * This file contains the syscall implementations that are not specific
 * to any architecture. Architecture-specific code (dispatch, TLS, etc.)
 * remains in arch/i386/syscall.c or equivalent.
 */

/* Kernel internal includes */
#include <kern/sched.h>
#include <kern/version.h>
#include <kern/panic.h>
#include <kern/console.h>
#include <exec/perso/personality.h>
#include <vm/vm_kmem.h>
#include <include/sys/thr.h>
#include <include/sys/acct.h>
#include <include/sys/file.h>
#include <include/sys/proc.h>
#include <include/sys/signal.h>
#include <include/sys/session.h>
#include <kern/file.h>
#include <vfs/vfs.h>
#include <drivers/console/uart/uart.h>
#include <include/sys/sysinfo.h>
#include <sys/kern_syscalls.h>
#include <sys/utsname.h>

#include <sys/smp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <arch/x86-common/include/io.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include <sys/kern_syscalls.h>
#include <sys/utsname.h>

extern thread_t *current_thread; 
extern process_t *current_process;
extern process_t processes[64];
extern thread_t threads[256];

// Simple file structure allocator
#define MAX_SYSTEM_FILES 128
static file_t system_files[MAX_SYSTEM_FILES];
static file_t *file_free_list = NULL;
static bool file_system_initialized = false;

static void file_init_list(void) {
    for (int i = 0; i < MAX_SYSTEM_FILES - 1; i++) {
        system_files[i].f_next = &system_files[i + 1];
    }
    system_files[MAX_SYSTEM_FILES - 1].f_next = NULL;
    file_free_list = &system_files[0];
    file_system_initialized = true;
}

/*
 * file_alloc - Allocate a file structure from the free list.
 * Note: Assumes external locking or uniprocessor environment.
 */
file_t *file_alloc(void) {
    if (!file_system_initialized) {
        file_init_list();
    }

    if (file_free_list) {
        file_t *f = file_free_list;
        file_free_list = f->f_next;
        f->f_next = NULL;
        f->f_count = 1;
        return f;
    }
    return NULL;
}

void file_free(file_t *f) {
    if (!f) return;

    // Safety check: ensure pointer is within the static array
    if (f < system_files || f >= system_files + MAX_SYSTEM_FILES) {
        return;
    }

    // Prevent double-free
    if (f->f_count == 0) return;
    
    f->f_count = 0;
    f->f_data = NULL;
    f->f_next = file_free_list;
    file_free_list = f;
}

int kern_write(int fd, const char *buf, int len) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    // Check for console node if write_fs isn't fully generic yet
    if (f->f_data && ((fs_node_t*)f->f_data)->write) {
        return write_fs((fs_node_t*)f->f_data, f->f_offset, len, (uint8_t*)buf);
    }
    
    return 0;
}

int sys_write(int fd, const char *buf, int len) {
    if (len < 0) return -1;
    if (len == 0) return 0;

    void *kbuf = kmalloc(4096);
    if (!kbuf) return -12; // ENOMEM

    int total_written = 0;
    while (len > 0) {
        int to_write = (len > 4096) ? 4096 : len;
        if (copyin(buf + total_written, kbuf, to_write) != 0) {
            kfree(kbuf, 4096);
            return -14; // EFAULT
        }

        int bytes = kern_write(fd, kbuf, to_write);
        if (bytes <= 0) {
            if (total_written == 0) {
                kfree(kbuf, 4096);
                return bytes;
            }
            break;
        }

        total_written += bytes;
        len -= bytes;
        if (bytes < to_write) break;
    }
    kfree(kbuf, 4096);
    return total_written;
}

int kern_read(int fd, char *buf, int len) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    uint32_t bytes = read_fs((fs_node_t*)f->f_data, f->f_offset, len, (uint8_t*)buf);
    f->f_offset += bytes;
    return bytes;
}

int sys_read(int fd, char *buf, int len) {
    if (len < 0) return -1;
    if (len == 0) return 0;

    void *kbuf = kmalloc(4096);
    if (!kbuf) return -12; // ENOMEM

    int total_read = 0;
    while (len > 0) {
        int to_read = (len > 4096) ? 4096 : len;
        int bytes = kern_read(fd, kbuf, to_read);
        if (bytes <= 0) {
            if (total_read == 0) {
                kfree(kbuf, 4096);
                return bytes;
            }
            break;
        }

        if (copyout(kbuf, buf + total_read, bytes) != 0) {
            kfree(kbuf, 4096);
            return -14; // EFAULT
        }

        total_read += bytes;
        len -= bytes;
        if (bytes < to_read) break;
    }
    kfree(kbuf, 4096);
    return total_read;
}

int sys_open(const char *path, int flags, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    return kern_open(kpath, flags, mode);
}

int kern_open(const char *path, int flags, int mode) {
    (void)mode;
    if (!path) return -1;
    
    // Find free FD using hint
    int fd = -1;
    int start = current_process->next_fd;
    if (start < 0 || start >= MAX_FD) start = 0;

    for (int i = start; i < MAX_FD; i++) {
        if (!current_process->fds[i]) {
            fd = i;
            break;
        }
    }

    // Wrap around search
    if (fd == -1 && start > 0) {
        for (int i = 0; i < start; i++) {
            if (!current_process->fds[i]) {
                fd = i;
                break;
            }
        }
    }

    if (fd == -1) return -1;
    current_process->next_fd = fd + 1;

    // Lookup file
    // Handle absolute/relative. For now assume root relative if starts with /
    fs_node_t *node = 0;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;

    if (path[0] == '/') {
        // Skip leading / for finddir which usually expects name in dir
        if (path[1] == 0) node = root;
        else node = finddir_fs(root, (char*)path + 1);
    } else {
        node = finddir_fs(root, (char*)path);
    }

    if (!node) return -1;

    file_t *f = file_alloc();
    if (!f) return -1;

    f->f_data = node;
    f->f_offset = 0;
    f->f_flag = (short)flags;
    f->f_count = 1;

    current_process->fds[fd] = f;
    open_fs(node, 1, 0); // Open read/write?

    return fd;
}

// Helper for internal use (and userspace via sys_close)
void file_close_ptr(file_t *f) {
    if (!f) return;
    f->f_count--;
    if (f->f_count <= 0) {
        close_fs((fs_node_t*)f->f_data);
        file_free(f);
    }
}

int kern_close(int fd) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    file_close_ptr(f);
    current_process->fds[fd] = 0;

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



// Linux dirent structure for getdents
struct linux_dirent {
    unsigned long  d_ino;
    unsigned long  d_off;
    unsigned short d_reclen;
    char           d_name[];
};

int sys_getdents(unsigned int fd, void *dirp, unsigned int count) {
    if (count > 65536) count = 65536;
    void *kdirp = kmalloc(count);
    if (!kdirp) return -12;
    int ret = kern_getdents(fd, kdirp, count);
    if (ret > 0) {
        if (copyout(kdirp, dirp, ret) != 0) {
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
    
    char *buf = (char*)dirp;
    unsigned int bpos = 0;
    
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
        
        struct linux_dirent *ld = (struct linux_dirent*)(buf + bpos);
        ld->d_ino = d->d_ino;
        ld->d_off = f->f_offset + 1; // Offset of NEXT entry
        ld->d_reclen = reclen;
        for(int i=0; i<name_len; i++) ld->d_name[i] = d->d_name[i];
        ld->d_name[name_len] = 0;
        
        bpos += reclen;
        f->f_offset++;
    }
    
    return bpos;
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

extern int sys_vm86(void *v);
extern int sys_sysarch(int op, void *args);

// ...

// Helper to fill stat struct from fs_node
static void fill_stat(struct stat *buf, fs_node_t *node) {
    if (!buf) return;
    memset(buf, 0, sizeof(struct stat));
    buf->st_ino = node->inode;
    buf->st_size = (off_t)node->length;
    buf->st_uid = node->uid;
    buf->st_gid = node->gid;
    buf->st_mode = node->mask;
    buf->st_rdev = node->rdev;
    
    // Set file type bits
    if ((node->flags & 0x7) == FS_DIRECTORY)
        buf->st_mode |= 0040000;  // S_IFDIR
    else if ((node->flags & 0x7) == FS_SYMLINK)
        buf->st_mode |= 0120000;  // S_IFLNK
    else if ((node->flags & 0x7) == FS_CHARDEVICE)
        buf->st_mode |= 0020000;  // S_IFCHR
    else if ((node->flags & 0x7) == FS_BLOCKDEVICE)
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

    fs_node_t *node = 0;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;

    if (path[0] == '/') {
        if (path[1] == 0) node = root;
        else node = finddir_fs(root, (char*)path + 1);
    } else {
        node = finddir_fs(root, (char*)path);
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

int kern_mkdir(const char *p, int m) {
    if (!p) return -1;
    return vfs_mkdir(p, (uint16_t)m);
}
int sys_rmdir(const char *p) { 
    char kpath[256];
    if (copyinstr(p, kpath, sizeof(kpath), NULL) != 0) return -14;
    // kern_rmdir not in header yet, but following pattern
    return 0; 
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
int sys_clone(uint32_t f, void *s, int *p, void *t, int *c) { (void)f; (void)s; (void)p; (void)t; (void)c; return -1; }


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
    if (!path || !buf) return -1;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *node = vfs_lookup(root, path);
    if (!node) return -1;
    fill_stat(buf, node);
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
    if (!path || !buf) return -1;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *node = vfs_lookup_lstat(root, path);
    if (!node) return -1;
    fill_stat(buf, node);
    return 0;
}

// poll() implementation
#include <sys/poll.h>

int sys_poll(struct pollfd *fds, unsigned int nfds, int timeout) {
    if (nfds > 1024) return -22;
    struct pollfd *kfds = kmalloc(nfds * sizeof(struct pollfd));
    if (!kfds) return -12;
    if (copyin(fds, kfds, nfds * sizeof(struct pollfd)) != 0) {
        kfree(kfds, nfds * sizeof(struct pollfd));
        return -14;
    }
    int ret = kern_poll(kfds, nfds, timeout);
    if (ret >= 0) {
        if (copyout(kfds, fds, nfds * sizeof(struct pollfd)) != 0) {
            kfree(kfds, nfds * sizeof(struct pollfd));
            return -14;
        }
    }
    kfree(kfds, nfds * sizeof(struct pollfd));
    return ret;
}

int kern_poll(struct pollfd *fds, unsigned int nfds, int timeout) {
    int ready = 0;
    void *waiter = NULL; 
    
    while (1) {
        ready = 0;
        for (unsigned int i = 0; i < nfds; i++) {
            if (fds[i].fd < 0) {
                fds[i].revents = 0;
                continue;
            }
            
            file_t *f = (fds[i].fd < MAX_FD) ? current_process->fds[fds[i].fd] : NULL;
            short mask = 0;
            
            if (f && f->f_data) {
                mask = poll_fs((fs_node_t*)f->f_data, waiter);
                short ret_mask = mask & (fds[i].events | POLLERR | POLLHUP | POLLNVAL);
                fds[i].revents = ret_mask;
            } else {
                fds[i].revents = POLLNVAL;
            }
            
            if (fds[i].revents) ready++;
        }
        
        if (ready > 0) return ready;
        if (timeout == 0) return 0;
        
        if (timeout > 0 && timeout != -1) {
             timeout -= 10; 
             if (timeout <= 0) return 0;
        }
        
        sched_yield();
    }
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

// ioctl - device control
int sys_ioctl(int fd, uint32_t request, void *arg) {
    // ioctl arg can be anything. For security, we should really know the size.
    // However, many ioctls use small structs.
    // This is hard to fix generically without a table.
    // For now, at least validate the pointer if it looks like one.
    return kern_ioctl(fd, request, arg);
}

int kern_ioctl(int fd, uint32_t request, void *arg) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -1;
    
    if (((fs_node_t*)f->f_data)->ioctl) {
        return ((fs_node_t*)f->f_data)->ioctl((fs_node_t*)f->f_data, request, arg);
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

int kern_unlink(const char *path) {
    if (!path) return -1;
    
    char dir[256];
    char file[128];
    
    // Find the last slash to separate directory and filename
    const char *last_slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    
    fs_node_t *parent = NULL;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    
    if (!last_slash) {
        // No slash - parent is CWD
        parent = current_process->cwd_node ? current_process->cwd_node : root;
        if (strlen(path) >= sizeof(file)) return -36; // ENAMETOOLONG
        strcpy(file, path);
    } else if (last_slash == path) {
        // Only one slash at the beginning - parent is root
        parent = root;
        if (strlen(path + 1) >= sizeof(file)) return -36; // ENAMETOOLONG
        strcpy(file, path + 1);
    } else {
        // Split into dir and file
        size_t dirlen = last_slash - path;
        if (dirlen >= sizeof(dir)) return -36; // ENAMETOOLONG
        memcpy(dir, path, dirlen);
        dir[dirlen] = '\0';
        
        if (strlen(last_slash + 1) >= sizeof(file)) return -36; // ENAMETOOLONG
        strcpy(file, last_slash + 1);
        
        parent = vfs_lookup(root, dir);
    }
    
    if (!parent || !file[0]) return -1;
    
    return unlink_fs(parent, file);
}

int sys_link(const char *oldpath, const char *newpath) {
    char kold[256], knew[256];
    if (copyinstr(oldpath, kold, sizeof(kold), NULL) != 0) return -14;
    if (copyinstr(newpath, knew, sizeof(knew), NULL) != 0) return -14;
    return kern_link(kold, knew);
}

int kern_link(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -1;

    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;

    // Resolve oldpath to source node
    fs_node_t *source = vfs_lookup(cwd, oldpath);
    if (!source) return -1;

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
        if (strlen(newpath) >= sizeof(file)) return -36; // ENAMETOOLONG
        strcpy(file, newpath);
    } else if (last_slash == newpath) {
        parent = root;
        if (strlen(newpath + 1) >= sizeof(file)) return -36; // ENAMETOOLONG
        strcpy(file, newpath + 1);
    } else {
        size_t dirlen = last_slash - newpath;
        if (dirlen >= sizeof(dir)) return -36; // ENAMETOOLONG
        memcpy(dir, newpath, dirlen);
        dir[dirlen] = '\0';
        
        if (strlen(last_slash + 1) >= sizeof(file)) return -36; // ENAMETOOLONG
        strcpy(file, last_slash + 1);
        parent = vfs_lookup(root, dir);
    }

    if (!parent || !file[0]) return -1;

    return link_fs(parent, source, file);
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

int kern_readlink(const char *pathname, char *buf, size_t bufsiz) {
    if (!pathname || !buf || bufsiz == 0) return -14; // EFAULT
    
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;
    
    // Lookup the symlink (using lstat-like behavior)
    fs_node_t *node = vfs_lookup_lstat(cwd, pathname);
    if (!node) return -2; // ENOENT
    
    // Check if it's a symlink
    if (!(node->flags & FS_SYMLINK)) return -22; // EINVAL - not a symlink
    
    // Read the link target
    int ret = readlink_fs(node, buf, bufsiz);
    return ret;
}

int sys_access(const char *path, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    return kern_access(kpath, mode);
}

int kern_access(const char *path, int mode) {
    if (!path) return -1;

    fs_node_t *node = 0;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;

    if (path[0] == '/') {
        if (path[1] == 0) node = root;
        else node = finddir_fs(root, (char*)path + 1);
    } else {
        node = finddir_fs(root, (char*)path);
    }

    if (!node) return -1;

    // F_OK check
    if (mode == F_OK) return 0;

    return vfs_check_permissions(node, current_process->uid, current_process->gid, mode);
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

    int f1 = -1, f2 = -1;
    int start = current_process->next_fd;
    if (start < 0 || start >= MAX_FD) start = 0;

    // Search from hint
    for (int i = start; i < MAX_FD; i++) {
        if (!current_process->fds[i]) {
            if (f1 == -1) f1 = i;
            else if (f2 == -1) { f2 = i; break; }
        }
    }

    // Wrap around if needed
    if (f2 == -1 && start > 0) {
        for (int i = 0; i < start; i++) {
            if (!current_process->fds[i]) {
                if (f1 == -1) f1 = i;
                else if (f2 == -1) { f2 = i; break; }
            }
        }
    }

    if (f1 == -1 || f2 == -1) return -1;
    current_process->next_fd = f2 + 1;

    file_t *rf = file_alloc();
    rf->f_data = read_node;
    rf->f_flag = 0; // O_RDONLY
    current_process->fds[f1] = rf;

    file_t *wf = file_alloc();
    wf->f_data = write_node;
    wf->f_flag = 0; // O_WRONLY
    current_process->fds[f2] = wf;

    fds[0] = f1;
    fds[1] = f2;
    return 0;
}

int sys_dup(int oldfd) {
    if (oldfd < 0 || oldfd >= MAX_FD) return -1;
    file_t *f = current_process->fds[oldfd];
    if (!f) return -1;

    // Find free FD with hint
    int newfd = -1;
    int start = current_process->next_fd;
    if (start < 0 || start >= MAX_FD) start = 0;

    for (int i = start; i < MAX_FD; i++) {
        if (!current_process->fds[i]) {
            newfd = i;
            break;
        }
    }

    // Wrap around
    if (newfd == -1 && start > 0) {
        for (int i = 0; i < start; i++) {
            if (!current_process->fds[i]) {
                newfd = i;
                break;
            }
        }
    }

    if (newfd == -1) return -1;
    current_process->next_fd = newfd + 1;

    current_process->fds[newfd] = f;
    f->f_count++;
    return newfd;
}

int sys_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= MAX_FD) return -1;
    if (newfd < 0 || newfd >= MAX_FD) return -1;
    if (oldfd == newfd) return newfd;

    file_t *f = current_process->fds[oldfd];
    if (!f) return -1;

    if (current_process->fds[newfd]) {
        file_close_ptr(current_process->fds[newfd]);
    }

    current_process->fds[newfd] = f;
    f->f_count++;
    return newfd;
}

int sys_chmod(const char *path, int mode) {
    (void)path; (void)mode;
    return 0;
}

int sys_lchown(const char *path, int uid, int gid) {
    (void)path; (void)uid; (void)gid;
    return 0;
}

int sys_fcntl(int fd, int cmd, int arg) {
    (void)fd; (void)cmd; (void)arg;
    return 0;
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
<<<<<<< HEAD
    int kstatus = 0;
    int ret = kern_waitpid(pid, status ? &kstatus : NULL, options);
    if (ret > 0 && status) {
=======
    int kstatus;
    int ret = kern_waitpid(pid, status ? &kstatus : NULL, options);
    if (ret >= 0 && status) {
>>>>>>> main
        if (copyout(&kstatus, status, sizeof(int)) != 0) return -14;
    }
    return ret;
}

int kern_waitpid(int pid, int *status, int options) {
    (void)options;
    while (1) {
        bool found = false;
        for (int i = 0; i < 16; i++) {
            if (processes[i].pid == -1) continue;
            if (processes[i].ppid != current_process->pid) continue;
            if (pid != -1 && processes[i].pid != pid) continue;
            found = true;
            bool all_zombies = true;
            for (int j = 0; j < 64; j++) {
                if (threads[j].proc == &processes[i] && threads[j].tid != -1) {
                    if (threads[j].state != THREAD_ZOMBIE) {
                        all_zombies = false;
                        break;
                    }
                }
            }
            if (all_zombies) {
                if (status) *status = processes[i].exit_code;
                int child_pid = processes[i].pid;
                processes[i].pid = -1;
                return child_pid;
            }
        }
        if (!found) return -1;
        sched_sleep(&current_process->pid);
    }
}

int sys_getpid(void) { if(current_process) return current_process->pid; return 0; }

#include <sys/exec.h>

int sys_execve(const char *f, char *const a[], char *const e[]) {
<<<<<<< HEAD
    char kpath[256];
    if (copyinstr(f, kpath, sizeof(kpath), NULL) != 0) return -14;

    // We pass untrusted user pointers for a and e.
    // The ELF loader (elf_execve) has been updated to handle them safely
    // using capture helpers (copyin/copyinstr).
    return kern_execve(kpath, a, e);
=======
    char kf[256];
    if (copyinstr(f, kf, sizeof(kf), NULL) != 0) return -14;
    return kern_execve(kf, a, e);
>>>>>>> main
}

int kern_execve(const char *f, char *const a[], char *const e[]) {
    return exec_dispatch(f, a, e);
}

/* sys_fork and sys_vfork are arch-specific (need registers_t) - in arch/i386/syscall.c */
extern int sys_fork(void);
extern int sys_vfork(void);

int sys_mknod(const char *p, int m, int d) { (void)p; (void)m; (void)d; return 0; }

int vfs_mount_legacy(const char *device, const char *path, const char *type, uint32_t flags, void *data);
int vfs_unmount_legacy(const char *path);
fs_node_t *vfs_lookup(fs_node_t *root, const char *path);

int sys_mount(const char *source, const char *target, const char *fstype, unsigned long flags, void *data) {
<<<<<<< HEAD
    char ksource[256], ktarget[256], ktype[64];
=======
    char ksource[256], ktarget[256], kfstype[64];
>>>>>>> main
    if (source) {
        if (copyinstr(source, ksource, sizeof(ksource), NULL) != 0) return -14;
    }
    if (copyinstr(target, ktarget, sizeof(ktarget), NULL) != 0) return -14;
<<<<<<< HEAD
    if (copyinstr(fstype, ktype, sizeof(ktype), NULL) != 0) return -14;

    // data is filesystem specific, hard to wrap generically.
    return kern_mount(source ? ksource : NULL, ktarget, ktype, flags, data);
=======
    if (copyinstr(fstype, kfstype, sizeof(kfstype), NULL) != 0) return -14;

    return kern_mount(source ? ksource : NULL, ktarget, kfstype, flags, data);
>>>>>>> main
}

int kern_mount(const char *source, const char *target, const char *fstype, unsigned long flags, void *data) {
    if (!target || !fstype) return -1;
    if (current_process->euid != 0) return -EPERM;
    return vfs_mount_legacy(source, target, fstype, (uint32_t)flags, data);
}

int sys_umount(const char *target) {
    char ktarget[256];
    if (copyinstr(target, ktarget, sizeof(ktarget), NULL) != 0) return -14;
    return kern_umount(ktarget);
}

<<<<<<< HEAD
int sys_umount(const char *target) {
    char ktarget[256];
    if (copyinstr(target, ktarget, sizeof(ktarget), NULL) != 0) return -14;
    return kern_umount(ktarget);
}

=======
>>>>>>> main
int kern_umount(const char *target) {
    if (!target) return -1;
    if (current_process->euid != 0) return -EPERM;
    return vfs_unmount_legacy(target);
}


int sys_nanosleep(void *req, void *rem) { (void)req; (void)rem; return 0; }

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
    
    if (path[0] == '/') {
        node = vfs_lookup(root, path);
    } else {
        // Relative path - lookup from cwd
        fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;
        node = vfs_lookup(cwd, path);
    }
    
    if (!node) return -1;
    if ((node->flags & 0x7) != FS_DIRECTORY) return -1;
    
    current_process->cwd_node = node;
    return 0;
}

int kern_fchdir(int fd) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -1;
    
    fs_node_t *node = (fs_node_t*)f->f_data;
    if ((node->flags & 0x7) != FS_DIRECTORY) return -1;
    
    current_process->cwd_node = node;
    return 0;
}

<<<<<<< HEAD

=======
int sys_fchdir(int fd) {
    return kern_fchdir(fd);
}
>>>>>>> main
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
<<<<<<< HEAD
    if (size > 4096) size = 4096;
    char *kbuf = kmalloc(size);
    if (!kbuf) return -12;
=======
    if (size == 0) return -22;
    char *kbuf = kmalloc(size);
    if (!kbuf) return -12;
    
>>>>>>> main
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
<<<<<<< HEAD

int kern_getcwd(char *buf, size_t size) {
    if (!buf || size < 2) return -1;
=======
>>>>>>> main

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
        for (int i = 0; i < 64; i++) {
            if (processes[i].pid == pid) {
                target = &processes[i];
                break;
            }
        }
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
    info->state = target->state;
    info->bitness = target->bitness;
    info->start_time = target->start_time;
    
    strncpy(info->name, target->comm, sizeof(info->name)-1);
    return 0;
}

// sys_proc_list - List all active PIDs
// Returns count of PIDs written, or total count if pids==NULL
int sys_proc_list(pid_t *pids, size_t count) {
<<<<<<< HEAD
    if (count == 0 || !pids) return kern_proc_list(NULL, 0);
    if (count > 1024) count = 1024;
    pid_t *kpids = kmalloc(count * sizeof(pid_t));
    if (!kpids) return -12;
=======
    if (pids == NULL || count == 0) return kern_proc_list(NULL, 0);

    pid_t *kpids = kmalloc(count * sizeof(pid_t));
    if (!kpids) return -12;

>>>>>>> main
    int ret = kern_proc_list(kpids, count);
    if (ret > 0) {
        if (copyout(kpids, pids, ret * sizeof(pid_t)) != 0) {
            kfree(kpids, count * sizeof(pid_t));
            return -14;
<<<<<<< HEAD
        }
    }
    kfree(kpids, count * sizeof(pid_t));
    return ret;
}

int kern_proc_list(pid_t *pids, size_t count) {
    int total_procs = 0;
    
    // First pass: count active processes
    for (int i = 0; i < 64; i++) {
        if (processes[i].pid != -1) {
            total_procs++;
=======
>>>>>>> main
        }
    }
    kfree(kpids, count * sizeof(pid_t));
    return ret;
}

int kern_proc_list(pid_t *pids, size_t count) {
    int total_procs = 0;
    for (int i = 0; i < 64; i++) {
        if (processes[i].pid != -1) total_procs++;
    }
    
    if (!pids || count == 0) return total_procs;
    
    int copied = 0;
    for (int i = 0; i < 64 && copied < (int)count; i++) {
        if (processes[i].pid != -1) {
            pids[copied++] = processes[i].pid;
        }
    }
    return copied;
}

// sys_proc_count - Get total number of active processes
int sys_proc_count(void) {
    int count = 0;
    for (int i = 0; i < 64; i++) {
        if (processes[i].pid != -1) {
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
    char *kbuf = kmalloc(len);
    if (!kbuf) return -12;

    int ret = kern_hostname(kbuf, len);
    if (ret == 0) {
        if (copyout(kbuf, buf, strlen(kbuf) + 1) != 0) {
            kfree(kbuf, len);
            return -14;
        }
    }
    kfree(kbuf, len);
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

int sys_proc_threads(pid_t pid, tid_t *tids, size_t *count) {
    (void)pid; (void)tids; (void)count;
    return -1; // ENOSYS stub
}

int sys_proc_fds(pid_t pid, sys_fd_t *fds, size_t *count) {
    (void)pid; (void)fds; (void)count;
    return -1;
}

int sys_proc_maps(pid_t pid, sys_map_t *maps, size_t *count) {
    (void)pid; (void)maps; (void)count;
    return -1;
}

int sys_proc_cwd(pid_t pid, char *buf, size_t len) {
    (void)pid; (void)buf; (void)len;
    return -1;
}

int sys_proc_exe(pid_t pid, char *buf, size_t len) {
    (void)pid; (void)buf; (void)len;
    return -1;
}

int sys_proc_cmdline(pid_t pid, char **argv, size_t *argc) {
    (void)pid; (void)argv; (void)argc;
    return -1;
}

int sys_proc_environ(pid_t pid, char **envp, size_t *envc) {
    (void)pid; (void)envp; (void)envc;
    return -1;
}

int sys_reboot(int cmd) {
    (void)cmd;
    // For now, always reboot. 
    // In a real system we'd check cmd for RB_HALT, RB_POWEROFF etc.
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
    for (int i = 0; i < 64; i++) {
        process_t *p = &processes[i];
        if (p->pid == -1) continue;

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
        for (int t = 0; t < MAX_THREADS; t++) {
            if (threads[t].tid != -1 && threads[t].proc == p) {
                thread_prio = threads[t].base_priority;
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

        for (int t = 0; t < MAX_THREADS; t++) {
            if (threads[t].tid != -1 && threads[t].proc == p) {
                sched_set_priority(threads[t].tid, threads[t].sched_class, new_base_prio);
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

    for (int i = 0; i < 64; i++) {
        process_t *p = &processes[i];
        if (p->pid == -1) continue;

        bool match = false;
        if (which == PRIO_PROCESS && p->pid == target_id) match = true;
        else if (which == PRIO_PGRP && p->p_pgrp && p->p_pgrp->pg_id == target_id) match = true;
        else if (which == PRIO_USER && (int)p->uid == target_id) match = true;

        if (!match) continue;
        found++;

        int thread_prio = 20;
        for (int t = 0; t < MAX_THREADS; t++) {
            if (threads[t].tid != -1 && threads[t].proc == p) {
                thread_prio = threads[t].base_priority;
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
