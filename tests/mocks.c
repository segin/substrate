#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include "../sys/arch/i386/pmap.h"
#include "../sys/vm/vm_map.h"

// VGA/UART Mocks
void vga_write(const char *s, size_t n) { (void)s; (void)n; }
void uart_write(const char *s, size_t n) { (void)s; (void)n; }
void vga_init() {}
void uart_init() {}
void keyboard_handler(void *regs) { (void)regs; }
void mouse_handler(void *regs) { (void)regs; }

// FS init mocks
void ext2_init() {}
void fat_init() {}
void exfat_init() {}
void minix_init() {}
void udf_init() {}
void procfs_init() {}
void sysfs_init() {}
void pseudo_init() {}
void full_init() {}
void fuse_init() {}
void fuse_fs_init() {}
void p9_init() {}
void vfs_init_mock_root(void) {}
void nchinit(void) {}
struct fs_node *fs_root = NULL;

// Driver init mocks
void scsi_init() {}
void ide_init() {}
void ahci_init() {}
void nvme_init() {}
void pci_init() {}
void fpu_init() {}
void keyboard_init() {}
void mouse_init() {}
void input_init() {}

// Panic Mock
void panic(const char *msg) {
    fprintf(stderr, "KERNEL PANIC (Mock): %s\n", msg);
    exit(1);
}

// Globals Mocks
#include <sys/proc.h>
#include "../sys/exec/perso/personality.h"
struct personality personality_native = { "Native", PERS_NATIVE, NULL };
struct personality personality_freebsd = { "FreeBSD", PERS_FREEBSD, NULL };
struct personality personality_linux = { "Linux", PERS_LINUX, NULL };

// Paging Mocks
void pmap_invalidate_page(uint32_t v) { (void)v; }

int pmap_enter(pmap_t p, uint32_t va, uint32_t pa, uint32_t prot, uint32_t flags) {
    (void)p; (void)va; (void)pa; (void)prot; (void)flags;
    return 0;
}

uint32_t pmap_extract(pmap_t p, uint32_t va) {
    (void)p; (void)va;
    return 0x1234000;
}

pmap_t pmap_kernel() { return (pmap_t)1; }
void pmap_activate(pmap_t pmap) { (void)pmap; }

// PMM Mocks
static char mock_phys_memory[1024 * 4096];
static int next_mock_page = 0;

void *pmm_alloc_block() {
    if (next_mock_page < 1024) {
        return &mock_phys_memory[(next_mock_page++) * 4096];
    }
    return NULL;
}

void pmm_free_block(void *p) { (void)p; }
void *pmm_alloc_contiguous(size_t c) {
    if (next_mock_page + c < 1024) {
        void *p = &mock_phys_memory[next_mock_page * 4096];
        next_mock_page += c;
        return p;
    }
    return NULL;
}
void pmm_free_contiguous(void *p, size_t c) { (void)p; (void)c; }

// idt/gdt/io mocks
void idt_flush(uint32_t p) { (void)p; }
void gdt_flush(uint32_t p) { (void)p; }
void tss_flush() {}
void idt_set_gate(uint8_t n, uint32_t b, uint16_t s, uint8_t f) { (void)n; (void)b; (void)s; (void)f; }
void outb(uint16_t p, uint8_t v) { (void)p; (void)v; }
uint8_t inb(uint16_t p) { (void)p; return 0; }
uint32_t lapic_get_id() { return 0; }
int smp_get_cpu_count() { return 1; }

// kmem mocks for host
void *kmalloc(size_t size) {
    return malloc(size);
}
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

void set_kernel_stack(uint32_t stack) { (void)stack; }

void switch_to(void *prev, void *next) {
    (void)prev; (void)next;
    // Do nothing in mock, context switching isn't real on host
}

void isr128() {}
void fork_child_return() {}

