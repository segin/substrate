#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>
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
__attribute__((weak)) void procfs_init() {}
void sysfs_init() {}
void pseudo_init() {}
void full_init() {}
void fuse_init() {}
void fuse_fs_init() {}
void p9_init() {}
void devfs_init() {}
void vfs_init_mock_root(void);
// nchinit and fs_root removed (linked from vfs)

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
void pmap_invalidate_page(uintptr_t v) { (void)v; }

int mock_pmap_enter_count = 0;
int mock_pmap_enter_batch_count = 0;

int pmap_enter(pmap_t p, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags) {
    (void)p; (void)va; (void)pa; (void)prot; (void)flags;
    mock_pmap_enter_count++;
    return 0;
}

int pmap_enter_batch(pmap_t pmap, uintptr_t va_start, int count, uintptr_t *pa_list, uint32_t prot, uint32_t flags) {
    (void)pmap; (void)va_start; (void)count; (void)pa_list; (void)prot; (void)flags;
    mock_pmap_enter_batch_count++;
    return 0;
}

uintptr_t pmap_extract(pmap_t p, uintptr_t va) {
    (void)p;
    return va;
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
void gdt_set_gate(int n, uint32_t b, uint32_t l, uint8_t a, uint8_t g) { (void)n; (void)b; (void)l; (void)a; (void)g; }
void tss_flush() {}
void idt_set_gate(uint8_t n, uint32_t b, uint16_t s, uint8_t f) { (void)n; (void)b; (void)s; (void)f; }
void outb(uint16_t p, uint8_t v) { (void)p; (void)v; }
uint8_t inb(uint16_t p) { (void)p; return 0; }
uint32_t lapic_get_id() { return 0; }
int smp_get_cpu_count() { return 1; }
int smp_get_cpu_id() { return 0; }
void sched_smp_init() {}

// kmem mocks for host
void *kmalloc(size_t size) {
    if (size > 65536) return NULL;
    // Host tests use standard malloc for kmalloc
    return malloc(size);
}
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

void set_kernel_stack(uint32_t stack) { (void)stack; }

void switch_to(void *prev, void *next) {
    (void)prev;
    extern thread_t *current_thread;
    current_thread = (thread_t *)next;
}

void isr128() {}
void fork_child_return(void) {}

// Syscall Mocks - Only keep those NOT in syscall.c or other linked files
int sys_fork(void) { return 0; }

// Other missing functions
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

// Copy functions mocks
void *mock_fault_addr = NULL;

int copyin(const void *src, void *dst, size_t size) {
    if (src == mock_fault_addr) return 14; // EFAULT
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    if (dst == mock_fault_addr) return 14; // EFAULT
    memcpy(dst, src, size);
    return 0;
}

int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len) {
    size_t l = strlen(src);
    if (l >= maxlen) {
        l = maxlen - 1;
    }
    memcpy(dst, src, l);
    ((char *)dst)[l] = '\0';
    if (len) *len = l + 1;
    return 0;
}

// PM/Sched globals and mocks
void sched_periodic_balance(void) {}
void sched_update_loadavg(void) {}
int syscall_trace_enabled_fn(int syscall_num) { (void)syscall_num; return 0; }

// VFS Mocks
#include <vfs/vfs.h>
fs_node_t mock_root;
static fs_node_t mock_init_node;
static fs_node_t *mock_finddir(fs_node_t *node, char *name) {
    (void)node;
    if (strcmp(name, "init") == 0) return &mock_init_node;
    return NULL;
}
void vfs_init_mock_root() {
    memset(&mock_root, 0, sizeof(mock_root));
    strcpy(mock_root.name, "/");
    mock_root.flags = FS_DIRECTORY;
    mock_root.finddir = mock_finddir;
    
    memset(&mock_init_node, 0, sizeof(mock_init_node));
    strcpy(mock_init_node.name, "init");
    mock_init_node.flags = FS_FILE;
    
    fs_root = &mock_root;
}

// Additional Kernel Mocks for Linker
pmap_t pmap_fork(pmap_t pmap) { (void)pmap; return NULL; }
void pmap_release(pmap_t pmap) { (void)pmap; }
void rusage_init(process_t *p) { (void)p; }
void rusage_finalize(process_t *p) { (void)p; }
void tty_hangup(struct tty *tty) { (void)tty; }
static int mock_swap_count = 0;
int swap_out(void *m) {
    if (mock_swap_count >= 1024) return -1;
    mock_swap_count++;
    vm_page_t *page = (vm_page_t *)m;
    page->flags |= PG_SWAPPED;
    page->flags &= ~PG_VALID;
    return 0;
}
int swap_in(void *m) {
    if (mock_swap_count > 0) mock_swap_count--;
    vm_page_t *page = (vm_page_t *)m;
    page->flags &= ~PG_SWAPPED;
    page->flags |= PG_VALID;
    return 0;
}
void reset_swap_mock(void) { mock_swap_count = 0; }

