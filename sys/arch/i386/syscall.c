#include "syscall.h"
#include "idt.h"
#include "vga.h"
#include "../../kern/sched.h"
#include "../../kern/version.h"
#include "../../exec/perso/personality.h"
#include "../../sys/thr.h"
#include "../../sys/acct.h"
#include "../../sys/file.h"
#include "../../sys/signal.h"
#include "../../vfs/vfs.h"
#include "../../drivers/serial/uart.h"
#include <sys/types.h>

extern thread_t *current_thread; 
extern process_t *current_process;

extern int sys_acct(const char *path);
extern int sys_time(uint32_t *tloc);
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
        vga_write(buf, len);
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
        // Stdin (keyboard?)
        // Stub: return 0 (EOF)
        return 0;
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
    if (path[0] == '/') {
        // Skip leading / for finddir which usually expects name in dir
        // Mock root handling:
        if (path[1] == 0) node = fs_root;
        else node = finddir_fs(fs_root, (char*)path + 1);
    } else {
        node = finddir_fs(fs_root, (char*)path);
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

int sys_lseek(int fd, int off, int w) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    file_t *f = current_process->fds[fd];
    if (!f) return -1;
    
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
    vga_write("Process exited.\n", 16);
    uart_write("Process exited.\n", 16);
    acct_process(code);
    while(1);
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

void syscall_handler(registers_t *regs) {
    if (!current_process || !current_process->pers) return;
    struct personality *p = current_process->pers;
    if (regs->eax >= p->syscall_count) return;
    void *location = p->syscall_table[regs->eax];
    if (!location) return;
    typedef int (*sys_func_t)(uint32_t, uint32_t, uint32_t);
    sys_func_t func = (sys_func_t)location;
    regs->eax = func(regs->ebx, regs->ecx, regs->edx);

    signal_handle_pending(regs);
}

// Local stat def matching userspace
struct stat {
    unsigned long  st_dev;
    unsigned long  st_ino;
    unsigned short st_mode;
    unsigned short st_nlink;
    unsigned short st_uid;
    unsigned short st_gid;
    unsigned long  st_rdev;
    unsigned long  st_size;
    unsigned long  st_blksize;
    unsigned long  st_blocks;
    time_t         st_atime;
    unsigned long  st_atime_nsec;
    time_t         st_mtime;
    unsigned long  st_mtime_nsec;
    time_t         st_ctime;
    unsigned long  st_ctime_nsec;
};

int sys_mkdir(const char *p, int m) { (void)p; (void)m; return 0; }
int sys_rmdir(const char *p) { (void)p; return 0; }
int sys_getuid(void) { return 0; }
int sys_getgid(void) { return 0; }
int sys_geteuid(void) { return 0; }
int sys_getegid(void) { return 0; }
int sys_setuid(int u) { (void)u; return 0; }
int sys_setgid(int g) { (void)g; return 0; }
int sys_clone(uint32_t f, void *s, int *p, void *t, int *c) { (void)f; (void)s; (void)p; (void)t; (void)c; return -1; }
int sys_stat(const char *p, struct stat *buf) { 
    // Need proper VFS stat. Stub for now to allow `test` to work partially?
    (void)p; 
    if(buf) {
        buf->st_mtime = 1000;
        buf->st_mode = 0040777; // Directory
    }
    return 0; 
}
int sys_access(const char *path, int mode) {
    if (!path) return -1;

    fs_node_t *node = 0;
    if (path[0] == '/') {
        if (path[1] == 0) node = fs_root;
        else node = finddir_fs(fs_root, (char*)path + 1);
    } else {
        node = finddir_fs(fs_root, (char*)path);
    }

    if (!node) return -1;

    // F_OK check
    if (mode == F_OK) return 0;

    return vfs_check_permissions(node, current_process->uid, current_process->gid, mode);
}

int sys_sync(void) { return 0; }
int sys_pipe(int *p) { if(p) { p[0]=3; p[1]=4; } return 0; }
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

int sys_getpid(void) { if(current_process) return current_process->pid; return 0; }
int sys_execve(const char *f, char *const a[], char *const e[]) { (void)f; (void)a; (void)e; return -1; }
int sys_fork(void) { return -1; }
int sys_mknod(const char *p, int m, int d) { (void)p; (void)m; (void)d; return 0; }
int sys_mount(const char *s, const char *t, const char *fs, unsigned long f, void *d) { (void)s; (void)t; (void)fs; (void)f; (void)d; return 0; }
int sys_umount(const char *t) { (void)t; return 0; }
int sys_nanosleep(void *req, void *rem) { (void)req; (void)rem; return 0; }
int sys_getcwd(char *buf, size_t size) {
    if(!buf || size < 2) return -1;
    buf[0] = '/'; buf[1] = 0;
    return 0;
}

extern void isr128(void); 
void syscall_init(void) {
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0x8E);
}
