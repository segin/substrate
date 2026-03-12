#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <sys/errno.h>
#include <sys/proc.h>
#include <pm/pm.h>
#include <sys/ldt.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/file.h>
#include <sys/poll.h>
#include <sys/termios.h>
#include <sys/time.h>
#include <sys/times.h>
#include <kern/cmdline.h>
#include <arch/i386/idt.h>
#include <exec/formats/elks_aout.h>
#include <vfs/vfs.h>

process_t *current_process;
thread_t *current_thread;
process_t processes[MAX_PROCS];
struct mountlist mountlist;

static const char *last_name;
static uintptr_t last_ptr;
static uintptr_t last_ptr2;
static uintptr_t last_ptr3;
static int last_i0;
static int last_i1;
static char last_log[128];
static char last_exec_path[128];
static char last_exec_argv0[128];
static char last_exec_argv1[128];
static char last_exec_env0[128];
static char last_path_arg[128];
static int last_sigaction_sig;
static sig_t last_sigaction_handler;
static int last_kill_pid;
static int last_kill_sig;
static int last_sigexit_sig;
static time_t stub_clock_sec = 123456789;
static suseconds_t stub_clock_usec = 654321;
static int stub_tz_minuteswest;
static int stub_tz_dsttime;
static struct sigaction stub_oldact;
static uint8_t *ds_mem;
static size_t ds_mem_size;
static size_t stub_vm_phys_free_pages = 512;
static size_t stub_vm_phys_used_pages = 256;
static struct pollfd last_poll_fds[32];
static unsigned int last_poll_nfds;
static int last_poll_timeout;
typedef int32_t elks_time_t;

#define ELKS_TEST_NCCS 17
#define ELKS_TEST_TERMIOS_BYTES offsetof(struct termios, c_cc[ELKS_TEST_NCCS])

#define sys_exit   stub_sys_exit
#define sys_fork   stub_sys_fork
#define sys_vfork  stub_sys_vfork
#define sys_read   stub_sys_read
#define sys_write  stub_sys_write
#define sys_open   stub_sys_open
#define sys_close  stub_sys_close
#define kern_open  stub_kern_open
#define kern_waitpid stub_kern_waitpid
#define sys_creat  stub_sys_creat
#define sys_link   stub_sys_link
#define sys_unlink stub_sys_unlink
#define kern_unlink stub_kern_unlink
#define sys_execve stub_sys_execve
#define sys_chdir  stub_sys_chdir
#define sys_time   stub_sys_time
#define sys_mknod  stub_sys_mknod
#define sys_chmod  stub_sys_chmod
#define sys_lchown stub_sys_lchown
#define sys_lseek  stub_sys_lseek
#define sys_getpid stub_sys_getpid
#define sys_geteuid stub_sys_geteuid
#define sys_mount  stub_sys_mount
#define sys_umount stub_sys_umount
#define sys_setuid stub_sys_setuid
#define sys_getuid stub_sys_getuid
#define sys_stime  stub_sys_stime
#define sys_alarm  stub_sys_alarm
#define sys_fstat  stub_sys_fstat
#define sys_pause  stub_sys_pause
#define sys_access stub_sys_access
#define sys_sync   stub_sys_sync
#define sys_kill   stub_sys_kill
#define sys_mkdir  stub_sys_mkdir
#define sys_rmdir  stub_sys_rmdir
#define sys_dup    stub_sys_dup
#define sys_pipe   stub_sys_pipe
#define sys_times  stub_sys_times
#define sys_brk    stub_sys_brk
#define sys_setgid stub_sys_setgid
#define sys_getgid stub_sys_getgid
#define sys_getegid stub_sys_getegid
#define sys_signal stub_sys_signal
#define sys_ioctl  stub_sys_ioctl
#define kern_ioctl stub_kern_ioctl
#define sys_fcntl  stub_sys_fcntl
#define sys_umask  stub_sys_umask
#define sys_stat   stub_sys_stat
#define sys_dup2   stub_sys_dup2
#define sys_getppid stub_sys_getppid
#define sys_getpgrp stub_sys_getpgrp
#define kern_stat  stub_kern_stat
#define kern_lstat stub_kern_lstat
#define kern_fstat stub_kern_fstat
#define kern_readlink stub_kern_readlink
#define kern_sigaction stub_kern_sigaction
#define kern_poll stub_kern_poll
#define kern_gettimeofday stub_kern_gettimeofday
#define kern_stime stub_kern_stime
#define kern_uname stub_kern_uname
#define sigexit stub_sigexit
#define trapsignal stub_trapsignal
#define kern_execve stub_kern_execve
#define kprint      stub_kprint
#define get_ticks   stub_get_ticks

