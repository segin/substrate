#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_fault.h>
#include <vm/vm_pager.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <vfs/vfs.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <arch/i386/pmm.h>
#include <arch/i386/pmap.h>
#include <vm/vm_kmem.h>
#include <vm/vm_commit.h>
#include <kern/cmdline.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <sys/errno.h>

// mman.h flag definitions (duplicated here for kernel use)
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x001
#define MAP_PRIVATE   0x002
#define MAP_FIXED     0x010
#define MAP_ANONYMOUS 0x020
#define MAP_NORESERVE 0x0040  /* FreeBSD historical value */

#define MCL_FUTURE    0x02    /* mlockall(2): lock future mappings */

static int mmap_validate_flags(int flags) {
    int sharing = flags & (MAP_SHARED | MAP_PRIVATE);
    return sharing == MAP_SHARED || sharing == MAP_PRIVATE;
}

static int vm_round_page_size(size_t length, size_t *aligned_out) {
    size_t aligned_length;

    if (!aligned_out || length == 0) {
        return -1;
    }
    if (length > (size_t)-1 - 0xFFF) {
        return -1;
    }

    aligned_length = (length + 0xFFF) & ~(size_t)0xFFF;
    if (aligned_length == 0) {
        return -1;
    }

    *aligned_out = aligned_length;
    return 0;
}

static int vm_user_range_valid(uintptr_t start, size_t length) {
    if (length == 0) {
        return -1;
    }
    if (start < USER_STACK_MIN || start >= KERN_BASE) {
        return -1;
    }
    if (length > (size_t)(KERN_BASE - start)) {
        return -1;
    }
    return 0;
}

static void brk_unmap_free_pages(pmap_t pmap, uintptr_t start, uintptr_t end) {
    for (uintptr_t va = start; va < end; va += 0x1000) {
        uintptr_t pa = pmap_extract(pmap, va);
        if (pa != 0) {
            pmap_remove(pmap, va);
            pmm_free_block((void *)((pa & ~0xFFFU) + KERN_BASE));
        }
    }
}

/*
 * Backing-object cache key.  It MUST be a stable file identity, not the
 * fs_node_t pointer: ext2 (and other filesystems) hand back freshly
 * allocated / cache-recycled fs_node_t instances on each open, so keying on
 * the pointer makes every process miss the cache and build its own private
 * backing object -- which is why each program got a private physical copy of
 * every shared library (≈10 MB per xterm).  (mount, inode) is stable across
 * opens and unique across mounts, so two processes mapping the same file now
 * share one backing object and therefore the same read-only physical pages.
 */
typedef struct shared_file_object_entry {
    struct mount *mp;
    uint64_t inode;
    uint64_t page_offset;
    vm_object_t *object;
    struct shared_file_object_entry *next;
} shared_file_object_entry_t;

static shared_file_object_entry_t *shared_file_objects;
static spinlock_t shared_file_objects_lock = SPINLOCK_INIT("mmap_shared_fileobj");

/*
 * Weak-reference cache.  Entries point at vm_objects without holding an
 * extra refcount; eviction is driven from vm_object_deallocate() at
 * ref_count == 0 (see vm_syscalls_evict_shared_obj below).  Lookups
 * promote the weak pointer to a real reference via
 * vm_object_try_reference() so a deallocate that has already won the
 * dec-to-0 race cannot be resurrected.
 *
 * Previously this cache held an extra vm_object_reference() per entry
 * and had no eviction path, so every file ever mmap'd accumulated a
 * permanent pin on its fs_node_t through the cached pager — makewhatis
 * over /usr/share/man exhausted the ext2 node cache after a few
 * hundred man pages.
 */
