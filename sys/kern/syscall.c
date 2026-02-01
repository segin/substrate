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
#include <include/sys/thr.h>
#include <include/sys/acct.h>
#include <include/sys/file.h>
#include <include/sys/proc.h>
#include <include/sys/signal.h>
#include <include/sys/session.h>
#include <vfs/vfs.h>
#include <drivers/console/uart/uart.h>
#include <include/sys/sysinfo.h>

#include <sys/smp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

extern thread_t *current_thread; 
extern process_t *current_process;
extern process_t processes[64];
extern thread_t threads[256];

extern int sys_acct(const char *path);
extern int64_t sys_time(int64_t *tloc);
extern int sys_sigaction(int sig, const void *act, void *oact);

// Simple file structure allocator
#define MAX_SYSTEM_FILES 128
static file_t system_files[MAX_SYSTEM_FILES];
static file_t *file_free_list = NULL;
static bool file_system_initialized = false;

static void file_init_list(void) {
    for (int i = 0; i < MAX_SYSTEM_FILES - 1; i++) {
        system_files[i].next_free = &system_files[i + 1];
    }
    system_files[MAX_SYSTEM_FILES - 1].next_free = NULL;
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
        file_free_list = f->next_free;
        f->next_free = NULL;
        f->ref_count = 1;
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
    if (f->ref_count == 0) return;

    f->ref_count = 0;
    f->node = NULL;
    f->next_free = file_free_list;
    file_free_list = f;
}

int sys_write(int fd, const char *buf, int len) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    // Check for console node if write_fs isn't fully generic yet
    if (f->node && f->node->write) {
        return write_fs(f->node, f->offset, len, (uint8_t*)buf);
    }
    
    return 0;
}

int sys_read(int fd, char *buf, int len) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    uint32_t bytes = read_fs(f->node, f->offset, len, (uint8_t*)buf);
    f->offset += bytes;
    return bytes;
}

int sys_open(const char *path, int flags, int mode) {
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

    f->node = node;
    f->offset = 0;
    f->flags = flags;
    f->ref_count = 1;

    current_process->fds[fd] = f;
    open_fs(node, 1, 0); // Open read/write?

    return fd;
}

// Helper for internal use (and userspace via sys_close)
void file_close_ptr(file_t *f) {
    if (!f) return;
    f->ref_count--;
    if (f->ref_count <= 0) {
        close_fs(f->node);
        file_free(f);
    }
}

int sys_close(int fd) {
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

int64_t sys_lseek(int fd, uint32_t off_lo, uint32_t off_hi, int w) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    off_t off = ((off_t)off_hi << 32) | off_lo;
    
    if (w == 0) f->offset = off; // SEEK_SET
    else if (w == 1) f->offset += off; // SEEK_CUR
    else if (w == 2) f->offset = f->node->length + off; // SEEK_END
    
    return f->offset;
}



// Linux dirent structure for getdents
struct linux_dirent {
    unsigned long  d_ino;
    unsigned long  d_off;
    unsigned short d_reclen;
    char           d_name[];
};

