#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
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
struct personality personality_native = { .name = "Native", .id = PERS_NATIVE };
struct personality personality_freebsd = { .name = "FreeBSD", .id = PERS_FREEBSD };
struct personality personality_linux = { .name = "Linux", .id = PERS_LINUX };

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

void isr128() {}

// Copy mocks
int copyin(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len) {
    size_t l = strlen(src);
    if (l >= maxlen) return -1;
    strcpy(dst, src);
    if (len) *len = l;
    return 0;
}

// VFS Mocks
#include <vfs/vfs.h>
fs_node_t mock_root;
void vfs_init_mock_root() {
    memset(&mock_root, 0, sizeof(mock_root));
    strcpy(mock_root.name, "/");
    mock_root.flags = FS_DIRECTORY;
    fs_root = &mock_root;
}

// Print Mocks
#include <stdarg.h>
void kprint(const char *s) {
    if (!s) return;
    printf("%s", s);
}

void kprintf(const char *fmt, ...) {
    if (!fmt) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

// Additional Kernel Mocks for Linker
void pmap_activate(pmap_t pmap) { (void)pmap; }
pmap_t pmap_fork(pmap_t pmap) { (void)pmap; return NULL; }
void pmap_release(pmap_t pmap) { (void)pmap; }
void sched_periodic_balance(void) {}
void rusage_init(process_t *p) { (void)p; }
void rusage_finalize(process_t *p) { (void)p; }
void tty_hangup(struct tty *tty) { (void)tty; }
int swap_out(void) { return 0; }
int swap_in(void *page, uint32_t slot) { (void)page; (void)slot; return 0; }
int sys_fork(void) { return 0; }
void psignal(struct process *p, int sig) { (void)p; (void)sig; }
void futex_thread_exit(thread_t *t) { (void)t; }
void acct_process(int code) { (void)code; }
void close_fs(fs_node_t *node) { (void)node; }
void file_close_ptr(file_t *f) { (void)f; }
void nchinit(void) {}
void sched_init_generic(void) {}
void sched_smp_init(int n) { (void)n; }
thread_t *sched_alloc_thread(process_t *p) { (void)p; return NULL; }
void arch_set_kernel_stack(uintptr_t s) { (void)s; }
void arch_switch_to(thread_t *prev, thread_t *next) { (void)prev; (void)next; }
uint64_t get_ticks(void) { return 0; }
void pmap_bootstrap(void) {}
void pmap_map_trampoline(void) {}
void random_init(void) {}
void crc32_init(void) {}
void sysctl_init(void) {}
void virtio_init(void) {}
void ntsync_init(void) {}
void run_kernel_tests(void) {}
void vfs_init(void) {}
void console_register_devfs(void) {}
void pmm_reclaim_setup(void) {}
void pmm_reclaim_range(uint32_t s, uint32_t e) { (void)s; (void)e; }
void ldt_activate(process_t *p) { (void)p; }
void pmap_activate_direct(pmap_t pmap) { (void)pmap; }
void sched_yield(void) {}
void sched_tick(void) {}
void sched_wakeup(void *chan) { (void)chan; }
void sched_wakeup_n(void *chan, int n) { (void)chan; (void)n; }
thread_t *sched_get_thread(int tid) { (void)tid; return NULL; }
void sched_sleep(void *chan) { (void)chan; }
int64_t get_time(void) { return 0; }
int smp_get_cpu_count(void) { return 1; }
char kernel_hostname[64] = "host";
void fork_child_return(void) {}
