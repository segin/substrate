#include "syscall.h"
#include "idt.h"
#include "../../kern/sched.h"
#include "../../kern/version.h"
#include "../../kern/panic.h"
#include "../../kern/console.h"
#include "../../exec/perso/personality.h"
#include "../../include/sys/thr.h"
#include "../../include/sys/acct.h"
#include "../../include/sys/file.h"
#include "../../include/sys/proc.h"
#include "../../include/sys/signal.h"
#include "../../vfs/vfs.h"
#include "../../drivers/serial/uart.h"
#include <sys/types.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

extern thread_t *current_thread; 
extern process_t *current_process;
extern process_t processes[64];
extern thread_t threads[256];

extern int sys_acct(const char *path);
extern int64_t sys_time(int64_t *tloc);
extern int sys_waitpid(int pid, int *status, int options);
extern int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
extern void signal_handle_pending(registers_t *regs);

// Simple file structure allocator
#define MAX_SYSTEM_FILES 128
static file_t system_files[MAX_SYSTEM_FILES];

file_t *file_alloc(void) {
    for (int i = 0; i < MAX_SYSTEM_FILES; i++) {
        if (system_files[i].ref_count == 0) {
            system_files[i].ref_count = 1;
            return &system_files[i];
        }
    }
    return 0;
}

void file_free(file_t *f) {
    f->ref_count = 0;
}

int sys_write(int fd, const char *buf, int len) {
    if (fd == 1 || fd == 2) {
        console_write(buf, len);
        uart_write(buf, len);
        return len;
    }
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    // VFS write not fully implemented for files yet, stub
    return 0;
}

int sys_read(int fd, char *buf, int len) {
    if (fd == 0) {
        // Stdin - read from console input buffer (keyboard)
        extern void console_read(char *data, size_t len);
        console_read(buf, len);
        return len;
    }
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
    
    // Find free FD
    int fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!current_process->fds[i]) {
            fd = i;
            break;
        }
    }
    if (fd == -1) return -1;

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

int sys_close(int fd) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
    f->ref_count--;
    if (f->ref_count <= 0) {
        close_fs(f->node);
        file_free(f);
    }
    current_process->fds[fd] = 0;
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
    
    struct linux_dirent *ld = (struct linux_dirent*)dirp;
    unsigned int bpos = 0;
    
    // Read one entry
    struct dirent *d = readdir_fs(f->node, f->offset);
    if (!d) return 0;
    
    // Calculate size
    int name_len = 0;
    while(d->name[name_len]) name_len++;
    
    int reclen = sizeof(unsigned long) * 2 + sizeof(unsigned short) + name_len + 1;
    reclen = (reclen + 3) & ~3; // Align
    
    if (bpos + reclen > count) return -1; // Buffer too small for even one
    
    ld->d_ino = d->ino;
    ld->d_off = f->offset + 1;
    ld->d_reclen = reclen;
    for(int i=0; i<name_len; i++) ld->d_name[i] = d->name[i];
    ld->d_name[name_len] = 0;
    
    f->offset++;
    return reclen;
}

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

int sys_uname(struct utsname *buf) {
    if (!buf) return -1;
    char *s = "TestUnix";
    char *n = "localhost";
    char *r = "0.1";
    char *v = "Kernel";
    char *m = "i386";
    
    for(int i=0; s[i]; i++) buf->sysname[i] = s[i]; 
    buf->sysname[8]=0;
    
    for(int i=0; n[i]; i++) buf->nodename[i] = n[i]; 
    buf->nodename[9]=0;
    
    for(int i=0; r[i]; i++) buf->release[i] = r[i]; 
    buf->release[3]=0;
    
    for(int i=0; v[i]; i++) buf->version[i] = v[i]; 
    buf->version[6]=0;
    
    for(int i=0; m[i]; i++) buf->machine[i] = m[i]; 
    buf->machine[4]=0;
    
    return 0;
}