int sys_getdents(unsigned int fd, void *dirp, unsigned int count) {
    if (fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    char *buf = (char*)dirp;
    unsigned int bpos = 0;
    
    while (bpos < count) {
        // Read one entry
        struct dirent *d = readdir_fs(f->node, f->offset);
        if (!d) {
            // EOF
            if (bpos == 0) return 0; // EOF on first try
            break; // Return what we have
        }
        
        // Calculate size
        int name_len = 0;
        while(d->name[name_len]) name_len++;
        
        int reclen = sizeof(unsigned long) * 2 + sizeof(unsigned short) + name_len + 1;
        reclen = (reclen + 3) & ~3; // Align to 4 bytes
        
        if (bpos + reclen > count) {
            // Buffer full
            if (bpos == 0) return -22; // EINVAL (Buffer too small for even one)
            break; 
        }
        
        struct linux_dirent *ld = (struct linux_dirent*)(buf + bpos);
        ld->d_ino = d->ino;
        ld->d_off = f->offset + 1; // Offset of NEXT entry
        ld->d_reclen = reclen;
        for(int i=0; i<name_len; i++) ld->d_name[i] = d->name[i];
        ld->d_name[name_len] = 0;
        // Zero pad if needed for alignment
        // (already done by simple pointer math for next entry, but maybe nice to clear garbage)
        
        bpos += reclen;
        f->offset++;
    }
    
    return bpos;
}

struct utsname {
    char sysname[256];
    char nodename[256];
    char release[256];
    char version[256];
    char machine[256];
    char domainname[256];
};

/* Validate user address range is within user space (below kernel) */
static int validate_user_buffer(const void *buf, size_t size) {
    uintptr_t start = (uintptr_t)buf;
    uintptr_t end = start + size;
    
    /* NULL check */
    if (start < 0x1000) return -1;
    
    /* Overflow check */
    if (end < start) return -1;
    
    /* Must be below kernel space at 0xC0000000 */
    if (end > 0xC0000000) return -1;
    
    return 0;
}

int sys_uname(struct utsname *buf) {
    if (!buf) return -14; /* EFAULT */
    
    /* Validate user buffer can hold entire struct */
    if (validate_user_buffer(buf, sizeof(struct utsname)) != 0) {
        return -14; /* EFAULT */
    }
    
    extern char kernel_hostname[MAXHOSTNAMELEN];
    
    /* Build result in kernel buffer first */
    struct utsname kbuf;
    memset(&kbuf, 0, sizeof(kbuf));
    
    strncpy(kbuf.sysname, "Substrate", 255);
    kbuf.sysname[255] = '\0';
    strncpy(kbuf.nodename, kernel_hostname, 255);
    kbuf.nodename[255] = '\0';
    strncpy(kbuf.release, "0.1", 255);
    kbuf.release[255] = '\0';
    strncpy(kbuf.version, "Kernel", 255);
    kbuf.version[255] = '\0';
    strncpy(kbuf.machine, "i386", 255);
    kbuf.machine[255] = '\0';
    kbuf.domainname[0] = '\0';
    
    /* Copy to user space - already validated above */
    memcpy(buf, &kbuf, sizeof(struct utsname));
    
    return 0;
}

extern void proc_exit(int code);

int sys_exit(int code) {
    proc_exit(code);
    return 0;
}

int sys_thr_new(struct thr_param *param, int param_size) {
    if (!param || param_size < (int)sizeof(struct thr_param)) return -1;
    struct thr_param p = *param;
    void *stack_top = (char*)p.stack_base + p.stack_size;
    int tid = sched_create_thread(current_process, p.start_func, stack_top, p.arg);
    if (tid > 0) {
        if (p.child_tid) *p.child_tid = tid;
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
    if (!p) return -1;
    return vfs_mkdir(p, (uint16_t)m);
}
int sys_rmdir(const char *p) { (void)p; return 0; }
int sys_getuid(void) { return current_process->uid; }
int sys_getgid(void) { return current_process->gid; }
int sys_getppid(void) { return current_process->ppid; }
int sys_geteuid(void) { return current_process->uid; } // No EUID yet
int sys_getegid(void) { return current_process->gid; } // No EGID yet
int sys_setuid(int u) {
    if (current_process->uid == 0) {
        current_process->uid = u;
        return 0;
    }
    return -1;
}
int sys_setgid(int g) {
    if (current_process->uid == 0) {
        current_process->gid = g;
        return 0;
    }
    return -1;
}
int sys_clone(uint32_t f, void *s, int *p, void *t, int *c) { (void)f; (void)s; (void)p; (void)t; (void)c; return -1; }


int sys_stat(const char *path, struct stat *buf) { 
    if (!path || !buf) return -1;
    
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *node = vfs_lookup(root, path);
    
    if (!node) return -1;
    fill_stat(buf, node);
    return 0; 
}


int sys_lstat(const char *path, struct stat *buf) {
    if (!path || !buf) return -1;
    
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    // Use vfs_lookup_lstat which avoids following the final symlink
    fs_node_t *node = vfs_lookup_lstat(root, path);
    
    if (!node) return -1;
    fill_stat(buf, node);
    return 0;
}

// poll() implementation
#include <sys/poll.h>

int sys_poll(struct pollfd *fds, unsigned int nfds, int timeout) {
    // Sanity check
    if (nfds > 1024) return -22; // EINVAL
    
    // Copy fds from user if needed? 
    // They are in user memory, we can access directly (assuming kernel has access)
    // but strict separation requires copy or verify. Substrate has shared addr space roughly.
    // We should copy to be safe/correct if we modify revents.
    
    // Allocate kernel buffer for safety (avoid TOCTOU or paging issues if pmap changes in loop)
    // For now, assume direct access is fine as we only write revents.
    
    // Timeout loop
    // Convert timeout (ms) to ticks or similar.
    // Since we don't have full wait queues, we'll do a busy-wait/yield loop 
    // if no events are ready immediately. This is not efficient but provides the "behavior".
    

    // extern unsigned long get_ticks(void); // Assuming this exists or similar
    
    int ready = 0;
    
    // POLL Loop
    // 1. Scan all FDs
    // 2. If any ready, return count.
    // 3. If none ready and timeout expired, return 0.
    // 4. If none ready types, yield/sleep and retry.
    
    // For "Infrastructure", we pass a dummy 'waiter' for now since
    // we don't have the wait queue object.
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
            
            if (f && f->node) {
                mask = poll_fs(f->node, waiter);
                
                // Mask against requested events
                // Always return ERR/HUP/NVAL
                short ret_mask = mask & (fds[i].events | POLLERR | POLLHUP | POLLNVAL);
                
                // If the node reported nothing, but we requested generic read/write, 
                // and it didn't support poll (0), poll_fs might return 0.
                
                fds[i].revents = ret_mask;
            } else {
                fds[i].revents = POLLNVAL;
            }
            
            if (fds[i].revents) ready++;
        }
        
        if (ready > 0) return ready;
        if (timeout == 0) return 0;
        
        // Timeout handling (simplified)
        // If timeout > 0, we should decrement or check time.
        // For now, just return 0 to prevent hanging the shell if it expects instant response.
        // Busybox shell usually polls with -1 (infinite) for input.
        // If we return 0 immediately, it will spin 100% CPU.
        // So we MUST yield at least once.
        if (timeout > 0 && timeout != -1) {
             timeout -= 10; // Approx 10ms per yield?
             if (timeout <= 0) return 0;
        }
        
        // Wait/Yield
        // Ideally: sched_sleep(waiter);
        // But since drivers don't wake us, we yield.
        sched_yield();
        
        // Avoid infinite hang if drivers never change state:
        // Checking for signals?
        // if (current_thread->sig_pending) return -4; // EINTR
    }
}