vm_object_t *mmap_get_shared_backing_object(fs_node_t *node, size_t length,
                                             uint32_t vm_prot, uint64_t offset) {
    uint64_t page_offset = offset >> 12;
    shared_file_object_entry_t *entry, *prev;

    spinlock_acquire(&shared_file_objects_lock);
    prev = NULL;
    for (entry = shared_file_objects; entry != NULL; ) {
        if (entry->mp == node->mp && entry->inode == node->inode &&
            entry->page_offset == page_offset) {
            if (vm_object_try_reference(entry->object)) {
                spinlock_release(&shared_file_objects_lock);
                return entry->object;
            }
            /* Object is dying — its dealloc hasn't yet reached the
             * eviction call.  Unlink the stale entry here so we don't
             * keep racing it, then fall through to allocate fresh. */
            shared_file_object_entry_t *stale = entry;
            if (prev) prev->next = entry->next;
            else shared_file_objects = entry->next;
            entry = entry->next;
            kfree(stale, sizeof(shared_file_object_entry_t));
            continue;
        }
        prev = entry;
        entry = entry->next;
    }
    spinlock_release(&shared_file_objects_lock);

    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_VNODE, length);
    if (!obj) {
        return NULL;
    }

    obj->handle = node;
    obj->pager = vm_pager_allocate(VM_OBJ_TYPE_VNODE, node, length, vm_prot, offset);
    if (!obj->pager) {
        vm_object_deallocate(obj);
        return NULL;
    }

    entry = kmalloc(sizeof(shared_file_object_entry_t));
    if (!entry) {
        vm_object_deallocate(obj);
        return NULL;
    }

    entry->mp = node->mp;
    entry->inode = node->inode;
    entry->page_offset = page_offset;
    entry->object = obj;

    spinlock_acquire(&shared_file_objects_lock);
    /* Recheck: another thread may have raced to allocate during our window. */
    for (shared_file_object_entry_t *cur = shared_file_objects; cur != NULL; cur = cur->next) {
        if (cur->mp == node->mp && cur->inode == node->inode &&
            cur->page_offset == page_offset) {
            if (vm_object_try_reference(cur->object)) {
                spinlock_release(&shared_file_objects_lock);
                vm_object_deallocate(obj);
                kfree(entry, sizeof(shared_file_object_entry_t));
                return cur->object;
            }
            /* Lost the race AND the racer's object is dying — unusual
             * but possible.  Fall through and replace the entry. */
        }
    }

    entry->next = shared_file_objects;
    shared_file_objects = entry;

    /* Cache holds a weak pointer only; obj's ref_count is the one the
     * caller will own.  When that ref reaches 0 the deallocate path
     * will call vm_syscalls_evict_shared_obj to unlink this entry. */
    spinlock_release(&shared_file_objects_lock);

    return obj;
}

void vm_syscalls_evict_shared_obj(vm_object_t *object) {
    if (!object) return;
    shared_file_object_entry_t *entry, *prev = NULL;

    spinlock_acquire(&shared_file_objects_lock);
    for (entry = shared_file_objects; entry != NULL; entry = entry->next) {
        if (entry->object == object) {
            if (prev) prev->next = entry->next;
            else shared_file_objects = entry->next;
            spinlock_release(&shared_file_objects_lock);
            kfree(entry, sizeof(shared_file_object_entry_t));
            return;
        }
        prev = entry;
    }
    spinlock_release(&shared_file_objects_lock);
}

static vm_object_t *mmap_create_file_object(fs_node_t *node, size_t length, uint32_t vm_prot,
                                            int flags, uint64_t offset) {
    if (!node) {
        return NULL;
    }

    if (flags & MAP_SHARED) {
        return mmap_get_shared_backing_object(node, length, vm_prot, offset);
    }

    vm_object_t *backing = mmap_get_shared_backing_object(node, length, vm_prot, offset);
    if (!backing) {
        return NULL;
    }

    vm_object_t *obj = vm_object_shadow(backing);
    if (!obj) {
        vm_object_deallocate(backing);
        return NULL;
    }

    vm_object_deallocate(backing);
    return obj;
}