int stub_sys_exit(int a) { last_name = "exit"; last_i0 = a; return 11; }
int stub_sys_fork(void) { last_name = "fork"; return 12; }
int stub_sys_vfork(void) { last_name = "vfork"; return 13; }
int stub_sys_read(int a, char *b, int c) { last_name = "read"; last_i0 = a; last_ptr = (uintptr_t)b; last_i1 = c; return 22; }
int stub_sys_write(int a, const char *b, int c) { last_name = "write"; last_i0 = a; last_ptr = (uintptr_t)b; last_i1 = c; return 33; }
int stub_sys_open(const char *a, int b, int c) { last_name = "open"; last_ptr = (uintptr_t)a; last_i0 = b; last_i1 = c; return 44; }
int stub_sys_close(int a) { last_name = "close"; last_i0 = a; return 55; }
int stub_kern_open(const char *a, int b, int c) {
    strncpy(last_path_arg, a ? a : "", sizeof(last_path_arg) - 1);
    last_path_arg[sizeof(last_path_arg) - 1] = '\0';
    return stub_sys_open(a, b, c);
}
int stub_kern_waitpid(int a, int *b, int c) {
    last_name = "waitpid";
    last_i0 = a;
    last_ptr = (uintptr_t)b;
    last_i1 = c;
    if (b) {
        *b = 0x1234;
    }
    return 66;
}
int stub_sys_creat(const char *a, int b) { (void)a; (void)b; return -1; }
int stub_sys_link(const char *a, const char *b) {
    last_name = "link";
    last_ptr = (uintptr_t)a;
    last_ptr2 = (uintptr_t)b;
    return 46;
}
int stub_sys_unlink(const char *a) { (void)a; return -1; }
int stub_kern_unlink(const char *a) {
    strncpy(last_path_arg, a ? a : "", sizeof(last_path_arg) - 1);
    last_path_arg[sizeof(last_path_arg) - 1] = '\0';
    return stub_sys_unlink(a);
}
int stub_sys_execve(const char *a, char **b, char **c) { (void)a; (void)b; (void)c; return -1; }
int stub_sys_chdir(const char *a) {
    last_name = "chdir";
    last_ptr = (uintptr_t)a;
    return 47;
}
time_t stub_sys_time(time_t *a) {
    last_name = "time";
    last_ptr = (uintptr_t)a;
    if (a) {
        *a = stub_clock_sec;
    }
    return stub_clock_sec;
}
int stub_sys_mknod(const char *a, int b, int c) {
    last_name = "mknod";
    last_ptr = (uintptr_t)a;
    last_i0 = b;
    last_i1 = c;
    return 48;
}
int stub_sys_chmod(const char *a, int b) {
    last_name = "chmod";
    last_ptr = (uintptr_t)a;
    last_i0 = b;
    return 49;
}
int stub_sys_lchown(const char *a, int b, int c) {
    last_name = "chown";
    last_ptr = (uintptr_t)a;
    last_i0 = b;
    last_i1 = c;
    return 50;
}
int64_t stub_sys_lseek(int a, uint32_t b, uint32_t c, int d) { (void)a; (void)b; (void)c; (void)d; return -1; }
int stub_sys_getpid(void) { return 321; }
int stub_sys_mount(const char *a, const char *b, const char *c, unsigned long d, void *e) {
    last_name = "mount";
    last_ptr = (uintptr_t)a;
    last_ptr2 = (uintptr_t)b;
    last_ptr3 = (uintptr_t)c;
    last_i0 = (int)d;
    last_i1 = (int)(uintptr_t)e;
    return 51;
}
int stub_sys_umount(const char *a) {
    last_name = "umount";
    last_ptr = (uintptr_t)a;
    return 52;
}
int stub_sys_setuid(int a) { (void)a; return -1; }
int stub_sys_getuid(void) { return 123; }
int stub_sys_geteuid(void) { return 124; }
int stub_sys_stime(time_t *a) {
    last_name = "stime";
    last_ptr = (uintptr_t)a;
    if (a) {
        stub_clock_sec = *a;
    }
    return 53;
}
unsigned int stub_sys_alarm(unsigned int a) { (void)a; return 0; }
int stub_sys_fstat(int a, void *b) { (void)a; (void)b; return -1; }
int stub_sys_pause(void) { return -1; }
int stub_sys_access(const char *a, int b) {
    last_name = "access";
    last_ptr = (uintptr_t)a;
    last_i0 = b;
    return 54;
}
int stub_sys_sync(void) { return 0; }
int stub_sys_kill(int a, int b) { last_kill_pid = a; last_kill_sig = b; return 99; }
int stub_sys_mkdir(const char *a, int b) {
    last_name = "mkdir";
    last_ptr = (uintptr_t)a;
    last_i0 = b;
    return 55;
}
int stub_sys_rmdir(const char *a) {
    last_name = "rmdir";
    last_ptr = (uintptr_t)a;
    return 56;
}
int stub_sys_dup(int a) { (void)a; return -1; }
int stub_sys_pipe(int *a) { (void)a; return -1; }
clock_t stub_sys_times(struct tms *a) {
    last_name = "times";
    last_ptr = (uintptr_t)a;
    if (a) {
        a->tms_utime = 1;
        a->tms_stime = 2;
        a->tms_cutime = 3;
        a->tms_cstime = 4;
    }
    return 57;
}
int stub_sys_brk(uint32_t a) {
    last_name = "brk";
    last_ptr = a;
    if (current_process) {
        if (a >= current_process->brk_start) {
            current_process->brk = a;
        }
        return (int)current_process->brk;
    }
    return (int)a;
}
int stub_sys_setgid(int a) { (void)a; return -1; }
int stub_sys_getgid(void) { return 223; }
int stub_sys_getegid(void) { return 224; }
int stub_sys_signal(int a, void *b) { (void)a; (void)b; return -1; }
int stub_sys_ioctl(int a, uint32_t b, void *c) { (void)a; (void)b; (void)c; return -1; }
int stub_kern_ioctl(int a, uint32_t b, void *c) {
    last_name = "ioctl";
    last_i0 = a;
    last_i1 = (int)b;
    last_ptr = (uintptr_t)c;

    if (b == TCGETS && c) {
        struct termios *t = (struct termios *)c;

        memset(t, 0, sizeof(*t));
        t->c_iflag = ICRNL;
        t->c_oflag = ONLCR;
        t->c_cflag = B9600 | CS8 | CREAD;
        t->c_lflag = ISIG | ICANON | ECHO;
        t->c_line = 7;
        t->c_cc[VINTR] = 3;
        t->c_cc[VEOF] = 4;
    } else if (b == TIOCGWINSZ && c) {
        struct winsize *ws = (struct winsize *)c;

        ws->ws_row = 24;
        ws->ws_col = 80;
        ws->ws_xpixel = 0;
        ws->ws_ypixel = 0;
    }
    return 0;
}
int stub_sys_fcntl(int a, int b, int c) { (void)a; (void)b; (void)c; return -1; }
int stub_sys_umask(int a) { (void)a; return -1; }
int stub_sys_stat(const char *a, void *b) { (void)a; (void)b; return -1; }
int stub_sys_dup2(int a, int b) { (void)a; (void)b; return -1; }
int stub_sys_getppid(void) { return 654; }
int stub_sys_getpgrp(void) { return -1; }
int stub_kern_sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
    last_sigaction_sig = sig;
    last_sigaction_handler = act ? act->sa_handler : SIG_ERR;
    if (oact) {
        *oact = stub_oldact;
    }
    return 0;
}
int stub_kern_poll(struct pollfd *fds, unsigned int nfds, int timeout) {
    unsigned int i;

    last_poll_nfds = nfds;
    last_poll_timeout = timeout;
    memset(last_poll_fds, 0, sizeof(last_poll_fds));
    for (i = 0; i < nfds && i < 32U; i++) {
        last_poll_fds[i] = fds[i];
        fds[i].revents = fds[i].events;
    }
    return (int)nfds;
}
int stub_kern_gettimeofday(struct timeval *tv, struct timezone *tz) {
    if (tv) {
        tv->tv_sec = stub_clock_sec;
        tv->tv_usec = stub_clock_usec;
    }
    if (tz) {
        tz->tz_minuteswest = stub_tz_minuteswest;
        tz->tz_dsttime = stub_tz_dsttime;
    }
    return 0;
}
int stub_kern_stime(time_t *t) {
    if (!t) {
        return -1;
    }
    stub_clock_sec = *t;
    return 0;
}
int stub_kern_uname(struct utsname *buf) {
    if (!buf) {
        return -1;
    }
    memset(buf, 0, sizeof(*buf));
    strcpy(buf->sysname, "Substrate");
    strcpy(buf->nodename, "substrate-host");
    strcpy(buf->release, "0.1");
    strcpy(buf->version, "Kernel");
    strcpy(buf->machine, "i386");
    return 0;
}
void stub_sigexit(process_t *p, int sig) {
    (void)p;
    last_sigexit_sig = sig;
}
void stub_trapsignal(process_t *p, int sig, int code) {
    (void)p;
    (void)code;
    last_sigexit_sig = sig;
}
int stub_kern_execve(const char *path, char *const argv[], char *const envp[]) {
    last_name = "execve";
    strncpy(last_exec_path, path ? path : "", sizeof(last_exec_path) - 1);
    last_exec_path[sizeof(last_exec_path) - 1] = '\0';
    strncpy(last_exec_argv0, (argv && argv[0]) ? argv[0] : "", sizeof(last_exec_argv0) - 1);
    last_exec_argv0[sizeof(last_exec_argv0) - 1] = '\0';
    strncpy(last_exec_argv1, (argv && argv[1]) ? argv[1] : "", sizeof(last_exec_argv1) - 1);
    last_exec_argv1[sizeof(last_exec_argv1) - 1] = '\0';
    strncpy(last_exec_env0, (envp && envp[0]) ? envp[0] : "", sizeof(last_exec_env0) - 1);
    last_exec_env0[sizeof(last_exec_env0) - 1] = '\0';
    return 77;
}
void stub_kprint(const char *msg) {
    strncpy(last_log, msg ? msg : "", sizeof(last_log) - 1);
    last_log[sizeof(last_log) - 1] = '\0';
}
uint64_t stub_get_ticks(void) { return 0x12345678ULL; }
void core_capture_trapframe(process_t *p, const registers_t *regs) { (void)p; (void)regs; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }
size_t vm_phys_get_free(void) { return stub_vm_phys_free_pages; }
size_t vm_phys_get_used(void) { return stub_vm_phys_used_pages; }
void proc_capture_cmdline(process_t *p, char *const argv[]) {
    size_t used = 0;
    int i;

    if (!p) {
        return;
    }
    p->cmdline_tail_len = 0;
    p->cmdline_tail_argc = 0;
    memset(p->cmdline_tail, 0, sizeof(p->cmdline_tail));
    if (!argv) {
        return;
    }
    for (i = 1; argv[i] != NULL && used < sizeof(p->cmdline_tail); i++) {
        size_t avail = sizeof(p->cmdline_tail) - used;
        size_t copy_len;

        if (avail <= 1U) {
            break;
        }
        copy_len = strnlen(argv[i], avail - 1U);
        memcpy(p->cmdline_tail + used, argv[i], copy_len);
        p->cmdline_tail[used + copy_len] = '\0';
        used += copy_len + 1U;
        p->cmdline_tail_len = (uint16_t)used;
        p->cmdline_tail_argc++;
        if (argv[i][copy_len] != '\0') {
            break;
        }
    }
}
size_t proc_emit_cmdline(const process_t *p, char *buf, size_t buf_len, size_t *argc_out) {
    size_t limit;
    size_t written;
    size_t argc;
    size_t cursor;
    const char *name = (p && p->comm[0] != '\0') ? p->comm : "unknown";
    size_t name_len;

    if (argc_out) {
        *argc_out = 0;
    }
    if (!p || !buf || buf_len == 0) {
        return 0;
    }
    limit = buf_len > PROC_CMDLINE_MAX ? PROC_CMDLINE_MAX : buf_len;
    name_len = strnlen(name, limit - 1U);
    memcpy(buf, name, name_len);
    buf[name_len] = '\0';
    written = name_len + 1U;
    argc = 1U;
    cursor = 0;
    while (cursor < p->cmdline_tail_len && written < limit) {
        size_t avail = limit - written;
        size_t stored_len;
        size_t copy_len;

        if (avail <= 1U) {
            break;
        }
        stored_len = strnlen(p->cmdline_tail + cursor, (size_t)p->cmdline_tail_len - cursor);
        copy_len = stored_len > (avail - 1U) ? (avail - 1U) : stored_len;
        memcpy(buf + written, p->cmdline_tail + cursor, copy_len);
        buf[written + copy_len] = '\0';
        written += copy_len + 1U;
        argc++;
        if (copy_len != stored_len) {
            break;
        }
        cursor += stored_len + 1U;
    }
    if (argc_out) {
        *argc_out = argc;
    }
    return written;
}