int sys_fstat(int fd, struct stat *buf) {
    if (fd < 0 || fd >= MAX_FD || !buf) return -1;
    file_t *f = current_process->fds[fd];
    if (!f || !f->node) return -1;
    fill_stat(buf, f->node);
    return 0;
}

// ioctl - device control
int sys_ioctl(int fd, uint32_t request, void *arg) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f || !f->node) return -1;
    
    if (f->node->ioctl) {
        return f->node->ioctl(f->node, request, arg);
    }
    
    // Default: not supported
    return -1;
}

/* sys_setsid is now implemented in pm/pgrp.c */
extern int sys_setsid(void);


int sys_unlink(const char *path) {
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
        strcpy(file, path);
    } else if (last_slash == path) {
        // Only one slash at the beginning - parent is root
        parent = root;
        strcpy(file, path + 1);
    } else {
        // Split into dir and file
        size_t dirlen = last_slash - path;
        if (dirlen >= sizeof(dir)) return -1;
        memcpy(dir, path, dirlen);
        dir[dirlen] = '\0';
        strcpy(file, last_slash + 1);
        
        parent = vfs_lookup(root, dir);
    }
    
    if (!parent || !file[0]) return -1;
    
    return unlink_fs(parent, file);
}

int sys_link(const char *oldpath, const char *newpath) {
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
        strcpy(file, newpath);
    } else if (last_slash == newpath) {
        parent = root;
        strcpy(file, newpath + 1);
    } else {
        size_t dirlen = last_slash - newpath;
        if (dirlen >= sizeof(dir)) return -1;
        memcpy(dir, newpath, dirlen);
        dir[dirlen] = '\0';
        strcpy(file, last_slash + 1);
        parent = vfs_lookup(root, dir);
    }

    if (!parent || !file[0]) return -1;

    return link_fs(parent, source, file);
}

int sys_readlink(const char *pathname, char *buf, size_t bufsiz) {
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

int sys_sync(void) {
    // In a real system, we'd iterate over all mounted filesystems
    // and call their sync methods.
    return 0;
}

extern int sys_stat(const char *p, struct stat *buf);
extern void pipe_create(fs_node_t **read_node, fs_node_t **write_node);

int sys_pipe(int *fds) {
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
    rf->node = read_node;
    rf->flags = 0; // O_RDONLY
    current_process->fds[f1] = rf;

    file_t *wf = file_alloc();
    wf->node = write_node;
    wf->flags = 0; // O_WRONLY
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
    f->ref_count++;
    return newfd;
}

int sys_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= MAX_FD) return -1;
    if (newfd < 0 || newfd >= MAX_FD) return -1;
    if (oldfd == newfd) return newfd;

    file_t *f = current_process->fds[oldfd];
    if (!f) return -1;

    if (current_process->fds[newfd]) {
        sys_close(newfd);
    }

    current_process->fds[newfd] = f;
    f->ref_count++;
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
    return sys_sigaction(sig, &act, NULL);
}

