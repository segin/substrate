#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>
#include "../sys/arch/i386/pmap.h"
#include "../sys/arch/i386/cpu.h"
#include "../sys/vm/vm_map.h"
#include "../sys/include/sys/lock.h"
#include "../sys/include/sys/proc.h"
/* Types the mocks below take or dereference: struct bio_stats, blkdev_t,
 * memtrack_rec_t.  Included rather than hand-declared so the mocks keep
 * tracking the real definitions. */
#include "../sys/vfs/buf.h"
#include "../sys/drivers/storage/blkdev.h"
#include "../sys/kern/memtrack.h"

// VGA/UART Mocks
void vga_write(const char *s, size_t n) { (void)s; (void)n; }
void uart_write(const char *s, size_t n) { (void)s; (void)n; }
void vga_init() {}
int uart_init(void) { return 0; }
void keyboard_handler(void *regs) { (void)regs; }
void mouse_handler(void *regs) { (void)regs; }

// FS init mocks
__attribute__((weak)) void ext2_init() {}
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
void devfs_init(void) {}
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
#include <sys/session.h>
#include <pm/pm.h>
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
int pmap_test_and_clear_ref(struct vm_page *m) { (void)m; return 0; }
int pmap_is_modified_range(pmap_t pmap, uintptr_t sva, uintptr_t eva) { (void)pmap; (void)sva; (void)eva; return 0; }
int pmap_test_and_clear_modify(struct vm_page *m) { (void)m; return 0; }

void pmap_copy_page(uintptr_t src, uintptr_t dst) { (void)src; (void)dst; }
void pmap_zero_page(uintptr_t pa) { (void)pa; }
void pmap_remove(pmap_t pmap, uintptr_t va) { (void)pmap; (void)va; }

struct pgrp *pgrp_find(pid_t pgid) {
    for (int pid = 0; pid < 256; pid++) {
        process_t *proc = proc_find(pid);
        if (proc && proc->p_pgrp && proc->p_pgrp->pg_id == pgid) {
            return proc->p_pgrp;
        }
    }
    return NULL;
}

void pgrp_signal(struct pgrp *pgrp, int sig) {
    struct process *proc = pgrp ? pgrp->pg_members : NULL;
    while (proc) {
        psignal(proc, sig);
        proc = proc->p_pgrp_link;
    }
}
int pgrp_is_orphaned(struct pgrp *pgrp) { (void)pgrp; return 0; }
void pgrp_remove_proc(struct process *proc) { (void)proc; }
int sched_interactivity_boost(thread_t *t) { (void)t; return 0; }
struct personality *perso_lookup(int id) { (void)id; return &personality_native; }
/* Signature must track sys/include/sys/signal.h -- a mock that disagrees
 * with the header it stands in for is a conflicting definition at best
 * and a silently wrong ABI at worst. */
void sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs)
{ (void)handler; (void)sig; (void)mask; (void)flags; (void)regs; }

const uint8_t sigprop[NSIG] = {0}; 

int exec_dispatch(const char *path, char *const argv[], char *const envp[]) { (void)path; (void)argv; (void)envp; return 0; }

struct fs_node *devfs_root_node_ptr;
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

void sched_get_system_load(uint32_t *loads) { loads[0]=0; loads[1]=0; loads[2]=0; }


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

uint32_t pmm_get_total_memory(void) { return 0; }
uint32_t pmm_get_free_memory(void) { return 0; }
int cmdline_get(const char *key, char *buf, size_t buf_len)
{ (void)key; if (buf && buf_len > 0) buf[0] = '\0'; return -1; }

void swapper_request_work(void) {}
int pmap_page_is_referenced(struct vm_page *m) { (void)m; return 0; }
void pmap_page_clear_reference(struct vm_page *m) { (void)m; }
static vm_page_t mock_vm_page_pool[8];
static int mock_vm_page_pool_idx = 0;
void *vm_phys_alloc_page(void) {
    if (mock_vm_page_pool_idx >= 8) return NULL;
    vm_page_t *p = &mock_vm_page_pool[mock_vm_page_pool_idx++];
    memset(p, 0, sizeof(*p));
    p->magic_head = p->magic_tail = VM_PAGE_MAGIC;
    return p;
}
void vm_phys_free_page(void *p) { (void)p; }
void cpuid_init(void) {}
void pty_init(void) {}
blkdev_t *blkdev_get(const char *name) { (void)name; return NULL; }
void arch_fork_with_stack(void) {}
void exec_pin_current_thread(void) {}
void exec_unpin_current_thread(void) {}
char mock_percpu_data[8192];
void *percpu_get(void) { return &mock_percpu_data; }
int percpu_get_cpu_id(void) { return 0; }
int sched_can_run_on_cpu(void) { return 1; }
void host_wait_for_interrupt(void) {}
__attribute__((weak)) void vm_map_destroy(vm_map_t *map) { (void)map; }
int cmdline_get_full(char *buf, size_t buf_len)
{ if (buf && buf_len > 0) buf[0] = '\0'; return -1; }

