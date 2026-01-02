#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../sys/arch/i386/pmap.h"
#include "../sys/vm/vm_map.h"

// VGA/UART Mocks
void vga_write(const char *s, size_t n) { (void)s; (void)n; }
void uart_write(const char *s, size_t n) { (void)s; (void)n; }
void keyboard_handler(void *regs) { (void)regs; }

// Panic Mock
void panic(const char *msg) {
    fprintf(stderr, "KERNEL PANIC (Mock): %s\n", msg);
    exit(1);
}

// Globals Mocks
#include "../sys/sys/proc.h"
#include "../sys/exec/perso/personality.h"
process_t processes[16];
struct personality personality_native = { "Native", NULL, 0 };

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
void *pmm_alloc_contiguous(size_t c) { (void)c; return NULL; }
void pmm_free_contiguous(void *p, size_t c) { (void)p; (void)c; }

// idt/gdt/io mocks
void idt_flush(uint32_t p) { (void)p; }
void gdt_flush(uint32_t p) { (void)p; }
void tss_flush() {}
void idt_set_gate(uint8_t n, uint32_t b, uint16_t s, uint8_t f) { (void)n; (void)b; (void)s; (void)f; }
void outb(uint16_t p, uint8_t v) { (void)p; (void)v; }
uint8_t inb(uint16_t p) { (void)p; return 0; }
uint32_t lapic_get_id() { return 0; }

// kmem mocks for host
void *kmalloc(size_t size) {
    if (size > 4096) return NULL;
    return malloc(size);
}
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

void set_kernel_stack(uint32_t stack) { (void)stack; }

void switch_to(void *prev, void *next) {
    (void)prev; (void)next;
    // Do nothing in mock, context switching isn't real on host
}

// Syscall Mocks
int sys_exit(int c) { (void)c; return 0; }
int sys_fork(void) { return 0; }
int sys_read(int fd, char* b, int l) { (void)fd; (void)b; (void)l; return 0; }
int sys_write(int fd, const char* b, int l) { (void)fd; (void)b; (void)l; return 0; }
int sys_open(const char* p, int f, int m) { (void)p; (void)f; (void)m; return 0; }
int sys_close(int fd) { (void)fd; return 0; }
int sys_waitpid(int p, int* s, int o) { (void)p; (void)s; (void)o; return 0; }
int sys_creat(const char* p, int m) { (void)p; (void)m; return 0; }
int sys_link(const char* o, const char* n) { (void)o; (void)n; return 0; }
int sys_unlink(const char* p) { (void)p; return 0; }
int sys_execve(const char* f, char** a, char** e) { (void)f; (void)a; (void)e; return 0; }
int sys_chdir(const char* p) { (void)p; return 0; }
int sys_acct(const char* p) { (void)p; return 0; }
int sys_shmsys(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return 0; }
int sys_mknod(const char* p, int m, int d) { (void)p; (void)m; (void)d; return 0; }
int sys_chmod(const char* p, int m) { (void)p; (void)m; return 0; }
int sys_lchown(const char* p, int u, int g) { (void)p; (void)u; (void)g; return 0; }
int sys_stat(const char* p, void* b) { (void)p; (void)b; return 0; }
int sys_lseek(int fd, int o, int w) { (void)fd; (void)o; (void)w; return 0; }
int sys_getpid(void) { return 0; }
int sys_mount(const char* s, const char* t, const char* fs, unsigned long f, void* d) { (void)s; (void)t; (void)fs; (void)f; (void)d; return 0; }
int sys_umount(const char* t) { (void)t; return 0; }
int sys_setuid(int u) { (void)u; return 0; }
int sys_getuid(void) { return 0; }
int sys_stime(uint32_t* t) { (void)t; return 0; }
int sys_ptrace(int r, int p, int a, int d) { (void)r; (void)p; (void)a; (void)d; return 0; }
int sys_alarm(unsigned int s) { (void)s; return 0; }
int sys_fstat(int fd, void* b) { (void)fd; (void)b; return 0; }
int sys_pause(void) { return 0; }
int sys_utime(const char* p, void* t) { (void)p; (void)t; return 0; }
int sys_access(const char* p, int m) { (void)p; (void)m; return 0; }
int sys_nice(int n) { (void)n; return 0; }
int sys_statfs(const char* p, void* b) { (void)p; (void)b; return 0; }
int sys_sync(void) { return 0; }
int sys_kill(int p, int s) { (void)p; (void)s; return 0; }
int sys_fstatfs(int fd, void* b) { (void)fd; (void)b; return 0; }
int sys_pgrpsys(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return 0; }
int sys_dup(int fd) { (void)fd; return 0; }
int sys_pipe(int* p) { (void)p; return 0; }
int sys_times(void* t) { (void)t; return 0; }
int sys_prof(void* b, size_t s, unsigned long o, unsigned int f) { (void)b; (void)s; (void)o; (void)f; return 0; }
int sys_setgid(int g) { (void)g; return 0; }
int sys_getgid(void) { return 0; }
int sys_sigsys(int s, void* h) { (void)s; (void)h; return 0; }
int sys_msgsys(int a, int b, int c, int d, int e, int f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return 0; }
int sys_sysi86(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return 0; }
int sys_semsys(int a, int b, int c, int d, int e) { (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
int sys_ioctl(int fd, int r, int a) { (void)fd; (void)r; (void)a; return 0; }
int sys_uadmin(int c, int f, int m) { (void)c; (void)f; (void)m; return 0; }
int sys_utssys(void* b, int m, int o) { (void)b; (void)m; (void)o; return 0; }
int sys_fsync(int fd) { (void)fd; return 0; }
int sys_umask(int m) { (void)m; return 0; }
int sys_chroot(const char* p) { (void)p; return 0; }
int sys_fcntl(int fd, int c, int a) { (void)fd; (void)c; (void)a; return 0; }
int sys_ulimit(int c, long l) { (void)c; (void)l; return 0; }
int sys_rmdir(const char* p) { (void)p; return 0; }
int sys_mkdir(const char* p, int m) { (void)p; (void)m; return 0; }
int sys_getdents(unsigned int fd, void* d, unsigned int c) { (void)fd; (void)d; (void)c; return 0; }
int sys_getcwd(char* b, size_t s) { (void)b; (void)s; return 0; }
int sys_uname(void* b) { (void)b; return 0; }
int sys_mprotect(void* a, size_t s, int p) { (void)a; (void)s; (void)p; return 0; }
int sys_sigaction(int s, void* a, void* o) { (void)s; (void)a; (void)o; return 0; }
int sys_sigpending(int s, void* p) { (void)s; (void)p; return 0; }
int sys_sigprocmask(int h, void* s, void* o) { (void)h; (void)s; (void)o; return 0; }
int sys_sigsuspend(void* m) { (void)m; return 0; }
int sys_sigret(void) { return 0; }