int sys_waitpid(int pid, int *status, int options) {
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

int sys_execve(const char *f, char *const a[], char *const e[]) {
    extern int elf_execve(const char *path, char *const argv[], char *const envp[]);
    return elf_execve(f, a, e);
}

/* sys_fork and sys_vfork are arch-specific (need registers_t) - in arch/i386/syscall.c */
extern int sys_fork(void);
extern int sys_vfork(void);

int sys_mknod(const char *p, int m, int d) { (void)p; (void)m; (void)d; return 0; }

int sys_mount(const char *source, const char *target, const char *fstype, unsigned long flags, void *data) {
    if (!target || !fstype) return -1;
    // TODO: Check permissions (superuser)
    return vfs_mount(source, target, fstype, (uint32_t)flags, data);
}

extern int vfs_unmount(const char *path);

int sys_umount(const char *target) { 
    if (!target) return -1;
    // TODO: Check permissions
    return vfs_unmount(target); 
}


int sys_nanosleep(void *req, void *rem) { (void)req; (void)rem; return 0; }

// Current working directory per-process
int sys_chdir(const char *path) {
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

int sys_fchdir(int fd) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f || !f->node) return -1;
    if ((f->node->flags & 0x7) != FS_DIRECTORY) return -1;
    
    current_process->cwd_node = f->node;
    return 0;
}

int sys_getcwd(char *buf, size_t size) {
    if (!buf || size < 2) return -1;
    // TODO: Proper path tracking
    buf[0] = '/'; buf[1] = 0;
    return 0;
}

// sys_proc_info - Get detailed info for a single process
int sys_proc_info(pid_t pid, sys_procinfo_t *info) {
    if (!info) return -1;
    
    // If pid == 0, use current process
    // If pid > 0, find that process
    
    process_t *target = NULL;
    
    if (pid == 0) {
        target = current_process;
    } else {
        // Find process by PID
        for (int i = 0; i < 64; i++) {
            if (processes[i].pid == pid) {
                target = &processes[i];
                break;
            }
        }
    }
    
    if (!target) return -1; // ESRCH in real implementation
    
    // Populate struct
    sys_procinfo_t kinfo;
    memset(&kinfo, 0, sizeof(kinfo));
    
    kinfo.pid = target->pid;
    kinfo.ppid = target->ppid;
    
    // Process group and session IDs
    if (target->p_pgrp) {
        kinfo.pgid = target->p_pgrp->pg_id;
        if (target->p_pgrp->pg_session) {
            kinfo.sid = target->p_pgrp->pg_session->s_sid;
        }
    }
    
    kinfo.uid = target->uid;
    kinfo.gid = target->gid;
    kinfo.state = target->state;
    kinfo.bitness = target->bitness;
    kinfo.start_time = target->start_time;
    // kinfo.user_time = target->rusage.ru_utime.tv_sec; // Simplified
    // kinfo.sys_time = target->rusage.ru_stime.tv_sec;
    
    strncpy(kinfo.name, target->comm, sizeof(kinfo.name)-1);
    
    // Copy to user
    memcpy(info, &kinfo, sizeof(kinfo));
    
    return 0;
}

// sys_proc_list - List all active PIDs
// Returns count of PIDs written, or total count if pids==NULL
int sys_proc_list(pid_t *pids, size_t count) {
    int total_procs = 0;
    
    // First pass: count active processes
    for (int i = 0; i < 64; i++) {
        if (processes[i].pid != -1) {
            total_procs++;
        }
    }
    
    if (!pids || count == 0) {
        return total_procs;
    }
    
    // Second pass: fill buffer
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
    if (!buf || len == 0) return -1;
    
    extern char kernel_hostname[MAXHOSTNAMELEN];
    
    size_t hlen = 0;
    while (kernel_hostname[hlen] && hlen < MAXHOSTNAMELEN - 1) hlen++;
    
    if (len < hlen + 1) {
        // Buffer too small - copy what we can
        memcpy(buf, kernel_hostname, len - 1);
        buf[len - 1] = '\0';
    } else {
        memcpy(buf, kernel_hostname, hlen + 1);
    }
    
    return 0;
}