// Syscall Mocks
int sys_unlink(const char *path) { (void)path; return -1; }
int sys_execve(const char *path, char **argv, char **envp) { (void)path; (void)argv; (void)envp; return -1; }
int sys_chdir(const char *path) { (void)path; return -1; }
int sys_mknod(const char *path, int mode, int dev) { (void)path; (void)mode; (void)dev; return -1; }
int sys_chmod(const char *path, int mode) { (void)path; (void)mode; return -1; }
int sys_lchown(const char *path, int uid, int gid) { (void)path; (void)uid; (void)gid; return -1; }
int sys_stat(const char *path, void *buf) { (void)path; (void)buf; return -1; }
int64_t sys_lseek(int fd, uint32_t hi, uint32_t lo, int whence) { (void)fd; (void)hi; (void)lo; (void)whence; return -1; }
int sys_getpid(void) { return 1; }
int sys_mount(const char *dev, const char *dir, const char *type, unsigned long flags, void *data) { (void)dev; (void)dir; (void)type; (void)flags; (void)data; return -1; }
int sys_umount(const char *dir) { (void)dir; return -1; }
int sys_setuid(int uid) { (void)uid; return -1; }
int sys_getuid(void) { return 0; }
int sys_access(const char *path, int mode) { (void)path; (void)mode; return -1; }
int sys_sync(void) { return 0; }
int sys_dup(int fd) { (void)fd; return -1; }
int sys_pipe(int *fds) { (void)fds; return -1; }
int sys_setgid(int gid) { (void)gid; return -1; }
int sys_getgid(void) { return 0; }
int sys_ioctl(int fd, uint32_t cmd, void *arg) { (void)fd; (void)cmd; (void)arg; return -1; }
int sys_chroot(const char *path) { (void)path; return -1; }
int sys_fcntl(int fd, int cmd, int arg) { (void)fd; (void)cmd; (void)arg; return -1; }
int sys_dup2(int oldfd, int newfd) { (void)oldfd; (void)newfd; return -1; }
int sys_rmdir(const char *path) { (void)path; return -1; }
int sys_mkdir(const char *path, int mode) { (void)path; (void)mode; return -1; }
int sys_getdents(unsigned int fd, void *dirp, unsigned int count) { (void)fd; (void)dirp; (void)count; return -1; }
int sys_getcwd(char *buf, size_t size) { (void)buf; (void)size; return -1; }
int sys_waitpid(int pid, int *status, int options) { (void)pid; (void)status; (void)options; return -1; }
int sys_creat(const char *path, int mode) { (void)path; (void)mode; return -1; }
int sys_exit(int status) { exit(status); }
int sys_read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return -1; }
int sys_write(int fd, const char *buf, int len) { (void)fd; (void)buf; (void)len; return -1; }
int sys_open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; return -1; }
int sys_close(int fd) { (void)fd; return -1; }

// Other missing functions
void kprint(const char *s) { printf("%s", s); }
void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// Map some missing functions used in tests
bool test_libc_strlen(void) { return true; }
bool test_libc_memmove(void) { return true; }
bool test_fd_ref_counting(void) { return true; }

// LDT mocks
void ldt_activate(process_t *p) { (void)p; }

// Copy functions mocks
int copyin(const void *uaddr, void *kaddr, size_t len) { memcpy(kaddr, uaddr, len); return 0; }
int copyout(const void *kaddr, void *uaddr, size_t len) { memcpy(uaddr, kaddr, len); return 0; }
int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len) {
    const char *uaddr = (const char *)src;
    char *kaddr = (char *)dst;
    size_t i;
    for (i = 0; i < maxlen; i++) {
        kaddr[i] = uaddr[i];
        if (uaddr[i] == '\0') {
            if (len) *len = i + 1;
            return 0;
        }
    }
    if (len) *len = maxlen;
    return 0;
}

// PM/Sched missing globals
process_t *current_process = NULL;
process_t processes[16]; // Mock process table

void pm_init(void) {}
uint64_t get_ticks(void) { static uint64_t t = 0; return t++; }
uint32_t get_hz(void) { return 100; }
void sched_periodic_balance(void) {}
void sched_smp_init(int n) { (void)n; }
void sched_update_loadavg(void) {}
int syscall_trace_enabled = 0;
void devfs_init(void) {}
struct fs_node *devfs_root_node_ptr = NULL;
struct nameidata;
int namei(struct nameidata *nd) { (void)nd; return -1; }
void vput(void *vp) { (void)vp; }
void vrele(void *vp) { (void)vp; }
void gdt_set_gate(int n, uint32_t b, uint32_t l, uint8_t a, uint8_t g) { (void)n; (void)b; (void)l; (void)a; (void)g; }
void rusage_init(process_t *p) { (void)p; }
void rusage_finalize(process_t *p) { (void)p; }
void tty_hangup(void *t) { (void)t; }
void file_close_ptr(void *fp) { (void)fp; }

struct pgrp;
struct pgrp *pgrp_find(int id) { (void)id; return NULL; }
void pgrp_signal(struct pgrp *pg, int sig) { (void)pg; (void)sig; }
int pgrp_is_orphaned(struct pgrp *pg) { (void)pg; return 0; }
const uint8_t sigprop[NSIG] = { 0 };
struct personality *perso_lookup(int id) { (void)id; return &personality_native; }
void sendsig(void *h, int s, uint32_t m, uint32_t f, void *r) { (void)h; (void)s; (void)m; (void)f; (void)r; }