// User Memory System Calls

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint64_t offset) {
    process_t *p = current_process;
    if (!p || !p->vm_map) return (void *)-1;
    /* Invalid flags word (neither -- or both -- of MAP_SHARED/MAP_PRIVATE)
     * is EINVAL.  Returning a bare (void *)-1 would be negated by the libc
     * mmap wrapper into errno=EPERM instead (OPTS mmap/21-1). */
    if (!mmap_validate_flags(flags)) return (void *)(intptr_t)(-EINVAL);

    vm_map_t *map = p->vm_map;
    uintptr_t v_addr = (uintptr_t)addr;
    size_t aligned_length;

    /* Zero length is EINVAL and must be caught before page-rounding, which
     * also rejects 0 but would surface as a bare -1 == EPERM (OPTS mmap/32-1). */
    if (length == 0) return (void *)(intptr_t)(-EINVAL);
    if (vm_round_page_size(length, &aligned_length) != 0) return (void *)-1;

    /* POSIX EINVAL cases that otherwise feed malformed ranges into the
     * file-object / device (/dev/mem) mmap paths: zero length, a
     * non-page-aligned file offset, and offset+length wrapping past 2^64. */
    if (!(flags & MAP_ANONYMOUS) && (offset & 0xFFF)) return (void *)(intptr_t)(-EINVAL);
    if (offset + (uint64_t)aligned_length < offset) return (void *)(intptr_t)(-EOVERFLOW);

    /*
     * A file-backed mapping whose (page-offset + page-length) overflows the
     * 32-bit page-index the mmap(2) ABI carries exceeds the file's offset
     * maximum: POSIX mandates EOVERFLOW (OPTS mmap/31-1).  offset is page-
     * aligned and reaches the kernel as a 32-bit page count shifted up, so
     * offset>>12 is bounded by 0xFFFFFFFF; the sum is what can overflow.
     */
    if (!(flags & MAP_ANONYMOUS) &&
        (offset >> 12) + ((uint64_t)aligned_length >> 12) > 0xFFFFFFFFULL)
        return (void *)(intptr_t)(-EOVERFLOW);

    /*
     * mlockall(MCL_FUTURE) requests that every future mapping be locked in
     * memory.  substrate does not swap (so the lock is a no-op), but POSIX
     * requires EAGAIN when an unprivileged process's mapping cannot be locked
     * because it would exceed RLIMIT_MEMLOCK (OPTS mmap/18-1).  Only
     * unprivileged processes that have called mlockall(MCL_FUTURE) are
     * affected; root and processes without MCL_FUTURE skip this entirely.
     */
    if ((p->mlockall_flags & MCL_FUTURE) && p->euid != 0 &&
        (uint64_t)aligned_length > (uint64_t)p->rlim_memlock_cur)
        return (void *)(intptr_t)(-EAGAIN);

    /*
     * RLIMIT_AS: reject a mapping that would push the process address space
     * over its soft limit.  Only active when a finite limit has been set
     * (default RLIM_INFINITY, no enforcement); enforced against the map's
     * current byte size so an explicit small RLIMIT_AS makes malloc()'s mmap
     * fail with ENOMEM once the limit is reached, instead of consuming all of
     * RAM (OPTS pthread_cond_init/4-1, pthread_mutex_init/5-1).  Internal
     * growth (demand-paged stack, COW) does not pass through here, so it is
     * unaffected.
     */
    if (p->rlim_as_cur != RLIM_INFINITY && p->vm_map &&
        (uint64_t)p->vm_map->size + (uint64_t)aligned_length >
            (uint64_t)p->rlim_as_cur)
        return (void *)(intptr_t)(-ENOMEM);

    /*
     * Validate the descriptor BEFORE touching the address space.  A MAP_FIXED
     * request must leave the caller's existing mappings untouched when it
     * fails (POSIX), so the fd validity (EBADF) and type (ENODEV) checks are
     * hoisted above the MAP_FIXED vm_map_remove below -- otherwise
     * mmap(MAP_FIXED, badfd) would destroy the old mapping and then error.
     */
    file_t *file = NULL;
    if (!(flags & MAP_ANONYMOUS) && fd >= 0 && fd < MAX_FD) {
        file = p->fds[fd];
    }
    /* A file-backed mmap with no valid open descriptor is EBADF, not the
     * bare -1 (EPERM) the caller would otherwise see (OPTS mmap/19-1). */
    if (!(flags & MAP_ANONYMOUS) && (!file || !file->f_data)) return (void *)(intptr_t)(-EBADF);

    /*
     * A pipe or socket is a file type mmap() does not support; POSIX
     * mandates ENODEV (OPTS mmap/23-1).  These node types never carry an
     * ->mmap handler, so this only rejects what would otherwise fall
     * through to the (meaningless) vnode-pager path.  The low 3 bits of
     * fs_node.flags hold the node type (FS_PIPE=5, FS_SOCKET=7).
     */
    if (file && file->f_data) {
        uint32_t ntype = ((fs_node_t *)file->f_data)->flags & 0x07;
        if (ntype == FS_PIPE || ntype == FS_SOCKET)
            return (void *)(intptr_t)(-ENODEV);
    }

    /*
     * POSIX EACCES: the descriptor must be open for reading -- mmap() always
     * reads the object to populate its pages -- and a MAP_SHARED mapping that
     * requests PROT_WRITE additionally requires it be open for writing
     * (OPTS mmap/6-4, mmap/6-6).  A MAP_PRIVATE PROT_WRITE mapping of a
     * read-only descriptor stays allowed: its writes go to the private copy,
     * never back to the file (OPTS mmap/6-5).  Skipped for anonymous maps,
     * which have no backing descriptor.
     */
    if (file && file->f_data) {
        if (!(file->f_flag & FREAD))
            return (void *)(intptr_t)(-EACCES);
        if ((prot & PROT_WRITE) && (flags & MAP_SHARED) &&
            !(file->f_flag & FWRITE))
            return (void *)(intptr_t)(-EACCES);
    }

    // Find virtual address space
    if (v_addr == 0 || !(flags & MAP_FIXED)) {
        /* Insufficient room in the address space is ENOMEM, not the bare -1
         * (EPERM) the caller would otherwise see. */
        if (vm_map_find_space(map, &v_addr, aligned_length) != 0)
            return (void *)(intptr_t)(-ENOMEM);
    } else {
        /* MAP_FIXED with a misaligned address is EINVAL; a range that runs
         * past the process address space is ENOMEM (OPTS mmap/24-2). */
        if (v_addr & 0xFFF) return (void *)(intptr_t)(-EINVAL);
        if (vm_user_range_valid(v_addr, aligned_length) != 0)
            return (void *)(intptr_t)(-ENOMEM);
        // MAP_FIXED: Unmap existing mappings in the range
        if (vm_map_remove(map, v_addr, v_addr + aligned_length) != 0) {
            return (void *)(intptr_t)(-ENOMEM);
        }
    }
    if (vm_user_range_valid(v_addr, aligned_length) != 0)
        return (void *)(intptr_t)(-ENOMEM);

    // Translate prot to VM_PROT_* flags
    uint32_t vm_prot = 0;
    if (prot & PROT_READ)  vm_prot |= VM_PROT_READ;
    if (prot & PROT_WRITE) vm_prot |= VM_PROT_WRITE;
    if (prot & PROT_EXEC)  vm_prot |= VM_PROT_EXEC;

    /* `file` (and the EBADF / ENODEV validation on it) is resolved above,
     * before the MAP_FIXED unmap, so a bad-fd request cannot destroy the
     * caller's existing mappings. */

    /*
     * Device-specific mmap handler.
     *
     * A MAP_PRIVATE mapping of a shared-memory object (an FS_FILE node that
     * ALSO exposes an ->mmap handler, i.e. shmfs) must be copy-on-write: the
     * caller's writes stay private and must NOT reach the underlying object,
     * and after fork() the child's writes must be invisible to the parent
     * (POSIX; OPTS mmap/7-3, fork/16-1).  The device ->mmap handler maps the
     * object's physical frames SHARED, so for MAP_PRIVATE of such a node we
     * fall through to the generic shadow-over-vnode-pager path below, which
     * demand-pages from the object via ->read and copies on write.  Genuine
     * device nodes (framebuffer, /dev/mem, audio) are not FS_FILE and always
     * delegate, exactly as before.
     */
    {
        fs_node_t *dnode = file ? (fs_node_t *)file->f_data : NULL;
        int private_shm = dnode && (flags & MAP_PRIVATE) &&
                          (dnode->flags & 0x07) == FS_FILE;
        if (dnode && dnode->mmap && !private_shm) {
            return dnode->mmap(dnode, addr, length, prot, flags, offset);
        }
    }

    /*
     * Strict commit accounting (no overcommit).  An anonymous PRIVATE
     * mapping grows the address space lazily and is demand-paged, so we
     * must RESERVE its pages now -- otherwise malloc() returns non-NULL
     * with no memory behind it and the process dies on first touch with
     * a non-portable signal.  Charge the whole span against the global
     * commit limit; if it would exceed the limit, fail the mmap with
     * ENOMEM (-> MAP_FAILED in userspace) per POSIX.  MAP_NORESERVE opts
     * out (caller accepts overcommit).  Shared and file-backed mappings
     * are not charged here: SHARED|ANON is shm (its own accounting model)
     * and file-backed pages are reclaimable from their vnode.
     */
    int commit_charged = 0;
    size_t commit_pages = aligned_length / 0x1000;
    int is_anon_private = (flags & MAP_ANONYMOUS) && !(flags & MAP_SHARED) &&
                          !(flags & MAP_NORESERVE) && !file;
    if (is_anon_private) {
        if (vm_commit_charge(commit_pages) != 0) {
            /* Return a negative errno (not bare -1, which the libc mmap
             * wrapper would surface as EPERM); POSIX mandates ENOMEM when
             * memory cannot be committed. */
            return (void *)(intptr_t)(-ENOMEM);
        }
        commit_charged = 1;
    }

    // Create VM object for tracking
    vm_object_t *obj;
    if (file) {
        fs_node_t *fnode = (fs_node_t *)file->f_data;
        obj = mmap_create_file_object(fnode, aligned_length, vm_prot, flags, offset);
    } else {
        obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, aligned_length);
    }
    if (!obj) {
        if (commit_charged) vm_commit_uncharge(commit_pages);
        return (void *)-1;
    }

    // Determine inheritance based on sharing
    uint8_t inheritance = (flags & MAP_SHARED) ? VM_INHERIT_SHARE : VM_INHERIT_COPY;

    /*
     * max_protection must allow a later mprotect() to raise access — POSIX/
     * Linux let a mapping be mprotect()'d to any of R/W/X regardless of the
     * initial prot (e.g. mmap(PROT_NONE) then mprotect(RW), as glibc does for
     * per-thread malloc arenas).  Pinning max_protection to the initial prot
     * (the old behavior) made that mprotect a silent no-op at the vm_map
     * level, so the first write faulted.  Allow R/W/X as the ceiling.
     */
    uint8_t max_prot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXEC;
    if (vm_map_insert(map, obj, 0, v_addr, v_addr + aligned_length, vm_prot, max_prot, inheritance) != 0) {
        vm_object_deallocate(obj);
        if (commit_charged) vm_commit_uncharge(commit_pages);
        return (void *)-1;
    }

    /*
     * Mark the entry so its teardown (vm_map_remove / vm_map_destroy)
     * uncharges exactly (end-start)/PAGE pages.  A private anonymous
     * object is unique per mmap, so this entry never merges with a
     * neighbour (vm_map_entries_mergeable compares object pointers) --
     * the looked-up entry is exactly the one inserted, span == length.
     */
    if (commit_charged) {
        vm_map_lock(map);
        vm_map_entry_t *e = vm_map_lookup(map, v_addr);
        if (e) {
            e->flags |= VME_COMMITTED;
        }
        vm_map_unlock(map);
    }

    return (void *)v_addr;
}

