/*
 * Shared kernel stubs for the standalone process host tests.
 *
 * host_test_proc_exit.c and host_test_proc_lifecycle.c each build
 * sys/pm/process.c (and wait.c, sleepq.c, ...) natively and link them alone.
 * proc_exit() is the process teardown path, so it touches nearly every
 * subsystem on its way out -- the block layer, ext2's statistics counters,
 * the name cache, POSIX/SysV IPC cleanup, the pager, procfs, the FPU, the
 * preemption counters -- none of which either test exercises.
 *
 * Everything here is weak, so a test that wants real behaviour (or already
 * defines the symbol itself) simply provides its own and wins the link.
 *
 * Signatures come from the declaring header, named in the comment on each
 * line.  Keep them matched: a stub that disagrees with its header is a
 * conflicting definition of the thing it stands in for.
 */

#include <stddef.h>
#include <stdint.h>

#include "../../sys/include/sys/proc.h"
#include "../../sys/kern/sched.h"
#include "../../sys/vfs/buf.h"
#include "../../sys/vfs/vfs.h"

__attribute__((weak)) void bio_get_stats(struct bio_stats *out) { (void)out; }   /* sys/vfs/buf.h */
__attribute__((weak)) int cmdline_debug_enabled(const char *channel) { (void)channel; return 0; }   /* sys/kern/cmdline.h */
__attribute__((weak)) int copyin(const void *src, void *dst, size_t size) { (void)src; (void)dst; (void)size; return 0; }   /* sys/include/sys/copy.h */
__attribute__((weak)) int copyout(const void *src, void *dst, size_t size) { (void)src; (void)dst; (void)size; return 0; }   /* sys/include/sys/copy.h */
__attribute__((weak)) uint64_t ext2_alloc_node_fail;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) uint64_t ext2_alloc_node_fail_locked;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) uint64_t ext2_alloc_node_fail_pinned;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) uint64_t ext2_alloc_node_hits;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) uint64_t ext2_alloc_node_new;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) uint64_t ext2_finddir_break_block0;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) uint64_t ext2_finddir_break_recv_malformed;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) uint64_t ext2_finddir_calls;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) uint64_t ext2_finddir_dcache_hit;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) uint64_t ext2_finddir_walk_found;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) uint64_t ext2_finddir_walk_missing;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) uint64_t ext2_root_pin_lost;   /* sys/fs/ext2/ext2.h */
__attribute__((weak)) void fpu_forget_process(struct process *p) { (void)p; }   /* sys/arch/i386/fpu/fpu_emu.h */
__attribute__((weak)) unsigned long fs_open_count, fs_close_count;   /* sys/vfs/vfs.h */
__attribute__((weak)) unsigned long fs_open_count;   /* sys/vfs/vfs.c */
__attribute__((weak)) uint64_t get_ticks(void) { return 0; }   /* sys/kern/time.h */
__attribute__((weak)) void kfree(void *ptr, size_t size) { (void)ptr; (void)size; }   /* sys/vm/vm_kmem.h */
__attribute__((weak)) void * kmalloc(size_t size) { (void)size; return NULL; }   /* sys/vm/vm_kmem.h */
__attribute__((weak)) int kprintf(const char *fmt, ...) { (void)fmt; return 0; }   /* sys/drivers/console/console.h */
__attribute__((weak)) void ksem_proc_cleanup(int pid) { (void)pid; }   /* sys/include/sys/posix_sem.h */
__attribute__((weak)) void mq_proc_cleanup(int pid) { (void)pid; }   /* sys/include/sys/mqueue.h */
__attribute__((weak)) unsigned long namecache_enter_count;   /* sys/vfs/vfs.h */
__attribute__((weak)) unsigned long namecache_evict_count;   /* sys/vfs/vfs.h */
__attribute__((weak)) unsigned long namecache_purge_count;   /* sys/vfs/vfs.h */
__attribute__((weak)) int pipe_set_nonblock(struct fs_node *node, int nonblock) { (void)node; (void)nonblock; return 0; }   /* sys/include/kern/file.h */
__attribute__((weak)) uint64_t pmap_create_calls;   /* sys/arch/i386/pmap.h */
__attribute__((weak)) uint64_t pmap_destroy_calls;   /* sys/arch/i386/pmap.h */
__attribute__((weak)) void poll_notify(void *chan) { (void)chan; }   /* sys/kern/sched.h */
__attribute__((weak)) void preempt_disable(void) { }   /* sys/include/sys/preempt.h */
__attribute__((weak)) void preempt_enable_noresched(void) { }   /* sys/include/sys/preempt.h */
__attribute__((weak)) void procfs_release_pid_nodes(int pid) { (void)pid; }   /* sys/fs/procfs.h */
__attribute__((weak)) int pty_set_nonblock(struct fs_node *node, int on) { (void)node; (void)on; return 0; }   /* sys/drivers/console/pty.h */
__attribute__((weak)) int sched_thread_running_remote(thread_t *t) { (void)t; return 0; }   /* sys/kern/sched.h */
__attribute__((weak)) void sem_proc_cleanup(int pid) { (void)pid; }   /* sys/include/sys/sem.h */
__attribute__((weak)) void shm_proc_cleanup(int pid) { (void)pid; }   /* sys/include/sys/shm.h */
__attribute__((weak)) void syscall_stats_dump(int reset) { (void)reset; }   /* sys/include/kern/main.h */
__attribute__((weak)) thread_t * thread_first(void) { return NULL; }   /* sys/kern/sched.h */
__attribute__((weak)) thread_t * thread_next(thread_t *t) { (void)t; return NULL; }   /* sys/kern/sched.h */
__attribute__((weak)) int vfs_cache_count;   /* sys/vfs/vfs.h */
__attribute__((weak)) unsigned long vm_map_destroy_count;   /* sys/vm/vm_map.h */
__attribute__((weak)) unsigned long vm_map_destroy_entries;   /* sys/vm/vm_map.h */
__attribute__((weak)) unsigned long vm_pager_vnode_alloc_count;   /* sys/vm/vm_pager.h */
__attribute__((weak)) unsigned long vm_pager_vnode_dealloc_count;   /* sys/vm/vm_pager.h */
__attribute__((weak)) void vt_release_graphics_on_exit(void *exiting_process) { (void)exiting_process; }   /* sys/include/sys/vt.h */

/* Commit accounting (sys/vm/vm_commit.h) -- proc_exit uncharges on teardown. */
__attribute__((weak)) int vm_commit_charge(size_t npages) { (void)npages; return 0; }
__attribute__((weak)) void vm_commit_uncharge(size_t npages) { (void)npages; }

/*
 * exec.c's format-handler registrations, and the two VFS entry points the
 * exec path uses.  A host test that links exec.c gets these; one that does
 * not simply never references them.
 */
__attribute__((weak)) void aout_init_handler(void) { }
__attribute__((weak)) void xout_init_handler(void) { }
__attribute__((weak)) void xout286_init_handler(void) { }
__attribute__((weak)) int kern_open_exec(const char *path) { (void)path; return -1; }
__attribute__((weak)) int vfs_check_permissions_groups(fs_node_t *node, uint32_t uid,
        uint32_t gid, const uint32_t *groups, int ngroups, int mode)
{ (void)node; (void)uid; (void)gid; (void)groups; (void)ngroups; (void)mode; return 0; }