int sys_exit(int code) {
    if (current_process->pid == 1) {
        panic("Attempted to exit init process!");
    }
    
    current_process->exit_code = code;
    acct_process(code);
    
    // Mark all threads of this process as zombie (simplified)
    for (int i = 0; i < 64; i++) {
        if (threads[i].proc == current_process && threads[i].tid != -1) {
            threads[i].state = THREAD_ZOMBIE;
        }
    }
    
    // Wake up parent
    sched_wakeup(&current_process->ppid);
    
    while(1) {
        sched_yield();
    }
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

// Linux struct user_desc for set_thread_area
struct user_desc {
    unsigned int entry_number;
    unsigned int base_addr;
    unsigned int limit;
    unsigned int seg_32bit:1;
    unsigned int contents:2;
    unsigned int read_exec_only:1;
    unsigned int limit_in_pages:1;
    unsigned int seg_not_present:1;
    unsigned int useable:1;
};

// GDT TLS entries (Linux uses entries 6, 7, 8 for TLS)
#define GDT_TLS_ENTRIES 3
#define GDT_TLS_START 6

extern void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

int sys_set_thread_area(struct user_desc *u_info) {
    if (!u_info) return -14; // EFAULT
    
    struct user_desc info;
    memcpy(&info, u_info, sizeof(info));
    
    // If entry_number is -1, allocate a new TLS entry
    if (info.entry_number == (unsigned int)-1) {
        info.entry_number = GDT_TLS_START; // Use first TLS slot
    }
    
    // Validate entry number (TLS entries 6, 7, 8)
    if (info.entry_number < GDT_TLS_START || 
        info.entry_number >= GDT_TLS_START + GDT_TLS_ENTRIES) {
        return -22; // EINVAL
    }
    
    // Set up the GDT entry
    // Access byte: 0xF2 = Present, Ring 3, Data segment, Expand-up, Writable
    uint8_t access = 0xF2;
    // Granularity: 0x40 = 32-bit segment, byte granularity
    uint8_t gran = 0x40;
    
    if (info.limit_in_pages) {
        gran |= 0x80; // Page granularity
    }
    if (!info.seg_32bit) {
        gran &= ~0x40; // 16-bit segment
    }
    if (info.seg_not_present) {
        access &= ~0x80; // Not present
    }
    
    gdt_set_gate(info.entry_number, info.base_addr, info.limit, access, gran);
    
    // Load GS with the new selector (entry_number * 8 | RPL 3)
    uint16_t selector = (info.entry_number << 3) | 3;
    __asm__ volatile("mov %0, %%gs" : : "r"(selector));
    
    // Write back the entry number to user
    u_info->entry_number = info.entry_number;
    
    return 0;
}

// Global pointer to current syscall's register frame (for fork)
registers_t *syscall_regs = NULL;

void syscall_handler(registers_t *regs) {
    if (!current_process || !current_process->pers) {
        regs->eax = -38; // ENOSYS
        return;
    }
    
    // Save regs pointer for special syscalls like fork
    syscall_regs = regs;
    
    struct personality *p = current_process->pers;
    
    // Check if syscall number is out of range
    if (regs->eax >= p->syscall_count) {
        char buf[128];
        sprintf(buf, "syscall: unimplemented #%u (PID=%d, Pers=%s)\n", 
                (unsigned int)regs->eax, current_process->pid, p->name);
        kprint(buf);
        regs->eax = -38; // ENOSYS
        return;
    }
    
    void *location = p->syscall_table[regs->eax];
    
    // Check if syscall handler is NULL (not implemented)
    if (!location) {
        char buf[128];
        sprintf(buf, "syscall: unimplemented #%u (PID=%d, Pers=%s)\n", 
                (unsigned int)regs->eax, current_process->pid, p->name);
        kprint(buf);
        regs->eax = -38; // ENOSYS
        return;
    }
    
    typedef int64_t (*sys_func_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    sys_func_t func = (sys_func_t)location;
    int64_t ret = func(regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi, regs->ebp);
    regs->eax = (uint32_t)(ret & 0xFFFFFFFF);
    regs->edx = (uint32_t)((ret >> 32) & 0xFFFFFFFF);

    signal_handle_pending(regs);
    
    // Check for ESP corruption (Kernel Stack bleeding into User Regs)
    // Only if returning to User Mode (CS=0x1B)
    if (regs->cs == 0x1B && regs->useresp >= 0xC0000000) {
        kprint("SYSCALL RET: Bad User ESP: 0x");
        char hex[16];
        uint32_t val = regs->useresp;
        for(int i=7; i>=0; i--) { int v=(val>>(i*4))&0xF; hex[7-i]=v<10?'0'+v:'A'+v-10; }
        hex[8]=0;
        kprint(hex);
        kprint("\n");
        panic("Syscall returning with Kernel ESP in User Frame");
    }
}

// Local stat def matching userspace
struct stat {
    uint32_t       st_dev;
    uint32_t       st_ino;
    uint16_t       st_mode;
    uint16_t       st_nlink;
    uint16_t       st_uid;
    uint16_t       st_gid;
    uint32_t       st_rdev;
    off_t          st_size;    // 64-bit size
    uint32_t       st_blksize;
    uint32_t       st_pad1;    // padding
    blkcnt_t       st_blocks;  // 64-bit block count
    time_t         st_atime;   // 64-bit time
    uint32_t       st_atime_nsec;
    uint32_t       st_pad2;
    time_t         st_mtime;
    uint32_t       st_mtime_nsec;
    uint32_t       st_pad3;
    time_t         st_ctime;
    uint32_t       st_ctime_nsec;
    uint32_t       st_pad4;
};

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

int sys_mkdir(const char *p, int m) { (void)p; (void)m; return 0; }
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
    // Same as stat for now - symlinks are auto-resolved
    // TODO: Implement no-follow version
    return sys_stat(path, buf);
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
    for (int i = 0; i < MAX_FD; i++) {
        if (!current_process->fds[i]) {
            if (f1 == -1) f1 = i;
            else if (f2 == -1) { f2 = i; break; }
        }
    }

    if (f1 == -1 || f2 == -1) return -1;

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

    // Find free FD
    int newfd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!current_process->fds[i]) {
            newfd = i;
            break;
        }
    }
    if (newfd == -1) return -1;

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

int sys_fork(void) {
    // Fork needs access to the current syscall's register frame
    extern registers_t *syscall_regs;
    // In a real OS, fork() returns 0 in the child and the child's PID in the parent.
    return sched_fork_process(current_process, syscall_regs);
}

int sys_vfork(void) {
    // vfork: child shares parent's address space, parent blocks until child exec/exit
    // For now, implement as regular fork (simpler, same behavior but less efficient)
    // A proper implementation would:
    // 1. Not duplicate page tables
    // 2. Mark parent as VFORK_WAITING
    // 3. Only run child until it execs or exits
    // 4. Resume parent after child's exec/exit
    
    // Simple implementation: just call fork
    extern registers_t *syscall_regs;
    return sched_fork_process(current_process, syscall_regs);
}

int sys_mknod(const char *p, int m, int d) { (void)p; (void)m; (void)d; return 0; }

int sys_mount(const char *source, const char *target, const char *fstype, unsigned long flags, void *data) {
    if (!target || !fstype) return -1;
    return vfs_mount(source, target, fstype, flags, data);
}

int sys_umount(const char *target) { 
    (void)target; 
    // TODO: Implement unmount
    return 0; 
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

extern void isr128(void); 
void syscall_init(void) {
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0x8E);
}