#include "../../sys/exec/perso/perso_elks.c"

static file_t stub_file;
static file_t stub_kmem_file;
static fs_node_t stub_dir_node;
static fs_node_t stub_kmem_node;
static struct dirent stub_dirent;

int stub_kern_stat(const char *path, struct stat *buf) {
    (void)path;
    memset(buf, 0, sizeof(*buf));
    buf->st_dev = 5;
    buf->st_ino = 0x11223344U;
    buf->st_mode = 0120644;
    buf->st_nlink = 2;
    buf->st_uid = 10;
    buf->st_gid = 11;
    buf->st_rdev = 7;
    buf->st_size = 1234;
    buf->st_atime = 1;
    buf->st_mtime = 2;
    buf->st_ctime = 3;
    return 0;
}

int stub_kern_lstat(const char *path, struct stat *buf) {
    return stub_kern_stat(path, buf);
}

int stub_kern_fstat(int fd, struct stat *buf) {
    (void)fd;
    return stub_kern_stat(NULL, buf);
}

int stub_kern_readlink(const char *pathname, char *buf, size_t bufsiz) {
    static const char target[] = "/target";
    size_t len = sizeof(target) - 1U;

    (void)pathname;
    if (bufsiz < len) {
        len = bufsiz;
    }
    memcpy(buf, target, len);
    return (int)len;
}

int kern_pipe(int *fds) {
    last_name = "pipe";
    if (fds) {
        fds[0] = 12;
        fds[1] = 34;
    }
    return 0;
}

struct dirent *readdir_fs(fs_node_t *node, uint64_t index) {
    (void)node;
    if (index != 0) {
        return NULL;
    }
    memset(&stub_dirent, 0, sizeof(stub_dirent));
    stub_dirent.d_ino = 0xAABBCCDDU;
    stub_dirent.d_namlen = 4;
    strcpy(stub_dirent.d_name, "init");
    return &stub_dirent;
}

static void setup_ds(process_t *proc, gdt_entry_t *ldt, size_t size) {
    uint32_t base;

    memset(proc, 0, sizeof(*proc));
    memset(ldt, 0, sizeof(gdt_entry_t) * 4);
    memset(ds_mem, 0, size);

    base = (uint32_t)(uintptr_t)ds_mem;

    ldt[ELKS_LDT_DS_INDEX].limit_low = (uint16_t)(size - 1U);
    ldt[ELKS_LDT_DS_INDEX].base_low = (uint16_t)(base & 0xFFFFU);
    ldt[ELKS_LDT_DS_INDEX].base_middle = (uint8_t)((base >> 16) & 0xFFU);
    ldt[ELKS_LDT_DS_INDEX].base_high = (uint8_t)((base >> 24) & 0xFFU);
    ldt[ELKS_LDT_DS_INDEX].access = 0xF2U;
    ldt[ELKS_LDT_DS_INDEX].granularity = 0x00U;

    ldt[ELKS_LDT_CS_INDEX].limit_low = (uint16_t)(size - 1U);
    ldt[ELKS_LDT_CS_INDEX].base_low = (uint16_t)(base & 0xFFFFU);
    ldt[ELKS_LDT_CS_INDEX].base_middle = (uint8_t)((base >> 16) & 0xFFU);
    ldt[ELKS_LDT_CS_INDEX].base_high = (uint8_t)((base >> 24) & 0xFFU);
    ldt[ELKS_LDT_CS_INDEX].access = 0xFAU;
    ldt[ELKS_LDT_CS_INDEX].granularity = 0x00U;

    ldt[ELKS_LDT_SS_INDEX].limit_low = (uint16_t)(size - 1U);
    ldt[ELKS_LDT_SS_INDEX].base_low = (uint16_t)(base & 0xFFFFU);
    ldt[ELKS_LDT_SS_INDEX].base_middle = (uint8_t)((base >> 16) & 0xFFU);
    ldt[ELKS_LDT_SS_INDEX].base_high = (uint8_t)((base >> 24) & 0xFFU);
    ldt[ELKS_LDT_SS_INDEX].access = 0xF2U;
    ldt[ELKS_LDT_SS_INDEX].granularity = 0x00U;

    proc->ldt = ldt;
    proc->ldt_entry_count = 4;
    proc->brk_start = base + 0x200U;
    proc->brk = proc->brk_start;
}