void wait_for_interrupt() {}

int cmdline_debug_enabled(const char *subsystem) { (void)subsystem; return 0; }

void vm_map_lock_read(vm_map_t *map) { (void)map; }
void vm_map_unlock_read(vm_map_t *map) { (void)map; }
int hw_text_tick_1hz(void) { return 0; }
void core_prepare_dump(struct process *p, int sig) { (void)p; (void)sig; }
int coredump(struct process *p) { (void)p; return -1; }
vm_map_t *vm_map_fork(vm_map_t *src_map, pmap_t dst_pmap) { (void)src_map; (void)dst_pmap; return NULL; }
void resource_dump(void *ctx) { (void)ctx; }
void pci_dump_devices(void *ctx) { (void)ctx; }
void bus_dump_tree(void *ctx) { (void)ctx; }
void kobject_uevent_dump(void *ctx) { (void)ctx; }

// Missing mocks restored
void sched_get_loadavg(unsigned long loads[3]) { loads[0] = loads[1] = loads[2] = 0; }
uint32_t sched_count_runnable(void) { return 0; }
uint32_t sched_count_threads(void) { return 0; }
int sys_pmap_stats(struct pmap_stats *stats) { (void)stats; return 0; }
void core_capture_trapframe(void *ctx) { (void)ctx; }
void ldt_init_process(struct process *p) { (void)p; }
void ldt_clone_process(struct process *parent, struct process *child) { (void)parent; (void)child; }
void ldt_free_process(struct process *p) { (void)p; }
void i386_cpu_init_early(void) {}
static const struct i386_cpu_features mock_cpu_features = {
    .detected = 1,
    .is_486_or_newer = 1,
    .has_cpuid = 1,
    .has_cr4 = 1,
    .has_tsc = 1,
    .has_apic = 1,
    .has_pse = 1,
    .has_pae = 1,
    .has_pge = 1,
    .has_fxsr = 1,
};
const struct i386_cpu_features *i386_cpu_get_features(void) { return &mock_cpu_features; }
int i386_cpu_is_486_or_newer(void) { return 1; }
int i386_cpu_has_cpuid(void) { return 1; }
int i386_cpu_has_cr4(void) { return 1; }
int i386_cpu_has_tsc(void) { return 1; }
int i386_cpu_has_apic(void) { return 1; }
int i386_cpu_has_pse(void) { return 1; }
int i386_cpu_has_pae(void) { return 1; }
int i386_cpu_has_pge(void) { return 1; }
int i386_cpu_has_fxsr(void) { return 1; }
int i386_cpu_has_pcid(void) { return 0; }
int i386_cpu_has_rdrand(void) { return 0; }
int i386_cpu_has_rdseed(void) { return 0; }
uint64_t i386_cpu_cycle_counter(void) { return 0; }
void i386_cpu_cycle_counter_split(uint32_t *lo, uint32_t *hi) {
    if (lo) *lo = 0;
    if (hi) *hi = 0;
}

/* ---- Lock / rwlock mocks ---- */
__attribute__((weak)) void rwlock_init(rwlock_t *rw, const char *name) { (void)rw; (void)name; }
void rw_rlock(rwlock_t *rw) { (void)rw; }
bool rw_try_rlock(rwlock_t *rw) { (void)rw; return true; }
void rw_runlock(rwlock_t *rw) { (void)rw; }
void rw_wlock(rwlock_t *rw) { (void)rw; }
bool rw_try_wlock(rwlock_t *rw) { (void)rw; return true; }
void rw_wunlock(rwlock_t *rw) { (void)rw; }
bool rw_wowned(rwlock_t *rw) { (void)rw; return false; }

void lockinit(struct lock *lkp, int prio, const char *name, int flags) {
    (void)prio; (void)name; (void)flags;
    if (lkp) { lkp->lk_flags = 0; lkp->lk_sharecount = 0; lkp->lk_exclusivecount = 0; }
}
void lockdestroy(struct lock *lkp) { (void)lkp; }
int lockmgr(struct lock *lkp, uint32_t flags, spinlock_t *interlock) {
    (void)interlock;
    if (!lkp) return -1;
    if (flags & LK_RELEASE) {
        if (lkp->lk_exclusivecount > 0)
            lkp->lk_exclusivecount--;
        else if (lkp->lk_sharecount > 0)
            lkp->lk_sharecount--;
    } else if (flags & LK_EXCLUSIVE) {
        lkp->lk_exclusivecount++;
    } else if (flags & LK_SHARED) {
        lkp->lk_sharecount++;
    }
    return 0;
}
int lockstatus(struct lock *lkp) {
    if (!lkp) return 0;
    if (lkp->lk_exclusivecount > 0) return LK_EXCLUSIVE;
    if (lkp->lk_sharecount > 0) return LK_SHARED;
    return 0;
}
int lockcount(struct lock *lkp) {
    if (!lkp) return 0;
    return (int)(lkp->lk_exclusivecount + lkp->lk_sharecount);
}