int sys_munmap(void *addr, size_t length) {
    if (!current_process || !current_process->vm_map) return -EINVAL;
    size_t aligned_len;
    /* POSIX EINVAL: len == 0 (or a length that overflows page rounding). */
    if (vm_round_page_size(length, &aligned_len) != 0) return -EINVAL;

    uintptr_t start = (uintptr_t)addr;

    /* POSIX EINVAL: addr not page-aligned, or [addr,addr+len) lies outside
     * the process address space (OPTS munmap/8-1 passes addr == (void *)-1). */
    if (start & 0xFFF) return -EINVAL;
    if (vm_user_range_valid(start, aligned_len) != 0) return -EINVAL;

    if (vm_map_remove(current_process->vm_map, start, start + aligned_len) != 0) {
        return -EINVAL;
    }
    return 0;
}

#include <string.h>

extern pmap_t pmap_kernel(void);

void *sys_brk(void *addr) {
    if (!current_process) return NULL;

    /*
     * Exec paths establish brk_start as the canonical heap floor. If brk has
     * not been materialized yet, recover it lazily from brk_start so the first
     * userspace brk/sbrk query does not observe a transient zero heap pointer.
     */
    if (current_process->brk == 0 && current_process->brk_start != 0) {
        current_process->brk = current_process->brk_start;
    }
    
    // If querying (addr == 0) or uninitialized
    if (!addr || !current_process->brk_start) {
        extern int syscall_trace_enabled;
        if (syscall_trace_enabled || cmdline_debug_enabled("vm:brk")) {
            extern void kprint(const char*);
            char buf[64];
            char *digits = "0123456789ABCDEF";
            uintptr_t val = (uintptr_t)current_process->brk;
            kprint("BRK: Query/Early returning 0x");
            for(int i=0;i<8;i++) buf[7-i] = digits[(val>>(i*4))&0xF];
            buf[8] = '\n'; buf[9]=0;
            kprint(buf);
        }
        return (void *)(uintptr_t)current_process->brk;
    }

    uintptr_t new_brk = (uintptr_t)addr;
    uintptr_t old_brk = (uintptr_t)current_process->brk;

    // Don't shrink below start
    if (new_brk < current_process->brk_start) 
        return (void *)(uintptr_t)old_brk;

    // Don't allow mapping into kernel address space
    if (new_brk >= 0xC0000000)
        return (void *)(uintptr_t)old_brk;

    if (old_brk + 0xFFF < old_brk || new_brk + 0xFFF < new_brk)
        return (void *)(uintptr_t)old_brk;

    // Align to page boundaries
    uintptr_t old_page_end = (old_brk + 0xFFF) & ~0xFFFULL;
    uintptr_t new_page_end = (new_brk + 0xFFF) & ~0xFFFULL;


    if (new_page_end > old_page_end) {
        /*
         * Strict commit accounting: reserve the heap pages we are about
         * to grow into BEFORE allocating them.  If the reservation would
         * exceed the commit limit, refuse to grow and return the old
         * break -- glibc/musl malloc treats "brk did not advance" as
         * ENOMEM and returns NULL, which is the POSIX-correct behaviour.
         */
        size_t grow_pages = (new_page_end - old_page_end) / 0x1000;
        if (vm_commit_charge(grow_pages) != 0) {
            extern int syscall_trace_enabled;
            if (syscall_trace_enabled || cmdline_debug_enabled("vm:brk")) {
                extern void kprint(const char*);
                kprint("BRK: commit limit reached -> ENOMEM\n");
            }
            return (void *)(uintptr_t)old_brk;
        }

        // Allocate and map new pages in batches
        #define BRK_BATCH_SIZE 256
        uintptr_t pa_batch[BRK_BATCH_SIZE];
        uintptr_t va = old_page_end;
        pmap_t brk_pmap = current_process->pmap ? (pmap_t)current_process->pmap : pmap_kernel();

        while (va < new_page_end) {
            int batch_count = 0;
            uintptr_t batch_va_start = va;

            // Fill batch
            while (batch_count < BRK_BATCH_SIZE && va < new_page_end) {
                void *pa_virt = pmm_alloc_block();
                if (!pa_virt) {
                     // Cleanup current batch (not mapped yet)
                     for (int k = 0; k < batch_count; k++) {
                         pmm_free_block((void*)(pa_batch[k] + 0xC0000000));
                     }
                     brk_unmap_free_pages(brk_pmap, old_page_end, batch_va_start);
                     vm_commit_uncharge(grow_pages); /* grow aborted */
                     extern int syscall_trace_enabled;
                     if (syscall_trace_enabled || cmdline_debug_enabled("vm:brk")) {
                         extern void kprint(const char*);
                         kprint("BRK: pmm_alloc failed!\n");
                     }
                     return (void *)(uintptr_t)old_brk; // Out of memory
                }
                pa_batch[batch_count++] = (uintptr_t)pa_virt - 0xC0000000;

                // Zero page immediately (warm cache)
                memset(pa_virt, 0, 0x1000);

                va += 0x1000;
            }
            
            // Map batch
            if (pmap_enter_batch(brk_pmap, batch_va_start, batch_count, pa_batch,
                                 VM_PROT_READ | VM_PROT_WRITE, 0) < 0) {
                 for (int k = 0; k < batch_count; k++) {
                     uintptr_t page_va = batch_va_start + ((uintptr_t)k * 0x1000);
                     uintptr_t mapped_pa = pmap_extract(brk_pmap, page_va) & ~0xFFFU;
                     if (mapped_pa == pa_batch[k]) {
                         pmap_remove(brk_pmap, page_va);
                     }
                     pmm_free_block((void *)(pa_batch[k] + KERN_BASE));
                 }
                 brk_unmap_free_pages(brk_pmap, old_page_end, batch_va_start);
                 vm_commit_uncharge(grow_pages); /* grow aborted */
                 extern int syscall_trace_enabled;
                 if (syscall_trace_enabled || cmdline_debug_enabled("vm:brk")) {
                     extern void kprint(const char*);
                     kprint("BRK: pmap_enter_batch failed!\n");
                 }
                 return (void *)(uintptr_t)old_brk;
            }
        }
    }

    /*
     * Keep the strict-commit accounting in step with the heap span.
     *   - grow: the charge was already taken (and the pages allocated)
     *     in the grow block above; record it in brk_committed.
     *   - shrink: release both the commit reservation AND the physical
     *     pages for the range we are giving back.  The old code "leaked"
     *     shrunk pages (lazy unmap); under strict accounting that double-
     *     counts memory, so we now actually unmap+free them and uncharge.
     */
    if (new_page_end > old_page_end) {
        current_process->brk_committed += (new_page_end - old_page_end) / 0x1000;
    } else if (new_page_end < old_page_end) {
        size_t shrink_pages = (old_page_end - new_page_end) / 0x1000;
        pmap_t brk_pmap = current_process->pmap ? (pmap_t)current_process->pmap
                                                : pmap_kernel();
        brk_unmap_free_pages(brk_pmap, new_page_end, old_page_end);
        vm_commit_uncharge(shrink_pages);
        if (current_process->brk_committed >= shrink_pages)
            current_process->brk_committed -= shrink_pages;
        else
            current_process->brk_committed = 0;
    }

    current_process->brk = (uint32_t)new_brk;
    
    // Debug print for success (restored and gated)
    extern int syscall_trace_enabled;
    if (syscall_trace_enabled || cmdline_debug_enabled("vm:brk")) {
        extern void kprint(const char*);
        char buf[64];
        char *digits = "0123456789ABCDEF";
        uintptr_t val = (uintptr_t)new_brk;
        kprint("BRK: Returning 0x");
        for(int i=0;i<8;i++) buf[7-i] = digits[(val>>(i*4))&0xF];
        buf[8] = '\n'; buf[9]=0;
        kprint(buf);
    }
    
    return (void *)(uintptr_t)new_brk;
}