int main(void) {
    process_t proc;
    thread_t thread;
    gdt_entry_t ldt[4];
    struct mount root_mount;
    struct mount dev_mount;
    char long_arg[PROC_CMDLINE_MAX + 32];
    char *kmem_argv[] = { (char *)"ps", (char *)"-l", long_arg, NULL };
    int (*fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    uintptr_t ds_base;
    uint16_t cs_sel = (uint16_t)((ELKS_LDT_CS_INDEX << 3) | 4U | 3U);
    uint16_t ss_sel = (uint16_t)((ELKS_LDT_SS_INDEX << 3) | 4U | 3U);

    ds_mem_size = 4096;
#ifdef MAP_32BIT
    ds_mem = mmap(NULL, ds_mem_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
#else
    ds_mem = mmap(NULL, ds_mem_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    if (ds_mem == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    memset(&thread, 0, sizeof(thread));
    memset(processes, 0, sizeof(processes));
    memset(&root_mount, 0, sizeof(root_mount));
    memset(&dev_mount, 0, sizeof(dev_mount));
    for (int i = 0; i < MAX_PROCS; i++) {
        processes[i].pid = -1;
    }
    setup_ds(&proc, ldt, ds_mem_size);
    current_process = &proc;
    current_thread = &thread;
    elks_personality_init();
    TAILQ_INIT(&mountlist);
    strcpy(root_mount.mnt_stat.f_fstypename, "ext2");
    strcpy(root_mount.mnt_stat.f_mntonname, "/");
    root_mount.mnt_stat.f_blocks = 4096;
    root_mount.mnt_stat.f_bfree = 1024;
    root_mount.mnt_stat.f_bavail = 1000;
    root_mount.mnt_stat.f_files = 256;
    root_mount.mnt_stat.f_ffree = 128;
    TAILQ_INSERT_TAIL(&mountlist, &root_mount, mnt_list);
    strcpy(dev_mount.mnt_stat.f_fstypename, "devfs");
    strcpy(dev_mount.mnt_stat.f_mntonname, "/dev");
    dev_mount.mnt_flag = MNT_RDONLY;
    TAILQ_INSERT_TAIL(&mountlist, &dev_mount, mnt_list);
    ds_base = (uintptr_t)ds_mem;
    memset(&stub_file, 0, sizeof(stub_file));
    memset(&stub_kmem_file, 0, sizeof(stub_kmem_file));
    memset(&stub_dir_node, 0, sizeof(stub_dir_node));
    memset(&stub_kmem_node, 0, sizeof(stub_kmem_node));
    stub_dir_node.readdir = readdir_fs;
    stub_kmem_node.flags = FS_CHARDEVICE;
    stub_kmem_node.rdev = (1U << 8) | 2U;
    stub_file.f_data = &stub_dir_node;
    stub_kmem_file.f_data = &stub_kmem_node;
    proc.fds[3] = &stub_file;
    proc.fds[4] = &stub_kmem_file;
    strcpy(proc.comm, "ps");
    strcpy(proc.exec_path, "/perso/elks/bin/ps");
    memset(long_arg, 'x', sizeof(long_arg) - 1U);
    long_arg[sizeof(long_arg) - 1U] = '\0';
    proc_capture_cmdline(&proc, kmem_argv);
    proc.pid = 7;
    proc.ppid = 1;
    proc.uid = 42;
    proc.state = SRUN;
    strcpy(processes[0].comm, "swapper");
    processes[0].pid = 0;
    processes[0].ppid = 0;
    processes[0].uid = 0;
    processes[0].state = SRUN;
    processes[0].is_kernel_task = 1;
    processes[1] = proc;
    processes[1].fds[3] = &stub_file;
    processes[1].fds[4] = &stub_kmem_file;
    strcpy(processes[2].comm, "kinit");
    processes[2].pid = 2;
    processes[2].ppid = 0;
    processes[2].uid = 0;
    processes[2].state = SSLEEP;
    current_process = &processes[1];
    current_thread->proc = &processes[1];

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_exit];
    if (fn(7, 0, 0, 0, 0, 0, 0, 0) != 11 || strcmp(last_name, "exit") != 0 || last_i0 != 7) {
        fprintf(stderr, "FAIL: ELKS exit wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_read];
    if (fn(3, 0x20, 5, 0, 0, 0, 0, 0) != 22 || strcmp(last_name, "read") != 0 ||
        last_i0 != 3 || last_ptr != ds_base + 0x20U || last_i1 != 5) {
        fprintf(stderr, "FAIL: ELKS read wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_write];
    if (fn(4, 0x20, 5, 0, 0, 0, 0, 0) != 33 || strcmp(last_name, "write") != 0 ||
        last_i0 != 4 || last_ptr != ds_base + 0x20U || last_i1 != 5) {
        fprintf(stderr, "FAIL: ELKS write wrapper wrong\n");
        return 1;
    }

    memset(ds_mem + 0x100, 0, 64);
    memset(ds_mem + 0x100, 0, ELKS_TEST_TERMIOS_BYTES + 1U);
    ds_mem[0x100 + ELKS_TEST_TERMIOS_BYTES] = 0xA5;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_ioctl];
    if (fn(1, TCGETS, 0x100, 0, 0, 0, 0, 0) != 0 || strcmp(last_name, "ioctl") != 0 ||
        last_i0 != 1 || last_i1 != TCGETS) {
        fprintf(stderr, "FAIL: ELKS ioctl TCGETS wrapper wrong\n");
        return 1;
    }
    {
        struct termios *t = (struct termios *)(void *)(ds_mem + 0x100);

        if (t->c_iflag != ICRNL || t->c_oflag != ONLCR ||
            t->c_cflag != (B9600 | CS8 | CREAD) ||
            t->c_lflag != (ISIG | ICANON | ECHO) ||
            t->c_line != 7 || t->c_cc[VINTR] != 3 || t->c_cc[VEOF] != 4) {
            fprintf(stderr, "FAIL: ELKS ioctl TCGETS translation wrong\n");
            return 1;
        }
        if (ds_mem[0x100 + ELKS_TEST_TERMIOS_BYTES] != 0xA5) {
            fprintf(stderr, "FAIL: ELKS ioctl TCGETS overflowed caller buffer\n");
            return 1;
        }
    }

    memset(ds_mem + 0x140, 0, sizeof(struct winsize));
    if (fn(1, TIOCGWINSZ, 0x140, 0, 0, 0, 0, 0) != 0 || strcmp(last_name, "ioctl") != 0 ||
        last_i0 != 1 || last_i1 != TIOCGWINSZ || last_ptr != ds_base + 0x140U) {
        fprintf(stderr, "FAIL: ELKS ioctl winsize wrapper wrong\n");
        return 1;
    }
    {
        struct winsize *ws = (struct winsize *)(void *)(ds_mem + 0x140);

        if (ws->ws_row != 24 || ws->ws_col != 80) {
            fprintf(stderr, "FAIL: ELKS ioctl winsize translation wrong\n");
            return 1;
        }
    }

    *(uint32_t *)(void *)(ds_mem + 0x168) = (1U << 0) | (1U << 2);
    *(uint32_t *)(void *)(ds_mem + 0x16C) = (1U << 1);
    *(uint32_t *)(void *)(ds_mem + 0x170) = (1U << 2);
    *(int32_t *)(void *)(ds_mem + 0x174) = 1;
    *(int32_t *)(void *)(ds_mem + 0x178) = 500000;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_select];
    if (fn(3, 0x168, 0x16C, 0x170, 0x174, 0, 0, 0) != 3 ||
        last_poll_nfds != 3U || last_poll_timeout != 1500) {
        fprintf(stderr, "FAIL: ELKS select wrapper count/timeout wrong\n");
        return 1;
    }
    if (last_poll_fds[0].fd != 0 || last_poll_fds[0].events != POLLIN ||
        last_poll_fds[1].fd != 1 || last_poll_fds[1].events != POLLOUT ||
        last_poll_fds[2].fd != 2 || last_poll_fds[2].events != (POLLIN | POLLPRI)) {
        fprintf(stderr, "FAIL: ELKS select wrapper poll translation wrong\n");
        return 1;
    }
    if (*(uint32_t *)(void *)(ds_mem + 0x168) != ((1U << 0) | (1U << 2)) ||
        *(uint32_t *)(void *)(ds_mem + 0x16C) != (1U << 1) ||
        *(uint32_t *)(void *)(ds_mem + 0x170) != (1U << 2)) {
        fprintf(stderr, "FAIL: ELKS select wrapper fdset copyout wrong\n");
        return 1;
    }
    *(int32_t *)(void *)(ds_mem + 0x174) = 0;
    *(int32_t *)(void *)(ds_mem + 0x178) = 1000000;
    if (fn(3, 0x168, 0x16C, 0x170, 0x174, 0, 0, 0) != -EINVAL) {
        fprintf(stderr, "FAIL: ELKS select invalid timeout not rejected\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_ioctl];
    memset(ds_mem + 0x150, 0, 16);
    if (fn(4, ELKS_MEM_GETDS, 0x150, 0, 0, 0, 0, 0) != 0 ||
        *(uint16_t *)(void *)(ds_mem + 0x150) != 0) {
        fprintf(stderr, "FAIL: ELKS MEM_GETDS emulation wrong\n");
        return 1;
    }
    memset(ds_mem + 0x152, 0, 16);
    if (fn(4, ELKS_MEM_GETMAXTASKS, 0x152, 0, 0, 0, 0, 0) != 0 ||
        *(uint16_t *)(void *)(ds_mem + 0x152) != MAX_PROCS) {
        fprintf(stderr, "FAIL: ELKS MEM_GETMAXTASKS emulation wrong\n");
        return 1;
    }
    memset(ds_mem + 0x154, 0, 16);
    if (fn(4, ELKS_MEM_GETTASK, 0x154, 0, 0, 0, 0, 0) != 0 ||
        *(uint16_t *)(void *)(ds_mem + 0x154) != ELKS_KMEM_TASKS_OFFSET) {
        fprintf(stderr, "FAIL: ELKS MEM_GETTASK emulation wrong\n");
        return 1;
    }
    memset(ds_mem + 0x156, 0, 16);
    if (fn(4, ELKS_MEM_GETSEGALL, 0x156, 0, 0, 0, 0, 0) != 0 ||
        *(uint16_t *)(void *)(ds_mem + 0x156) != ELKS_KMEM_SEGALL_OFFSET) {
        fprintf(stderr, "FAIL: ELKS MEM_GETSEGALL emulation wrong\n");
        return 1;
    }
    memset(ds_mem + 0x158, 0, 16);
    if (fn(4, ELKS_MEM_GETHEAP, 0x158, 0, 0, 0, 0, 0) != 0 ||
        *(uint16_t *)(void *)(ds_mem + 0x158) != ELKS_KMEM_HEAPALL_OFFSET) {
        fprintf(stderr, "FAIL: ELKS MEM_GETHEAP emulation wrong\n");
        return 1;
    }
    memset(ds_mem + 0x170, 0, sizeof(struct elks_mem_usage));
    if (fn(4, ELKS_MEM_GETUSAGE, 0x170, 0, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "FAIL: ELKS MEM_GETUSAGE emulation wrong\n");
        return 1;
    }
    {
        struct elks_mem_usage *mu = (struct elks_mem_usage *)(void *)(ds_mem + 0x170);

        if (mu->main_free != 2048U || mu->main_used != 1024U ||
            mu->xms_free != 0U || mu->xms_used != 0U) {
            fprintf(stderr, "FAIL: ELKS MEM_GETUSAGE values wrong\n");
            return 1;
        }
    }
    memset(ds_mem + 0x15a, 0xFF, 16);
    if (fn(4, ELKS_MEM_GETUPTIME, 0x15a, 0, 0, 0, 0, 0) != 0 ||
        *(uint16_t *)(void *)(ds_mem + 0x15a) != ELKS_KMEM_JIFFIES_OFFSET) {
        fprintf(stderr, "FAIL: ELKS MEM_GETUPTIME emulation wrong\n");
        return 1;
    }
    memset(ds_mem + 0x15c, 0xFF, 16);
    if (fn(4, ELKS_MEM_GETJIFFADDR, 0x15c, 0, 0, 0, 0, 0) != 0 ||
        *(uint16_t *)(void *)(ds_mem + 0x15c) != ELKS_KMEM_JIFFIES_OFFSET) {
        fprintf(stderr, "FAIL: ELKS MEM_GETJIFFADDR emulation wrong\n");
        return 1;
    }
    *(int32_t *)(void *)(ds_mem + 0x160) = (int32_t)ELKS_KMEM_JIFFIES_OFFSET;
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_lseek])(4, 0x160, 0, 0, 0, 0, 0, 0) != 0 ||
        *(int32_t *)(void *)(ds_mem + 0x160) != (int32_t)ELKS_KMEM_JIFFIES_OFFSET) {
        fprintf(stderr, "FAIL: ELKS kmem jiffies lseek emulation wrong\n");
        return 1;
    }
    memset(ds_mem + 0x1a0, 0, 8);
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_read])(4, 0x1a0, 4, 0, 0, 0, 0, 0) != 4 ||
        *(uint32_t *)(void *)(ds_mem + 0x1a0) != (uint32_t)stub_get_ticks()) {
        fprintf(stderr, "FAIL: ELKS kmem jiffies image wrong\n");
        return 1;
    }
    *(int32_t *)(void *)(ds_mem + 0x162) = (int32_t)ELKS_KMEM_SEGALL_OFFSET;
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_lseek])(4, 0x162, 0, 0, 0, 0, 0, 0) != 0 ||
        *(int32_t *)(void *)(ds_mem + 0x162) != (int32_t)ELKS_KMEM_SEGALL_OFFSET) {
        fprintf(stderr, "FAIL: ELKS kmem segall lseek wrong\n");
        return 1;
    }
    memset(ds_mem + 0x520, 0, ELKS_KMEM_LIST_SIZE);
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_read])(4, 0x520, ELKS_KMEM_LIST_SIZE, 0, 0, 0, 0, 0) !=
        ELKS_KMEM_LIST_SIZE) {
        fprintf(stderr, "FAIL: ELKS kmem segall read wrong\n");
        return 1;
    }
    if (*(uint16_t *)(void *)(ds_mem + 0x520 + ELKS_KMEM_LIST_NEXT) == ELKS_KMEM_SEGALL_OFFSET ||
        *(uint16_t *)(void *)(ds_mem + 0x520 + ELKS_KMEM_LIST_PREV) == ELKS_KMEM_SEGALL_OFFSET) {
        fprintf(stderr, "FAIL: ELKS kmem segall list empty\n");
        return 1;
    }
    *(int32_t *)(void *)(ds_mem + 0x164) = (int32_t)ELKS_KMEM_HEAPALL_OFFSET;
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_lseek])(4, 0x164, 0, 0, 0, 0, 0, 0) != 0 ||
        *(int32_t *)(void *)(ds_mem + 0x164) != (int32_t)ELKS_KMEM_HEAPALL_OFFSET) {
        fprintf(stderr, "FAIL: ELKS kmem heapall lseek wrong\n");
        return 1;
    }
    memset(ds_mem + 0x530, 0, ELKS_KMEM_LIST_SIZE);
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_read])(4, 0x530, ELKS_KMEM_LIST_SIZE, 0, 0, 0, 0, 0) !=
        ELKS_KMEM_LIST_SIZE) {
        fprintf(stderr, "FAIL: ELKS kmem heapall read wrong\n");
        return 1;
    }
    if (*(uint16_t *)(void *)(ds_mem + 0x530 + ELKS_KMEM_LIST_NEXT) == ELKS_KMEM_HEAPALL_OFFSET ||
        *(uint16_t *)(void *)(ds_mem + 0x530 + ELKS_KMEM_LIST_PREV) == ELKS_KMEM_HEAPALL_OFFSET) {
        fprintf(stderr, "FAIL: ELKS kmem heapall list empty\n");
        return 1;
    }
    *(int32_t *)(void *)(ds_mem + 0x166) = (int32_t)ELKS_KMEM_TASKS_OFFSET;
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_lseek])(4, 0x166, 0, 0, 0, 0, 0, 0) != 0 ||
        *(int32_t *)(void *)(ds_mem + 0x166) != (int32_t)ELKS_KMEM_TASKS_OFFSET) {
        fprintf(stderr, "FAIL: ELKS kmem lseek emulation wrong\n");
        return 1;
    }
    memset(ds_mem + 0x360, 0, ELKS_KMEM_TASK_SLOT_SIZE);
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_read])(4, 0x360, ELKS_KMEM_TASK_SLOT_SIZE, 0, 0, 0, 0, 0) !=
        ELKS_KMEM_TASK_SLOT_SIZE) {
        fprintf(stderr, "FAIL: ELKS kmem read emulation wrong\n");
        return 1;
    }
    if (*(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_PID) != 0 ||
        *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_KSTACK_MAGIC) != ELKS_KSTACK_MAGIC ||
        *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_PGRP_LEGACY) != 0) {
        fprintf(stderr, "FAIL: ELKS kmem swapper slot wrong\n");
        return 1;
    }
    *(int32_t *)(void *)(ds_mem + 0x166) = (int32_t)(ELKS_KMEM_TASKS_OFFSET + ELKS_KMEM_TASK_SLOT_SIZE);
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_lseek])(4, 0x166, 0, 0, 0, 0, 0, 0) != 0 ||
        *(int32_t *)(void *)(ds_mem + 0x166) != (int32_t)(ELKS_KMEM_TASKS_OFFSET + ELKS_KMEM_TASK_SLOT_SIZE)) {
        fprintf(stderr, "FAIL: ELKS kmem active-slot lseek wrong\n");
        return 1;
    }
    memset(ds_mem + 0x360, 0, ELKS_KMEM_TASK_SLOT_SIZE);
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_read])(4, 0x360, ELKS_KMEM_TASK_SLOT_SIZE, 0, 0, 0, 0, 0) !=
        ELKS_KMEM_TASK_SLOT_SIZE) {
        fprintf(stderr, "FAIL: ELKS kmem active-slot read wrong\n");
        return 1;
    }
    if (*(uint8_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_STATE) != ELKS_TASK_RUNNING ||
        *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_PID) != 7 ||
        *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_KSTACK_MAGIC) != ELKS_KSTACK_MAGIC ||
        *(uint8_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_STATE_LEGACY) != ELKS_TASK_RUNNING ||
        *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_PID_LEGACY) != 7 ||
        *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_PGRP_LEGACY) != 7 ||
        *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_UID_LEGACY) != 42 ||
        *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_MM_LEGACY) == 0 ||
        *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_T_BEGSTACK_LEGACY) == 0 ||
        *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_KSTACK_MAGIC_ALT) != ELKS_KSTACK_MAGIC) {
        fprintf(stderr, "FAIL: ELKS kmem active task image wrong\n");
        return 1;
    }
    *(int32_t *)(void *)(ds_mem + 0x168) =
        (int32_t)*(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_T_BEGSTACK_LEGACY);
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_lseek])(4, 0x168, 0, 0, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "FAIL: ELKS kmem stack lseek wrong\n");
        return 1;
    }
    memset(ds_mem + 0x460, 0, 0x220);
    if (((int (*)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t))
         personality_elks.syscall_table[ELKS_SYS_read])(4, 0x460, 0x220, 0, 0, 0, 0, 0) != 0x220) {
        fprintf(stderr, "FAIL: ELKS kmem stack read wrong\n");
        return 1;
    }
    if (*(uint16_t *)(void *)(ds_mem + 0x460) != 3 ||
        *(uint16_t *)(void *)(ds_mem + 0x460 + 2) !=
            *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_T_BEGSTACK_LEGACY) + 10 ||
        *(uint16_t *)(void *)(ds_mem + 0x460 + 4) !=
            *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_T_BEGSTACK_LEGACY) + 13 ||
        *(uint16_t *)(void *)(ds_mem + 0x460 + 6) !=
            *(uint16_t *)(void *)(ds_mem + 0x360 + ELKS_KMEM_TASK_T_BEGSTACK_LEGACY) + 16 ||
        *(uint16_t *)(void *)(ds_mem + 0x460 + 8) != 0 ||
        strcmp((char *)(void *)(ds_mem + 0x460 + 10), "ps") != 0 ||
        strcmp((char *)(void *)(ds_mem + 0x460 + 13), "-l") != 0 ||
        strnlen((char *)(void *)(ds_mem + 0x460 + 16), 0x220 - 16) != 505 ||
        ((char *)(void *)(ds_mem + 0x460 + 16))[0] != 'x') {
        fprintf(stderr, "FAIL: ELKS kmem command image wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x40), "/tmp/file");
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_open];
    if (fn(0x40, 1, 2, 0, 0, 0, 0, 0) != 44 || strcmp(last_name, "open") != 0 ||
        strcmp(last_path_arg, "/tmp/file") != 0 || last_i0 != 1 || last_i1 != 2) {
        fprintf(stderr, "FAIL: ELKS open wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_close];
    if (fn(9, 0, 0, 0, 0, 0, 0, 0) != 55 || strcmp(last_name, "close") != 0 || last_i0 != 9) {
        fprintf(stderr, "FAIL: ELKS close wrapper wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x180), "/tmp/old");
    strcpy((char *)(ds_mem + 0x1a0), "/tmp/new");
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_link];
    if (fn(0x180, 0x1a0, 0, 0, 0, 0, 0, 0) != 46 || strcmp(last_name, "link") != 0 ||
        last_ptr != ds_base + 0x180U || last_ptr2 != ds_base + 0x1a0U) {
        fprintf(stderr, "FAIL: ELKS link wrapper wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x1c0), "/tmp/cwd");
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_chdir];
    if (fn(0x1c0, 0, 0, 0, 0, 0, 0, 0) != 47 || strcmp(last_name, "chdir") != 0 ||
        last_ptr != ds_base + 0x1c0U) {
        fprintf(stderr, "FAIL: ELKS chdir wrapper wrong\n");
        return 1;
    }

    *(elks_time_t *)(void *)(ds_mem + 0x1d0) = 0;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_time];
    if (fn(0x1d0, 0, 0, 0, 0, 0, 0, 0) != (int)stub_clock_sec ||
        strcmp(last_name, "time") != 0 || last_ptr != 0U ||
        *(elks_time_t *)(void *)(ds_mem + 0x1d0) != (elks_time_t)stub_clock_sec) {
        fprintf(stderr, "FAIL: ELKS time wrapper wrong\n");
        return 1;
    }
    stub_clock_sec = (time_t)INT32_MAX + 1;
    if (fn(0x1d0, 0, 0, 0, 0, 0, 0, 0) != -EOVERFLOW) {
        fprintf(stderr, "FAIL: ELKS time overflow not rejected\n");
        return 1;
    }
    stub_clock_sec = 123456789;

    strcpy((char *)(ds_mem + 0x1e0), "/dev/test");
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_mknod];
    if (fn(0x1e0, 0600, 7, 0, 0, 0, 0, 0) != 48 || strcmp(last_name, "mknod") != 0 ||
        last_ptr != ds_base + 0x1e0U || last_i0 != 0600 || last_i1 != 7) {
        fprintf(stderr, "FAIL: ELKS mknod wrapper wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x200), "/tmp/mode");
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_chmod];
    if (fn(0x200, 0644, 0, 0, 0, 0, 0, 0) != 49 || strcmp(last_name, "chmod") != 0 ||
        last_ptr != ds_base + 0x200U || last_i0 != 0644) {
        fprintf(stderr, "FAIL: ELKS chmod wrapper wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x220), "/tmp/owner");
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_chown];
    if (fn(0x220, 12, 34, 0, 0, 0, 0, 0) != 50 || strcmp(last_name, "chown") != 0 ||
        last_ptr != ds_base + 0x220U || last_i0 != 12 || last_i1 != 34) {
        fprintf(stderr, "FAIL: ELKS chown wrapper wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x240), "/dev/storage/ide0");
    strcpy((char *)(ds_mem + 0x260), "/mnt");
    strcpy((char *)(ds_mem + 0x280), "ext2");
    memset(ds_mem + 0x2a0, 0x5A, 8);
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_mount];
    if (fn(0x240, 0x260, 0x280, 3, 0x2a0, 0, 0, 0) != 51 || strcmp(last_name, "mount") != 0 ||
        last_ptr != ds_base + 0x240U || last_ptr2 != ds_base + 0x260U ||
        last_ptr3 != ds_base + 0x280U || last_i0 != 3 ||
        last_i1 != (int)(ds_base + 0x2a0U)) {
        fprintf(stderr, "FAIL: ELKS mount wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_umount];
    if (fn(0x260, 0, 0, 0, 0, 0, 0, 0) != 52 || strcmp(last_name, "umount") != 0 ||
        last_ptr != ds_base + 0x260U) {
        fprintf(stderr, "FAIL: ELKS umount wrapper wrong\n");
        return 1;
    }

    *(elks_time_t *)(void *)(ds_mem + 0x2c0) = 777;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_stime];
    if (fn(0x2c0, 0, 0, 0, 0, 0, 0, 0) != 53 || strcmp(last_name, "stime") != 0 ||
        stub_clock_sec != 777) {
        fprintf(stderr, "FAIL: ELKS stime wrapper wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x2e0), "/tmp/access");
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_access];
    if (fn(0x2e0, 4, 0, 0, 0, 0, 0, 0) != 54 || strcmp(last_name, "access") != 0 ||
        last_ptr != ds_base + 0x2e0U || last_i0 != 4) {
        fprintf(stderr, "FAIL: ELKS access wrapper wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x300), "/tmp/newdir");
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_mkdir];
    if (fn(0x300, 0755, 0, 0, 0, 0, 0, 0) != 55 || strcmp(last_name, "mkdir") != 0 ||
        last_ptr != ds_base + 0x300U || last_i0 != 0755) {
        fprintf(stderr, "FAIL: ELKS mkdir wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_rmdir];
    if (fn(0x300, 0, 0, 0, 0, 0, 0, 0) != 56 || strcmp(last_name, "rmdir") != 0 ||
        last_ptr != ds_base + 0x300U) {
        fprintf(stderr, "FAIL: ELKS rmdir wrapper wrong\n");
        return 1;
    }

    *(uint16_t *)(void *)(ds_mem + 0x320) = 0;
    *(uint16_t *)(void *)(ds_mem + 0x322) = 0;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_pipe];
    if (fn(0x320, 0, 0, 0, 0, 0, 0, 0) != 0 ||
        *(uint16_t *)(void *)(ds_mem + 0x320) != 12 ||
        *(uint16_t *)(void *)(ds_mem + 0x322) != 34) {
        fprintf(stderr, "FAIL: ELKS pipe wrapper wrong\n");
        return 1;
    }

    memset(ds_mem + 0x330, 0, sizeof(struct tms));
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_times];
    if (fn(0x330, 0, 0, 0, 0, 0, 0, 0) != 57 || strcmp(last_name, "times") != 0 ||
        last_ptr != ds_base + 0x330U) {
        fprintf(stderr, "FAIL: ELKS times wrapper wrong\n");
        return 1;
    }
    {
        struct tms *t = (struct tms *)(void *)(ds_mem + 0x330);

        if (t->tms_utime != 1 || t->tms_stime != 2 ||
            t->tms_cutime != 3 || t->tms_cstime != 4) {
            fprintf(stderr, "FAIL: ELKS times result wrong\n");
            return 1;
        }
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_waitpid];
    if (fn(12, 0x30, 7, 0, 0, 0, 0, 0) != 66 || strcmp(last_name, "waitpid") != 0 ||
        last_i0 != 12 || last_i1 != 7 || *(uint16_t *)(void *)(ds_mem + 0x30) != 0x1234) {
        fprintf(stderr, "FAIL: ELKS waitpid wrapper wrong\n");
        return 1;
    }
    if (fn(0xFFFFU, 0x30, 0, 0, 0, 0, 0, 0) != 66 || strcmp(last_name, "waitpid") != 0 ||
        last_i0 != -1 || last_i1 != 0 || *(uint16_t *)(void *)(ds_mem + 0x30) != 0x1234) {
        fprintf(stderr, "FAIL: ELKS waitpid signed pid translation wrong\n");
        return 1;
    }

    *(uint16_t *)(void *)(ds_mem + 0x10) = 0;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_getpid];
    if (fn(0x10, 0, 0, 0, 0, 0, 0, 0) != 321 ||
        *(uint16_t *)(void *)(ds_mem + 0x10) != 654) {
        fprintf(stderr, "FAIL: ELKS getpid/getppid translation wrong\n");
        return 1;
    }

    *(uint16_t *)(void *)(ds_mem + 0x12) = 0;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_getuid];
    if (fn(0x12, 0, 0, 0, 0, 0, 0, 0) != 123 ||
        *(uint16_t *)(void *)(ds_mem + 0x12) != 124) {
        fprintf(stderr, "FAIL: ELKS getuid/euid translation wrong\n");
        return 1;
    }

    *(uint16_t *)(void *)(ds_mem + 0x14) = 0;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_getgid];
    if (fn(0x14, 0, 0, 0, 0, 0, 0, 0) != 223 ||
        *(uint16_t *)(void *)(ds_mem + 0x14) != 224) {
        fprintf(stderr, "FAIL: ELKS getgid/egid translation wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_brk];
    if (fn(0x60, 0, 0, 0, 0, 0, 0, 0) != 0x200 || strcmp(last_name, "brk") != 0 ||
        last_ptr != ds_base + 0x60U) {
        fprintf(stderr, "FAIL: ELKS brk wrapper wrong\n");
        return 1;
    }
    if (fn(0x2000, 0, 0, 0, 0, 0, 0, 0) != -ENOMEM) {
        fprintf(stderr, "FAIL: ELKS brk bounds check wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_fork];
    if (fn(0, 0, 0, 0, 0, 0, 0, 0) != 12 || strcmp(last_name, "fork") != 0) {
        fprintf(stderr, "FAIL: ELKS fork wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_vfork];
    if (fn(0, 0, 0, 0, 0, 0, 0, 0) != 13 || strcmp(last_name, "vfork") != 0) {
        fprintf(stderr, "FAIL: ELKS vfork wrapper wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x20), "/bin/sh");
    {
        uint16_t *stack = (uint16_t *)(void *)(ds_mem + 0x80);
        char *strings = (char *)(ds_mem + 0x80 + 12);

        stack[0] = 2;
        stack[1] = 12;
        stack[2] = 15;
        stack[3] = 0;
        stack[4] = 18;
        stack[5] = 0;

        strcpy(strings + 0, "sh");
        strcpy(strings + 3, "-c");
        strcpy(strings + 6, "TERM=ansi");
    }
    memset(last_exec_path, 0, sizeof(last_exec_path));
    memset(last_exec_argv0, 0, sizeof(last_exec_argv0));
    memset(last_exec_argv1, 0, sizeof(last_exec_argv1));
    memset(last_exec_env0, 0, sizeof(last_exec_env0));
    memset(last_path_arg, 0, sizeof(last_path_arg));
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_execve];
    if (fn(0x20, 0x80, 32, 0, 0, 0, 0, 0) != 77 || strcmp(last_name, "execve") != 0) {
        fprintf(stderr, "FAIL: ELKS execve wrapper wrong return path\n");
        return 1;
    }
    if (strcmp(last_exec_path, "/bin/sh") != 0 ||
        strcmp(last_exec_argv0, "sh") != 0 ||
        strcmp(last_exec_argv1, "-c") != 0 ||
        strcmp(last_exec_env0, "TERM=ansi") != 0) {
        fprintf(stderr, "FAIL: ELKS execve stack decoding wrong\n");
        return 1;
    }

    last_kill_pid = -1;
    last_kill_sig = -1;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_kill];
    if (fn(77, 2, 0, 0, 0, 0, 0, 0) != 99 ||
        last_kill_pid != 77 || last_kill_sig != SIGINT) {
        fprintf(stderr, "FAIL: ELKS kill translation wrong\n");
        return 1;
    }
    if (fn(77, 99, 0, 0, 0, 0, 0, 0) != -EINVAL) {
        fprintf(stderr, "FAIL: ELKS kill invalid signal translation wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x180), "/tmp/link");
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_stat];
    if (fn(0x180, 0x1C0, 0, 0, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "FAIL: ELKS stat wrapper wrong return path\n");
        return 1;
    }
    {
        struct elks_stat *st = (struct elks_stat *)(void *)(ds_mem + 0x1C0);
        if (st->st_ino != 0x11223344U || st->st_mode != 0120644 ||
            st->st_size != 1234 || st->st_ctime != 3) {
            fprintf(stderr, "FAIL: ELKS stat translation wrong\n");
            return 1;
        }
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_lstat];
    if (fn(0x180, 0x200, 0, 0, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "FAIL: ELKS lstat wrapper wrong return path\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_fstat];
    if (fn(9, 0x240, 0, 0, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "FAIL: ELKS fstat wrapper wrong return path\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_readlink];
    memset(ds_mem + 0x280, 0, 16);
    if (fn(0x180, 0x280, 16, 0, 0, 0, 0, 0) != 7 ||
        strcmp((char *)(ds_mem + 0x280), "/target") != 0) {
        fprintf(stderr, "FAIL: ELKS readlink translation wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_readdir];
    memset(ds_mem + 0x2C0, 0, sizeof(struct elks_dirent));
    if (fn(3, 0x2C0, 1, 0, 0, 0, 0, 0) != 1) {
        fprintf(stderr, "FAIL: ELKS readdir wrapper wrong return path\n");
        return 1;
    }
    {
        struct elks_dirent *de = (struct elks_dirent *)(void *)(ds_mem + 0x2C0);
        if (de->d_ino != 0xAABBCCDDU || de->d_offset != 1 ||
            de->d_namlen != 4 || strcmp(de->d_name, "init") != 0) {
            fprintf(stderr, "FAIL: ELKS readdir translation wrong\n");
            return 1;
        }
    }

    *(uint16_t *)(void *)(ds_mem + 0x300) = 0;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_sbrk];
    {
        int rc = fn(0x20, 0x300, 0, 0, 0, 0, 0, 0);
        uint16_t oldbrk = *(uint16_t *)(void *)(ds_mem + 0x300);

        if (rc != 0 || oldbrk != 0x200 ||
            current_process->brk != current_process->brk_start + 0x20U) {
            fprintf(stderr, "FAIL: ELKS sbrk translation wrong rc=%d old=0x%x brk=0x%x start=0x%x\n",
                    rc, oldbrk, current_process->brk, current_process->brk_start);
            return 1;
        }
    }
    current_process->brk = 0;
    *(uint16_t *)(void *)(ds_mem + 0x302) = 0;
    {
        int rc = fn(0x10, 0x302, 0, 0, 0, 0, 0, 0);
        uint16_t oldbrk = *(uint16_t *)(void *)(ds_mem + 0x302);

        if (rc != 0 || oldbrk != 0x200 ||
            current_process->brk != current_process->brk_start + 0x10U) {
            fprintf(stderr, "FAIL: ELKS sbrk bootstrap wrong rc=%d old=0x%x brk=0x%x start=0x%x\n",
                    rc, oldbrk, current_process->brk, current_process->brk_start);
            return 1;
        }
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_gettimeofday];
    memset(ds_mem + 0x320, 0, 12);
    stub_clock_sec = 0x11223344;
    stub_clock_usec = 0x00556677;
    stub_tz_minuteswest = 42;
    stub_tz_dsttime = 3;
    if (fn(0x320, 0x328, 0, 0, 0, 0, 0, 0) != 0 ||
        *(int32_t *)(void *)(ds_mem + 0x320) != 0x11223344 ||
        *(int32_t *)(void *)(ds_mem + 0x324) != 0x00556677 ||
        *(int16_t *)(void *)(ds_mem + 0x328) != 42 ||
        *(int16_t *)(void *)(ds_mem + 0x32A) != 3) {
        fprintf(stderr, "FAIL: ELKS gettimeofday translation wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_settimeofday];
    *(int32_t *)(void *)(ds_mem + 0x330) = 0x01020304;
    *(int32_t *)(void *)(ds_mem + 0x334) = 12345;
    if (fn(0x330, 0, 0, 0, 0, 0, 0, 0) != 0 || stub_clock_sec != 0x01020304) {
        fprintf(stderr, "FAIL: ELKS settimeofday translation wrong\n");
        return 1;
    }

    memset(ds_mem + 0x338, 0, sizeof(struct elks_utsname));
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_uname];
    if (fn(0x338, 0, 0, 0, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "FAIL: ELKS uname wrapper returned error\n");
        return 1;
    }
    {
        struct elks_utsname *u = (struct elks_utsname *)(void *)(ds_mem + 0x338);

        if (strcmp(u->sysname, "Substra") != 0 ||
            strcmp(u->nodename, "substrate-host") != 0 ||
            strcmp(u->release, "0.1") != 0 ||
            strcmp(u->version, "Kernel") != 0 ||
            strcmp(u->machine, "i386") != 0) {
            fprintf(stderr, "FAIL: ELKS uname translation wrong\n");
            return 1;
        }
    }

    memset(ds_mem + 0x380, 0, sizeof(struct elks_statfs));
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_ustatfs];
    if (fn(0, 0x380, ELKS_UF_NOFREESPACE, 0, 0, 0, 0, 0) != 0) {
        fprintf(stderr, "FAIL: ELKS ustatfs wrapper returned error\n");
        return 1;
    }
    {
        struct elks_statfs *s = (struct elks_statfs *)(void *)(ds_mem + 0x380);

        if (s->f_type != ELKS_FST_OTHER ||
            s->f_flags != 0 ||
            s->f_dev != 0 ||
            s->f_bsize != 1024 ||
            s->f_blocks != 4096 ||
            s->f_bfree != 0 ||
            s->f_bavail != 0 ||
            s->f_files != 256 ||
            s->f_ffree != 128 ||
            strcmp(s->f_mntonname, "/") != 0) {
            fprintf(stderr, "FAIL: ELKS ustatfs translation wrong\n");
            return 1;
        }
    }
    if (fn(1, 0, 0, 0, 0, 0, 0, 0) != ELKS_FST_OTHER ||
        fn(99, 0x380, 0, 0, 0, 0, 0, 0) != -EINVAL) {
        fprintf(stderr, "FAIL: ELKS ustatfs type/error handling wrong\n");
        return 1;
    }

    stub_oldact.sa_handler = SIG_DFL;
    last_sigaction_sig = -1;
    last_sigaction_handler = SIG_ERR;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_signal];
    if (fn(2, 0, 0, 0, 0, 0, 0, 0) != 0 ||
        last_sigaction_sig != SIGINT ||
        last_sigaction_handler != SIG_DFL) {
        fprintf(stderr, "FAIL: ELKS signal(SIG_DFL) translation wrong\n");
        return 1;
    }

    stub_oldact.sa_handler = SIG_IGN;
    last_sigaction_sig = -1;
    last_sigaction_handler = SIG_ERR;
    if (fn(3, 1, 0, 0, 0, 0, 0, 0) != 1 ||
        last_sigaction_sig != SIGQUIT ||
        last_sigaction_handler != SIG_IGN) {
        fprintf(stderr, "FAIL: ELKS signal(SIG_IGN) translation wrong\n");
        return 1;
    }

    stub_oldact.sa_handler = (sig_t)(uintptr_t)(ds_base + 0x88U);
    last_sigaction_sig = -1;
    last_sigaction_handler = SIG_ERR;
    if (fn(14, 0x44, cs_sel, 0, 0, 0, 0, 0) != 2 ||
        last_sigaction_sig != SIGALRM ||
        (uintptr_t)last_sigaction_handler != ds_base + 0x44U) {
        fprintf(stderr, "FAIL: ELKS custom signal handler translation wrong\n");
        return 1;
    }

    if (fn(16, 0, 0, 0, 0, 0, 0, 0) != 2) {
        fprintf(stderr, "FAIL: ELKS SIGURG disposition translation wrong\n");
        return 1;
    }

    {
        registers_t regs;
        uint16_t *words;

        memset(&regs, 0, sizeof(regs));
        memset(ds_mem + 0x180, 0, 16);
        last_sigexit_sig = 0;
        regs.eip = 0x1234;
        regs.cs = cs_sel;
        regs.useresp = 0x0186;
        regs.ss = ss_sel;
        regs.eflags = 0x00000602U | (1U << 10);

        personality_elks.sendsig((void *)(uintptr_t)(ds_base + 0x0044U), 14, 0, 0, &regs);
        if (last_sigexit_sig != 0) {
            fprintf(stderr, "FAIL: ELKS sendsig faulted\n");
            return 1;
        }
        if (regs.eip != 0x0044U || regs.cs != cs_sel || regs.useresp != 0x0180U) {
            fprintf(stderr, "FAIL: ELKS sendsig register rewrite wrong\n");
            return 1;
        }
        if (regs.eflags & (1U << 10)) {
            fprintf(stderr, "FAIL: ELKS sendsig did not clear DF\n");
            return 1;
        }

        words = (uint16_t *)(void *)(ds_mem + 0x0180);
        if (words[0] != 0x1234U || words[1] != cs_sel || words[2] != 14U) {
            fprintf(stderr, "FAIL: ELKS sendsig frame wrong\n");
            return 1;
        }
    }

    current_thread->syscall_num = 127;
    memset(last_log, 0, sizeof(last_log));
    cmdline_init("debug=perso:elks:syscall");
    fn = (void *)personality_elks.syscall_table[127];
    if (fn(0, 0, 0, 0, 0, 0, 0, 0) != -ENOSYS) {
        fprintf(stderr, "FAIL: ELKS unsupported syscall did not return ENOSYS\n");
        return 1;
    }
    if (strstr(last_log, "unsupported syscall") == NULL) {
        fprintf(stderr, "FAIL: ELKS unsupported syscall did not log\n");
        return 1;
    }

    munmap(ds_mem, ds_mem_size);
    puts("host_test_perso_elks: ok");
    return 0;
}