/* ---- Misc kernel stubs needed by vfs.c / kthread.c / time.c ---- */
process_t *swapper_get_proc(void) { return NULL; }
void bio_init(void) {}
void hw_text_tick(void) {}
void fb_console_tick(void) {}
void floppy_poll(void) {}
void vt_tick_1hz(void) {}
int pmap_protect(pmap_t pmap, uintptr_t sva, uintptr_t eva, uint32_t prot) {
    (void)pmap; (void)sva; (void)eva; (void)prot;
    return 0;
}

/* Buffer cache / zone stubs */
int binval_vnode(struct vnode *vp, int save) { (void)vp; (void)save; return 0; }
void uma_zdestroy(uma_zone_t *zone) { (void)zone; }
void random_on_exec(void) {}
void random_reseed_on_fork(int child_pid) { (void)child_pid; }
void i386_load_gs_for_thread(thread_t *t) { (void)t; }
const char *perso_name(int id) { (void)id; return "unknown"; }

/* ------------------------------------------------------------------------
 * Kernel surface the host tests link against but do not exercise.
 *
 * test_procfs.c #includes sys/fs/procfs.c, and procfs reports on very nearly
 * everything, so the link drags in the block layer, the pmap counters, the
 * commit accounting, the console log, the SysV IPC cleanup hooks and more.
 * None of it is under test here; these exist so the link resolves.
 *
 * Every signature below is copied from the header that declares it.  A mock
 * that disagrees with its header is worse than a missing one -- see the
 * sendsig drift that stopped this build earlier -- so if one of these starts
 * failing to compile, fix the mock to match the header rather than the other
 * way round.
 * --------------------------------------------------------------------- */

/* --- block layer / buffer cache (sys/vfs/buf.h, drivers/storage/blkdev.h) */
void bio_get_stats(struct bio_stats *out)
{ if (out) memset(out, 0, sizeof(*out)); }
size_t bio_reclaim(size_t target_bytes) { (void)target_bytes; return 0; }
int bufsync(int freq) { (void)freq; return 0; }
blkdev_t *blkdev_first(void) { return NULL; }
int blkdev_flush_all(void) { return 0; }
void blkdev_invalidate_node(fs_node_t *node) { (void)node; }
size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer)
{ (void)dev; (void)offset; (void)size; (void)buffer; return 0; }

/* --- console / kernel log (drivers/console/console.h) --- */
fs_node_t *console_get_node(void) { return NULL; }
int console_revoke(void) { return 0; }
size_t klog_size(void) { return 0; }
size_t klog_read(char *dst, size_t dstlen)
{ if (dst && dstlen) dst[0] = '\0'; return 0; }

/* --- checksums (include/crc16.h, include/crc32c.h) --- */
uint16_t crc16_update(uint16_t crc, const void *data, size_t len)
{ (void)data; (void)len; return crc; }
uint32_t crc32c_update(uint32_t crc, const void *buf, size_t len)
{ (void)buf; (void)len; return crc; }

/* --- misc kernel services --- */
int cmdline_has(const char *key) { (void)key; return 0; }
void device_shutdown_all(void) { }
void exec_cleanup_drain(void) { }
void syscall_stats_dump(int reset) { (void)reset; }
void sysv_init(void) { }
int i386_cpu_pat_wc_enabled(void) { return 0; }
int random_get_bytes_flags(void *buf, size_t len, unsigned int flags)
{ (void)flags; if (buf && len) memset(buf, 0, len); return 0; }

/* --- ext2 (fs/ext2/ext2.h) --- */
int ext2_htree_hash(const char *name, int len, const uint32_t *hash_seed,
                    int hash_version, uint32_t *hash_major, uint32_t *hash_minor)
{ (void)name; (void)len; (void)hash_seed; (void)hash_version;
  if (hash_major) *hash_major = 0; if (hash_minor) *hash_minor = 0; return 0; }
int ext2_xattr_get(fs_node_t *node, const char *full_name, void *out,
                   size_t out_size, size_t *result_size)
{ (void)node; (void)full_name; (void)out; (void)out_size;
  if (result_size) *result_size = 0; return -1; }
int ext2_xattr_list(fs_node_t *node, void *out, size_t out_size, size_t *result_size)
{ (void)node; (void)out; (void)out_size; if (result_size) *result_size = 0; return -1; }