// msync flags (from sys/mman.h)
#define MS_ASYNC      1
#define MS_SYNC       2
#define MS_INVALIDATE 4

int sys_msync(void *addr, size_t length, int flags) {
    if (!current_process || !current_process->vm_map) return -1;
    if (length == 0) return 0;
    
    (void)flags;  // Treat all as synchronous for now
    
    vm_map_t *map = current_process->vm_map;
    uintptr_t start = (uintptr_t)addr & ~0xFFF;
    uintptr_t end = ((uintptr_t)addr + length + 0xFFF) & ~0xFFF;
    vm_map_lock_read(map);
    
    // Walk the range and flush dirty pages
    for (uintptr_t va = start; va < end; va += 0x1000) {
        vm_map_entry_t *entry = NULL;
        for (vm_map_entry_t *cur = map->header->next; cur != map->header; cur = cur->next) {
            if (va >= cur->start && va < cur->end) {
                entry = cur;
                break;
            }
            if (va < cur->start) {
                break;
            }
        }
        if (!entry || !entry->object) continue;
        
        uint64_t pindex = (va - entry->start + entry->offset) / 4096;
        vm_page_t *m = vm_object_lookup_page(entry->object, pindex);

        /* A MAP_SHARED page is mapped writable, so a store sets only the
         * hardware PTE dirty bit, never the software PG_DIRTY flag.  Harvest
         * the real PTE dirty state so shared writes actually get flushed.
         * (MAP_PRIVATE faults onto an anonymous shadow with no pager, so its
         * dirty pages never reach the file here — correct.) */
        int hw_dirty = pmap_is_modified(map->pmap, va);
        if (m && ((m->flags & PG_DIRTY) || hw_dirty)) {
            // Write back via pager
            if (entry->object->pager) {
                vm_page_t *pages[1] = { m };
                vm_pager_put_pages(entry->object->pager, pages, 1, true);
            }
            m->flags &= ~PG_DIRTY;
            pmap_clear_modify(map->pmap, va);
        }
    }

    vm_map_unlock_read(map);
    return 0;
}