void pmap_bootstrap(void) {}
void pmap_map_trampoline(void) {}
void random_init(void) {}
void crc32_init(void) {}
void virtio_init(void) {}
void ntsync_init(void) {}
void run_kernel_tests(void) {}
void console_register_devfs(void) {}
void pmm_reclaim_setup(void) {}
void pmm_reclaim_range(uint32_t s, uint32_t e) { (void)s; (void)e; }
void ldt_activate(process_t *p) { (void)p; }

char kernel_hostname[64] = "host";

// New Mocks for Host Tests
#include <sys/exec.h>

#ifndef _BOOLEAN_T_DEFINED
typedef int boolean_t;
#define _BOOLEAN_T_DEFINED
#endif

int pmap_is_referenced_range(pmap_t pmap, uintptr_t sva, uintptr_t eva) { (void)pmap; (void)sva; (void)eva; return 0; }
int pmap_test_and_clear_ref(pmap_t pmap, uintptr_t va) { (void)pmap; (void)va; return 0; }
int pmap_is_modified_range(pmap_t pmap, uintptr_t sva, uintptr_t eva) { (void)pmap; (void)sva; (void)eva; return 0; }
int pmap_test_and_clear_modify(pmap_t pmap, uintptr_t va) { (void)pmap; (void)va; return 0; }

void pmap_copy_page(uintptr_t src, uintptr_t dst) { (void)src; (void)dst; }
void pmap_zero_page(uintptr_t pa) { (void)pa; }
void pmap_remove(pmap_t pmap, uintptr_t va) { (void)pmap; (void)va; }

struct pgrp *pgrp_find(pid_t pgid) { (void)pgid; return NULL; }
int pgrp_signal(struct pgrp *pgrp, int sig, int check_session) { (void)pgrp; (void)sig; (void)check_session; return 0; }
int pgrp_is_orphaned(struct pgrp *pgrp) { (void)pgrp; return 0; }
void pgrp_remove_proc(struct process *proc) { (void)proc; }
int sched_interactivity_boost(thread_t *t) { (void)t; return 0; }
struct personality *perso_lookup(int id) { (void)id; return &personality_native; }
void sendsig(void *sf, struct process *p, int sig) { (void)sf; (void)p; (void)sig; }

const uint8_t sigprop[NSIG] = {0}; 

int exec_dispatch(const char *path, char *const argv[], char *const envp[]) { (void)path; (void)argv; (void)envp; return 0; }

struct fs_node *devfs_root_node_ptr;
struct nameidata;
int namei(const char *path, struct nameidata *ndp) { (void)path; (void)ndp; return -1; }
int namei_simple(const char *path, struct nameidata *ndp) { (void)path; (void)ndp; return -1; }
struct vnode;

// vm_map_lookup stub
vm_map_entry_t *vm_map_lookup(vm_map_t *map, uintptr_t va) {
    (void)map; (void)va;
    return NULL;
}

// vm_pager stubs removed (linked from vm_pager.o)

// vm_phys stubs
void vm_phys_get_free(uint64_t *free) { *free = 0; }
void vm_phys_get_used(uint64_t *used) { *used = 0; }
static struct vm_page mock_pages[256];
static int mock_page_idx = 0;
void *vm_phys_alloc_page(void) {
    if (mock_page_idx >= 256) return NULL;
    return &mock_pages[mock_page_idx++];
}
void vm_phys_free_page(void *page) { (void)page; }
void sched_get_system_load(uint32_t *loads) { loads[0]=0; loads[1]=0; loads[2]=0; }

void swapper_request_work(void) {}

// Fix cast:
uint32_t pmm_get_page(void) { return (uint32_t)(uintptr_t)malloc(4096); }

void uma_reclaim(void) {}
// libc mocks removed (in test_libc_string_unit.c)

// UMA Mocks
typedef struct uma_zone {
    const char *uz_name;
    size_t uz_size;
} uma_zone_t;

typedef int (*uma_ctor)(void *obj, int size, void *arg, int flags);
typedef void (*uma_dtor)(void *obj, int size, void *arg);
typedef int (*uma_init)(void *obj, int size, int flags);
typedef void (*uma_fini)(void *obj, int size);

uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init init, uma_fini fini, int align, uint32_t flags) {
    (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    uma_zone_t *zone = (uma_zone_t *)malloc(sizeof(uma_zone_t));
    if (zone) {
        zone->uz_name = name;
        zone->uz_size = size;
    }
    return zone;
}

void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)flags;
    if (!zone) return NULL;
    return calloc(1, zone->uz_size);
}

void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone;
    free(item);
}

void uma_zone_set_max(uma_zone_t *zone, int max) { (void)zone; (void)max; }

void cmdline_get(char *buf, size_t buf_len) { buf[0] = '\0'; }
void sched_get_loadavg(unsigned long loads[3]) { loads[0] = loads[1] = loads[2] = 0; }
uint32_t sched_count_runnable(void) { return 0; }
uint32_t sched_count_threads(void) { return 0; }
int sys_pmap_stats(struct pmap_stats *stats) { return 0; }
void namei_init(void) {}
uint32_t pmm_get_total_memory(void) { return 0; }
uint32_t pmm_get_free_memory(void) { return 0; }