/* --- FPU (arch/i386/fpu/fpu_emu.h) --- */
void fpu_forget_process(struct process *p) { (void)p; }
void fpu_switch(void) { }

/* --- umtx (include/sys/umtx.h) --- */
int kern_umtx_op(void *obj, int op, unsigned long val, void *uaddr, void *uaddr2)
{ (void)obj; (void)op; (void)val; (void)uaddr; (void)uaddr2; return 0; }
int kern_umtx_wake(void *uaddr, int n) { (void)uaddr; (void)n; return 0; }

/* --- per-process IPC teardown hooks --- */
void ksem_proc_cleanup(int pid) { (void)pid; }
void mq_proc_cleanup(int pid) { (void)pid; }
void sem_proc_cleanup(int pid) { (void)pid; }
void shm_proc_cleanup(int pid) { (void)pid; }

/* --- memtrack (kern/memtrack.h) --- */
size_t memtrack_snapshot(memtrack_rec_t *out, size_t max)
{ (void)out; (void)max; return 0; }

/* --- arch entry points (arch/i386/intr.h, percpu.h) --- */
void new_kernel_thread_trampoline(void) { }
void new_user_thread_trampoline(void) { }
struct percpu_data *percpu_get_cpu(int cpu_id) { (void)cpu_id; return NULL; }

/* --- pmap (arch/i386/pmap.h) --- */
void pmap_clear_modify(pmap_t pmap, uintptr_t va) { (void)pmap; (void)va; }
int pmap_is_modified(pmap_t pmap, uintptr_t va) { (void)pmap; (void)va; return 0; }
uint32_t pmap_resident_count(pmap_t pmap) { (void)pmap; return 0; }
size_t pmap_copyin_other(pmap_t pmap, uintptr_t uva, void *dst, size_t len)
{ (void)pmap; (void)uva; (void)dst; (void)len; return 0; }

/* procfs prints these counters; they are plain statistics. */
uint64_t pmap_create_calls;
uint64_t pmap_destroy_calls;
uint64_t pmap_destroy_skip_refcnt;
uint64_t pmap_destroy_skip_wired;
uint64_t pmap_destroy_skip_obj;
uint64_t pmap_destroy_anon_skipped;
uint64_t pmap_destroy_anon_freed;
uint64_t pmap_destroy_anon_rc0;
uint64_t pmap_destroy_anon_rc2;
uint64_t pmap_destroy_anon_rc_big;

/* --- pty (drivers/console/pty.h) --- */
int pty_bsd_master_open(struct fs_node *node) { (void)node; return -1; }
int pty_is_bsd_master(struct fs_node *node) { (void)node; return 0; }
int pty_set_nonblock(struct fs_node *node, int on) { (void)node; (void)on; return 0; }

/* --- scheduler reboot/halt hooks (kern/sched.h) --- */
void sched_halt_userspace(thread_t *keep) { (void)keep; }
void sched_park_if_reboot_frozen(void) { }

/* --- shmfs (vfs/vfs.h) --- */
void shmfs_init(void) { }
uint64_t shmfs_resident_bytes(void) { return 0; }

/* --- sockets --- */
int sys_accept(int s, void *name, int *namelen)
{ (void)s; (void)name; (void)namelen; return -1; }

/* --- tty (include/sys/tty.h) --- */
int tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg)
{ (void)tty; (void)cmd; (void)arg; return -1; }
int tty_revoke(struct tty *tty) { (void)tty; return 0; }

/* --- commit accounting (vm/vm_commit.h) --- */
int vm_commit_charge(size_t npages) { (void)npages; return 0; }
void vm_commit_uncharge(size_t npages) { (void)npages; }
size_t vm_commit_current(void) { return 0; }
size_t vm_commit_limit(void) { return 0; }

/* --- vm_map (vm/vm_map.h) --- */
void vm_map_lock(vm_map_t *map) { (void)map; }
void vm_map_unlock(vm_map_t *map) { (void)map; }
int vm_map_protect(vm_map_t *map, uintptr_t start, uintptr_t end, uint8_t prot)
{ (void)map; (void)start; (void)end; (void)prot; return 0; }
unsigned long vm_map_destroy_count;
unsigned long vm_map_destroy_entries;

/*
 * Real one lives in sys/vm/phys_mem.c, which the host tests do not build.
 * It answers "is this pointer really one of the vm_page_t's in the page
 * array"; the tests hand out pages from their own mock storage, which would
 * never pass the real bounds check, so say yes and leave their behaviour as
 * it was before the check existed.
 */
int vm_phys_page_is_valid(const vm_page_t *p) { return p != NULL; }

/* --- virtual terminals (include/sys/vt.h) --- */
int vt_get_active(void) { return 0; }
void vt_release_graphics_on_exit(void *exiting_process) { (void)exiting_process; }
