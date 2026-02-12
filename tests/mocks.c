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

void nchinit(void) {}
void pmap_bootstrap(void) {}
void pmap_map_trampoline(void) {}
void random_init(void) {}
void crc32_init(void) {}
void sysctl_init(void) {}
void virtio_init(void) {}
void ntsync_init(void) {}
void run_kernel_tests(void) {}
void console_register_devfs(void) {}
void pmm_reclaim_setup(void) {}
void pmm_reclaim_range(uint32_t s, uint32_t e) { (void)s; (void)e; }
void ldt_activate(process_t *p) { (void)p; }
// smp_get_cpu_count was duplicated at line 171 and 172. 
// I will remove the one I added or the previous one.
// The previous replace (step 28692) left:
// int smp_get_cpu_count(void) { return 1; }
// int smp_get_cpu_count(void) { return 1; }
// I'll remove one.
// Also removing the extra include if any.
char kernel_hostname[64] = "host";
void fork_child_return(void) {}
int smp_get_cpu_count(void) { return 1; }

// New Mocks for Host Tests
#include <sys/exec.h>
#include <sys/types.h>

#ifndef _BOOLEAN_T_DEFINED
typedef int boolean_t;
#define _BOOLEAN_T_DEFINED
#endif

int pmap_is_referenced(pmap_t pmap, uint32_t va) { (void)pmap; (void)va; return 0; }
void pmap_clear_reference(pmap_t pmap, uint32_t va) { (void)pmap; (void)va; }
void pmap_copy_page(uintptr_t src, uintptr_t dst) { (void)src; (void)dst; }
void pmap_zero_page(uintptr_t pa) { (void)pa; }
void pmap_remove(pmap_t pmap, uint32_t va) { (void)pmap; (void)va; }

int syscall_trace_enabled(int syscall_num) { (void)syscall_num; return 0; }
void sched_update_loadavg(void) {}
struct pgrp *pgrp_find(pid_t pgid) { (void)pgid; return NULL; }
int pgrp_signal(struct pgrp *pgrp, int sig, int check_session) { (void)pgrp; (void)sig; (void)check_session; return 0; }
int pgrp_is_orphaned(struct pgrp *pgrp) { (void)pgrp; return 0; }
struct personality *perso_lookup(int id) { (void)id; return &personality_native; }
void sendsig(void *sf, struct process *p, int sig) { (void)sf; (void)p; (void)sig; }

const uint8_t sigprop[32] = {0}; // Mock array

int exec_dispatch(const char *path, char *const argv[], char *const envp[]) { (void)path; (void)argv; (void)envp; return 0; }

// FS init stubs
void udf_init() {}
void devfs_init() {}
void procfs_init() {}
void sysfs_init() {}
void pseudo_init() {}
void full_init() {}
void fuse_init() {}
void fuse_fs_init() {}
void p9_init() {}
void pipe_create() {}

struct fs_node *devfs_root_node_ptr;
struct nameidata;
int namei(const char *path, struct nameidata *ndp) { (void)path; (void)ndp; return -1; }
struct vnode;
void vput(struct vnode *vp) { (void)vp; }
void vrele(struct vnode *vp) { (void)vp; }

// vm_map_lookup stub if missing (usually in vm_map.c but we use vm_map_host.c)
struct vm_map_entry *vm_map_lookup_entry(struct vm_map *map, uint32_t vaddr) { (void)map; (void)vaddr; return NULL; }

struct vm_map_entry;
vm_map_entry_t *vm_map_lookup(vm_map_t *map, uintptr_t va) {
    (void)map; (void)va;
    return NULL;
}

// vm_pager stubs
int vm_pager_has_page(void *obj, uint32_t offset) { (void)obj; (void)offset; return 0; }
int vm_pager_get_pages(void *obj, void **m, int count, int *reqpage) { (void)obj; (void)m; (void)count; (void)reqpage; return 0; }
void vm_pager_put_pages(void *obj, void **m, int count, int flags, int *rtvals) { (void)obj; (void)m; (void)count; (void)flags; (void)rtvals; }

// vm_phys stubs
void vm_phys_get_free(uint64_t *free) { *free = 0; }
void vm_phys_get_used(uint64_t *used) { *used = 0; }
void sched_get_system_load(uint32_t *loads) { loads[0]=0; loads[1]=0; loads[2]=0; }

// Missing mocks
#include <stdlib.h>
#include <stdint.h>
// pmm_alloc_block is already defined above? Let's check.
// Error said: mocks.c:243:7: error: redefinition of ‘pmm_alloc_block’
// mocks.c:65:7: note: previous definition...
// So I should NOT define it here if it's already at line 65.
// I will check line 65 content via view_file if needed, but I'll assume I should just remove this one.

// uint32_t pmm_get_page(void) { return (uint32_t)malloc(4096); } 
// Fix cast:
uint32_t pmm_get_page(void) { return (uint32_t)(uintptr_t)malloc(4096); }

void uma_reclaim(void) {}
int libc_rand(void) { return rand(); }
void *libc_malloc(size_t size) { return malloc(size); }
void test_libc_strlen(void) {}
void test_libc_memmove(void) {}
void sched_smp_init(int n) { (void)n; }
