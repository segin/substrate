#ifndef _SYS_PM_H
#define _SYS_PM_H

#include <sys/proc.h>

#include <sys/lock.h>

/*
 * Process table layout
 * ====================
 *
 * Every process_t is kmalloc'd individually at proc_create() time and
 * linked into two structures:
 *
 *   allproc       — singly-linked list of every live process (head
 *                   first), walked by `FOREACH_PROC` and the iterator
 *                   helpers below.
 *   pid_hash[]    — open-chained hash table for O(1) proc_find(pid).
 *
 * Both are protected by pm.c's pid_lock.  The kernel/swapper (PID 0)
 * is bootstrapped by sched_init() — it's the very first kmalloc'd
 * process_t and lives at the tail of allproc for its whole lifetime.
 */

/* current_process is a per-CPU macro defined in <sys/proc.h> (included above). */
extern process_t *kernel_process;
extern mutex_t proctree_lock;

void pm_init(void);
process_t *proc_create(int perso_id);
int proc_fork(process_t *parent, void *stack);
int proc_vfork(process_t *parent, void *stack);
void proc_remove_child(process_t *parent, process_t *child);
int proc_begin_vfork(process_t *child);
void proc_vfork_done(process_t *child);

/*
 * Bootstrap path used by sched_init() exactly once, while it is
 * setting up the swapper.  Returns a fully-linked process_t with the
 * given pid, perso_id, and is_kernel_task=1 — caller fills in pmap,
 * comm, root_node etc.
 */
process_t *proc_bootstrap_kernel(int pid, int perso_id);

/*
 * Release a process_t.  Caller is responsible for tearing down all
 * its resources (pmap, vm_map, FDs, threads, etc.) first; this only
 * unlinks from allproc / pid_hash and kfree's the struct.
 */
void proc_destroy(process_t *p);
void proc_registry_lock(void);
void proc_registry_unlock(void);

void proc_set_bitness(process_t *p, uint8_t bitness);
uint8_t proc_get_bitness(process_t *p);
void proc_capture_cmdline(process_t *p, char *const argv[]);
size_t proc_emit_cmdline(const process_t *p, char *buf, size_t buf_len, size_t *argc_out);
int proc_alloc_fd_from(process_t *p, int start);
int proc_fcntl(process_t *p, int fd, int cmd, int arg);
/* Release all POSIX advisory record locks on an open file description
 * (called from file_free when the last reference is dropped). */
void advlock_release_file(struct file *f);
/* Release only `owner`'s advisory record locks on an open file description,
 * leaving other owners' locks intact.  Called on the close path (even when
 * the description stays alive via a fork/dup share) and for every open fd of
 * an exiting process, so a process's locks never outlive its use of the file. */
void advlock_release_by_owner(struct file *f, int owner);
int proc_fd_set_nonblock(int fd, int on);
void proc_close_cloexec(process_t *p);

/*
 * proc_find - O(1) PID lookup via pid_hash[]
 *
 * Returns the process_t* for the given PID or NULL.  The returned
 * pointer is valid until the process is destroyed (proc_destroy);
 * hold proctree_lock if you need to dereference across a possible
 * concurrent exit.
 */
process_t *proc_find(int pid);

/*
 * Iteration over allproc.  proc_first() returns the head, proc_next()
 * follows the link.  Safe against concurrent insertion (insertion is
 * at head); a destruction during walk requires the caller to hold
 * proctree_lock or otherwise serialize.
 */
process_t *proc_first(void);
process_t *proc_next(process_t *p);

#define FOREACH_PROC(var) \
    for (process_t *var = proc_first(); (var) != NULL; (var) = proc_next(var))

int proc_get_last_pid(void);
void proc_reap_autoreap_zombies(void);

/*
 * Release everything userspace holds, on the way to reboot: every process's
 * open descriptors, its cwd/root references, and (for all but `keep`) its
 * address space, whose vnode-pager references are what pin mapped
 * executables and libraries.  Call after sched_halt_userspace() and before
 * vfs_unmount_all(), so the filesystems come down unreferenced.
 * `keep` -- the process calling reboot -- retains its address space.
 */
void proc_teardown_userspace(process_t *keep);

void rusage_init(process_t *p);
void rusage_finalize(process_t *p);
void proc_exit(int code);

#endif
