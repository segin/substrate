/*
 * sys/fs/shmfs.c — POSIX shared-memory filesystem.
 *
 * tmpfs-flavoured filesystem dedicated to /dev/shm so that POSIX
 * shm_open(3) / shm_unlink(3) callers in userland see a real
 * mount point with read+write file backing instead of falling
 * back to /tmp/shm-<name> emulation.
 *
 * Design:
 *
 *   - Single root directory served by shmfs_root_node.  Children
 *     are tracked in a singly-linked list of struct shmfs_inode
 *     keyed by name (a flat namespace — POSIX shm names are
 *     opaque strings, no nested paths).
 *
 *   - Each inode owns a growing buffer (initialised empty, extended
 *     on write past current length).  Buffer reallocs use kmalloc /
 *     kfree-with-size; ftruncate is the canonical way to size a
 *     shm object before mapping.
 *
 *   - On unlink, the inode is removed from the directory but its
 *     backing frames survive until BOTH the last fd closes (refcount)
 *     AND the last mmap is torn down (map_count) — see
 *     shmfs_maybe_free_inode.  Freeing on fd close alone returned frames
 *     to the buddy allocator while a MAP_SHARED mapping still aliased
 *     them (fatal once a killed process is reaped).
 *
 *   - All namespace + lifetime state (the child list, the name hash,
 *     per-inode refcount/map_count) is serialised by shmfs_lock so a
 *     concurrent shm_open()/shm_unlink() storm cannot corrupt it.
 *
 *   - All mode/uid/gid checks are deferred to the VFS layer; the
 *     filesystem fills name/mode/uid/gid honestly so vfs_may_open
 *     can do its job.
 *
 * The filesystem is mounted by sys/kern/main.c immediately after
 * devfs goes live, on top of the empty /dev/shm directory that
 * devfs creates at init time.
 */

#include <vfs/vfs.h>
#include <vm/vm_kmem.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/lock.h>
#include <kern/time.h>
#include <sys/proc.h>
#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

#ifndef SHMFS_NAME_MAX
#define SHMFS_NAME_MAX 128
#endif

/* --------------------------------------------------------------
 * Per-inode state.  One per shm object plus the root directory.
 * -------------------------------------------------------------- */
typedef struct shmfs_inode {
    char     name[SHMFS_NAME_MAX];
    uint8_t *data;            /* kernel direct-map VA of the backing pages */
    uintptr_t data_phys;      /* physical base of the backing pages       */
    size_t   data_cap;        /* allocated bytes (always page-multiple)    */
    int      refcount;        /* open fd count; freed when 0 + unlinked */
    int      map_count;       /* live mmap device objects referencing us   */
    int      unlinked;        /* set true after directory removal */
    fs_node_t node;
    struct shmfs_inode *next;
    struct shmfs_inode *hash_next; /* chain in shmfs_hash[] bucket */
} shmfs_inode_t;

/* --------------------------------------------------------------
 * Concurrency: a single mutex serialises every mutation of the
 * child list, the name hash, and the per-inode refcount/map_count
 * lifetime bookkeeping.  Before this lock existed, a shm_open()
 * storm (OPTS shm_open/23-1 forks 1000 processes each racing to
 * create/unlink the same names) mutated shmfs_children with no
 * serialisation: two concurrent mknods lost a prepend or built a
 * cycle, and an unlink freed an inode another lookup was walking —
 * corrupting the kernel heap and panicking an unrelated later test.
 *
 * shmfs_lock is a leaf lock: no other lock is ever acquired while
 * it is held, and it is never held across a vm_map / vm_object call
 * (shmfs_node_mmap drops it before touching the VM layer), so the
 * only nesting direction is vm_map -> shmfs (the mmap destructor
 * callback), never the reverse — no lock-order inversion.
 * -------------------------------------------------------------- */
#ifndef SHMFS_HASH_SIZE
#define SHMFS_HASH_SIZE 256   /* power of two */
#endif

/* Singleton root + child list.  Mount returns the same root node
 * every time — POSIX shm has one global namespace per system. */
static fs_node_t       shmfs_root_node;
static shmfs_inode_t  *shmfs_children = NULL;
static shmfs_inode_t  *shmfs_hash[SHMFS_HASH_SIZE];
static mutex_t         shmfs_lock;
static struct dirent   shmfs_dirent;
static uint64_t        shmfs_next_inode = 2;  /* 1 reserved for root */

/* FNV-1a over the name, masked to the bucket count (power of two). */
static unsigned shmfs_name_hash(const char *name) {
    uint32_t h = 2166136261u;
    while (*name) {
        h ^= (uint8_t)*name++;
        h *= 16777619u;
    }
    return (unsigned)(h & (SHMFS_HASH_SIZE - 1));
}

static int shmfs_statfs(fs_node_t *node, struct statfs *buf)
{
    (void)node;
    if (!buf) return -EINVAL;
    memset(buf, 0, sizeof(*buf));
    buf->f_bsize  = 4096;
    buf->f_iosize = 4096;
    strncpy(buf->f_fstypename, "shmfs", sizeof(buf->f_fstypename));
    return 0;
}

static void shmfs_refresh_timestamps(fs_node_t *node) {
    time_t now;
    if (!node) return;
    now = get_time();
    node->atime = now;
    node->mtime = now;
    node->ctime = node->ctime ? node->ctime : get_boot_time();
}

/* --------------------------------------------------------------
 * Buffer management: grow `inode->data` to at least `want` bytes.
 * Bytes between old_cap and want are zeroed (POSIX shm objects
 * read as zeroes past their length).
 *
 * The backing store is page-aligned, physically-contiguous RAM
 * (pmm_alloc_contiguous) rather than a kmalloc heap buffer, because
 * shmfs_node_mmap exposes those exact physical frames to userspace
 * via a device pager (which needs a contiguous physical base and
 * page-aligned mappings).  inode->data stays a kernel direct-map VA
 * for the in-kernel read/write/grow paths; inode->data_phys is the
 * physical base the pager hands to the page-fault PTE installer.
 *
 * NOTE: growing reallocates to a fresh physical base and copies the
 * old contents over.  Per POSIX, an object must be sized with
 * ftruncate(2) BEFORE it is mmap'd; shmfs_node_mmap also pre-grows to
 * cover the requested window so that the common shm_open -> ftruncate
 * -> mmap sequence never reallocates a buffer that already has live
 * user mappings aliasing it.
 * -------------------------------------------------------------- */
static int shmfs_grow(shmfs_inode_t *inode, size_t want) {
    if (want <= inode->data_cap) return 0;
    /* Round up to a 4 KiB page boundary — the allocation unit is the page. */
    size_t newcap = (want + 4095) & ~(size_t)4095;
    size_t npages = newcap >> 12;
    uint8_t *nb = (uint8_t *)pmm_alloc_contiguous(npages);
    if (!nb) return -ENOMEM;
    if (inode->data && inode->data_cap > 0) {
        memcpy(nb, inode->data, inode->data_cap);
        pmm_free_contiguous(inode->data, inode->data_cap >> 12);
    }
    memset(nb + (inode->data_cap), 0, newcap - inode->data_cap);
    inode->data      = nb;
    inode->data_phys = (uintptr_t)V2P(nb);
    inode->data_cap  = newcap;
    return 0;
}

/* --------------------------------------------------------------
 * File operations on a regular shm object.
 * -------------------------------------------------------------- */
static size_t shmfs_read(fs_node_t *node, off_t off, size_t sz, uint8_t *buf) {
    shmfs_inode_t *inode = (shmfs_inode_t *)(uintptr_t)node->impl;
    if (!inode || off < 0) return 0;
    if ((uint64_t)off >= (uint64_t)node->length) return 0;
    size_t avail = (size_t)((uint64_t)node->length - (uint64_t)off);
    if (sz > avail) sz = avail;
    memcpy(buf, inode->data + off, sz);
    node->atime = get_time();
    return sz;
}

static size_t shmfs_write(fs_node_t *node, off_t off, size_t sz, const uint8_t *buf) {
    shmfs_inode_t *inode = (shmfs_inode_t *)(uintptr_t)node->impl;
    if (!inode || off < 0) return 0;
    size_t end = (size_t)off + sz;
    if (shmfs_grow(inode, end) != 0) return 0;
    memcpy(inode->data + off, buf, sz);
    if ((uint64_t)end > (uint64_t)node->length) node->length = (off_t)end;
    node->mtime = node->ctime = get_time();
    return sz;
}

/* --------------------------------------------------------------
 * Refcounted open/close.  open_fs/close_fs in sys/vfs/vfs.c
 * dispatch to these for every fd lifecycle event.  An inode's
 * buffer outlives unlink as long as someone still has it open.
 * -------------------------------------------------------------- */
static void shmfs_free_inode(shmfs_inode_t *inode) {
    if (!inode) return;
    if (inode->data && inode->data_cap > 0) {
        pmm_free_contiguous(inode->data, inode->data_cap >> 12);
    }
    kfree(inode, sizeof(*inode));
}

/* Free the inode's backing frames and struct once it is unlinked from
 * the namespace AND has no remaining references of either kind:
 *   - refcount  : open file descriptors (bumped by shmfs_node_open)
 *   - map_count : live mmap() device objects (bumped by shmfs_node_mmap,
 *                 dropped by the device-object destructor callback)
 * Freeing on fd-refcount alone (the old behaviour) returned the backing
 * frames to the buddy allocator while a MAP_SHARED mapping in a not-yet-
 * reaped process still pointed at them: the frames were recycled as page
 * tables / kernel objects, then that zombie's pmap teardown
 * vm_page_unhold()'d and double-freed them -> buddy corruption -> panic in
 * a later, unrelated shm_open (OPTS shm_open/26-1 after a killed 23-1).
 * Caller must hold shmfs_lock. */
static void shmfs_maybe_free_inode(shmfs_inode_t *inode) {
    if (!inode) return;
    if (inode->unlinked && inode->refcount == 0 && inode->map_count == 0) {
        shmfs_free_inode(inode);
    }
}

static void shmfs_node_open(fs_node_t *node) {
    shmfs_inode_t *inode = (shmfs_inode_t *)(uintptr_t)node->impl;
    if (!inode) return;
    mutex_lock(&shmfs_lock);
    inode->refcount++;
    mutex_unlock(&shmfs_lock);
}

static void shmfs_node_close(fs_node_t *node) {
    shmfs_inode_t *inode = (shmfs_inode_t *)(uintptr_t)node->impl;
    if (!inode) return;
    mutex_lock(&shmfs_lock);
    if (inode->refcount > 0) inode->refcount--;
    shmfs_maybe_free_inode(inode);
    mutex_unlock(&shmfs_lock);
}

/* Device-object destructor: the last mmap of this shm object went away
 * (every sharer's address space was torn down and the shared device
 * vm_object's ref_count hit zero, so vm_object_deallocate invoked the
 * pager dtor).  Drop the mapping reference and free the inode if nothing
 * else holds it.  Runs at munmap / process-exit teardown; takes only
 * shmfs_lock. */
static void shmfs_map_release(void *arg) {
    shmfs_inode_t *inode = (shmfs_inode_t *)arg;
    if (!inode) return;
    mutex_lock(&shmfs_lock);
    if (inode->map_count > 0) inode->map_count--;
    shmfs_maybe_free_inode(inode);
    mutex_unlock(&shmfs_lock);
}

/* --------------------------------------------------------------
 * mmap — map the inode's backing pages into the CALLING process's
 * address space and return a USER virtual address.
 *
 * The backing store is page-aligned, physically-contiguous RAM
 * (see shmfs_grow).  We expose those exact physical frames to the
 * caller through a device pager rooted at inode->data_phys + offset
 * and insert the mapping into current_process->vm_map with
 * VM_INHERIT_SHARE.  Because every mapping of the object — in this
 * process, in another process that shm_open()s the same name, or in
 * a forked child — resolves to the SAME physical frames (MAP_SHARED,
 * not copy-on-write), a write through any one mapping is immediately
 * visible through all the others.
 *
 * The device object holds no vm_page_t structures of its own, so its
 * teardown at munmap/exit (vm_object_deallocate) frees only the
 * mapping bookkeeping — never the shmfs frames.  Those frames are
 * owned solely by the inode.  Each mmap takes a map_count reference and
 * registers a device-pager destructor (shmfs_map_release) that drops it
 * when the object's last mapping is torn down; the inode (and its frames)
 * is freed only once refcount AND map_count are both zero and it is
 * unlinked.  No leak, no early free / use-after-free.
 *
 * This mirrors the established device-mmap pattern in fb_fs_mmap
 * (sys/drivers/video/fb.c) and ac97_mmap (sys/drivers/audio/ac97.c).
 * -------------------------------------------------------------- */
static void *shmfs_node_mmap(fs_node_t *node, void *addr, size_t length,
                             int prot, int flags, off_t offset) {
    shmfs_inode_t *inode = (shmfs_inode_t *)(uintptr_t)node->impl;
    vm_object_t *obj;
    vm_map_t *map;
    uintptr_t virt = (uintptr_t)addr;
    size_t aligned_length;
    uint32_t vm_prot = 0;
    uintptr_t phys_base;

    if (!inode) return (void *)-1;
    if (offset < 0 || length == 0) return (void *)-1;
    /* The offset selects which page of the object to start at, so it
     * must be page-aligned (the device pager indexes by page). */
    if ((offset & 0xFFF) != 0) return (void *)-1;

    if (current_process == NULL) return (void *)-1;
    map = current_process->vm_map;
    if (map == NULL) return (void *)-1;

    /* Size the object to cover the requested window BEFORE we build the
     * mapping, and take a mapping reference (map_count) so the backing
     * frames are not freed out from under this mapping when the last fd
     * closes (see shmfs_maybe_free_inode).  All inode-state mutation is
     * done under shmfs_lock; the VM-layer work below runs WITHOUT it to
     * keep shmfs_lock a leaf lock (no shmfs_lock -> vm_map ordering). */
    size_t end = (size_t)offset + length;
    mutex_lock(&shmfs_lock);
    if (shmfs_grow(inode, end) != 0) {
        mutex_unlock(&shmfs_lock);
        return (void *)-1;
    }
    /* The object's real data length BEFORE this mmap's window extension below.
     * A reference to a page that lies entirely past it must SIGBUS per POSIX
     * (mmap/11-3), even though the window's frames are backed here. */
    off_t orig_len = node->length;
    /* Extend logical length so subsequent read/write past the pre-mmap
     * length still sees the mapped region as live. */
    if ((uint64_t)end > (uint64_t)node->length) node->length = (off_t)end;
    /* Capture the physical base under the lock: after this a concurrent
     * grow could realloc data_phys, but a MAP_SHARED user has already
     * pinned its slice via ftruncate per POSIX, so no realloc happens. */
    phys_base = inode->data_phys + (uintptr_t)offset;
    inode->map_count++;
    mutex_unlock(&shmfs_lock);

    aligned_length = (length + 0xFFF) & ~(size_t)0xFFF;

    if (virt == 0 || !(flags & MAP_FIXED)) {
        if (vm_map_find_space(map, &virt, aligned_length) != 0)
            goto fail;
    } else {
        if ((virt & 0xFFF) != 0) goto fail;
        if (vm_map_remove(map, virt, virt + aligned_length) != 0)
            goto fail;
    }

    if (prot & VM_PROT_READ)  vm_prot |= VM_PROT_READ;
    /* x86 has no write-only page: any present page is readable, so a
     * PROT_WRITE mapping must also carry READ or it maps read-only and a
     * store SIGSEGVs.  POSIX explicitly permits treating PROT_WRITE as
     * PROT_READ|PROT_WRITE (OPTS shm_open/1-1,14-2,28-1,28-3 mmap PROT_WRITE
     * alone, then write). */
    if (prot & VM_PROT_WRITE) vm_prot |= VM_PROT_READ | VM_PROT_WRITE;
    if (prot & VM_PROT_EXEC)  vm_prot |= VM_PROT_EXEC;
    vm_prot |= VM_PROT_USER;

    obj = vm_object_allocate(VM_OBJ_TYPE_DEVICE, aligned_length);
    if (obj == NULL) goto fail;
    obj->pager = vm_pager_allocate(VM_OBJ_TYPE_DEVICE,
                                   (void *)phys_base,
                                   aligned_length, (uint8_t)vm_prot, 0);
    if (obj->pager == NULL) {
        vm_object_deallocate(obj);
        goto fail;
    }
    /* Bound the mapping to the object's real extent: a fault on a page wholly
     * past orig_len (from this mapping's offset) SIGBUSes instead of mapping an
     * out-of-object frame (POSIX mmap-past-end, mmap/11-3).  0 == no limit. */
    vm_pager_device_set_valid_pages(obj->pager,
        (uint64_t)orig_len > (uint64_t)offset
            ? (((uint64_t)orig_len - (uint64_t)offset + 4095) / 4096)
            : 0);
    /* Shared anonymous RAM (not strict MMIO registers): map it
     * write-combining so userland stores coalesce into bursts instead
     * of one serialized uncached transaction each.  Falls back to
     * uncached automatically when the CPU has no PAT/WC. */
    vm_pager_set_cache_mode(obj->pager, VM_PAGER_CACHE_WC);

    /* VM_INHERIT_SHARE: a fork must share — not copy — these frames, so
     * parent and child observe each other's writes (POSIX MAP_SHARED). */
    if (vm_map_insert(map, obj, 0, virt, virt + aligned_length,
                      (uint8_t)vm_prot, (uint8_t)vm_prot,
                      VM_INHERIT_SHARE) != 0) {
        vm_object_deallocate(obj);
        goto fail;
    }

    /* Register the destructor now that the mapping is live.  It fires from
     * vm_object_deallocate when this device object's ref_count hits zero
     * (every sharing address space torn down) and drops the map_count we
     * took above.  Set it last so no failure path above (which run
     * vm_object_deallocate before the dtor exists) double-drops map_count;
     * every such path routes through `fail:` and drops it explicitly. */
    vm_pager_device_set_dtor(obj->pager, shmfs_map_release, inode);

    (void)node;
    return (void *)virt;

fail:
    mutex_lock(&shmfs_lock);
    if (inode->map_count > 0) inode->map_count--;
    shmfs_maybe_free_inode(inode);
    mutex_unlock(&shmfs_lock);
    return (void *)-1;
}

static int shmfs_truncate(fs_node_t *node, off_t len) {
    shmfs_inode_t *inode = (shmfs_inode_t *)(uintptr_t)node->impl;
    if (!inode || len < 0) return -EINVAL;
    /* Serialise against shmfs_node_mmap's shmfs_grow (both realloc
     * inode->data); shmfs_lock protects the backing-store fields. */
    mutex_lock(&shmfs_lock);
    if ((size_t)len > inode->data_cap) {
        int r = shmfs_grow(inode, (size_t)len);
        if (r < 0) { mutex_unlock(&shmfs_lock); return r; }
    }
    /* Shrinking: zero the now-tail to satisfy "reads past length
     * see zero" without freeing the cap (cheaper for ftruncate(0)
     * followed by extend). */
    if ((uint64_t)len < (uint64_t)node->length && inode->data) {
        memset(inode->data + len, 0, (size_t)((uint64_t)node->length - (uint64_t)len));
    }
    node->length = len;
    node->mtime = node->ctime = get_time();
    mutex_unlock(&shmfs_lock);
    return 0;
}

/* --------------------------------------------------------------
 * Root directory operations: readdir, finddir, mknod, unlink.
 * -------------------------------------------------------------- */
static struct dirent *shmfs_root_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    if (index == 0) { strlcpy(shmfs_dirent.d_name, ".", sizeof(shmfs_dirent.d_name));  return &shmfs_dirent; }
    if (index == 1) { strlcpy(shmfs_dirent.d_name, "..", sizeof(shmfs_dirent.d_name)); return &shmfs_dirent; }
    mutex_lock(&shmfs_lock);
    uint64_t i = 2;
    for (shmfs_inode_t *e = shmfs_children; e; e = e->next, i++) {
        if (i == index) {
            strlcpy(shmfs_dirent.d_name, e->name, sizeof(shmfs_dirent.d_name));
            shmfs_dirent.d_name[sizeof(shmfs_dirent.d_name) - 1] = '\0';
            mutex_unlock(&shmfs_lock);
            return &shmfs_dirent;
        }
    }
    mutex_unlock(&shmfs_lock);
    return NULL;
}

/* O(1) name lookup via the FNV hash bucket.  Caller must hold shmfs_lock. */
static shmfs_inode_t *shmfs_find_child(const char *name) {
    unsigned b = shmfs_name_hash(name);
    for (shmfs_inode_t *e = shmfs_hash[b]; e; e = e->hash_next) {
        if (strcmp(e->name, name) == 0) return e;
    }
    return NULL;
}

/* Unlink `victim` from its hash bucket.  Caller must hold shmfs_lock. */
static void shmfs_hash_remove(shmfs_inode_t *victim) {
    unsigned b = shmfs_name_hash(victim->name);
    shmfs_inode_t **hp = &shmfs_hash[b];
    while (*hp) {
        if (*hp == victim) {
            *hp = victim->hash_next;
            victim->hash_next = NULL;
            return;
        }
        hp = &(*hp)->hash_next;
    }
}

static fs_node_t *shmfs_root_finddir(fs_node_t *node, char *name) {
    (void)node;
    mutex_lock(&shmfs_lock);
    shmfs_inode_t *e = shmfs_find_child(name);
    fs_node_t *n = e ? &e->node : NULL;
    mutex_unlock(&shmfs_lock);
    return n;
}

static int shmfs_root_mknod(fs_node_t *parent, const char *name, uint16_t mode, uint32_t dev) {
    (void)parent; (void)dev;
    if (!name || !*name) return -EINVAL;

    /* Allocate + initialise the inode BEFORE taking the lock so the lock
     * is held only across the O(1) existence check + list/hash splice —
     * kmalloc must not run under a leaf spin-adjacent mutex any longer
     * than necessary under a 1000-way shm_open storm. */
    shmfs_inode_t *ino = (shmfs_inode_t *)kmalloc(sizeof(*ino));
    if (!ino) return -ENOMEM;
    memset(ino, 0, sizeof(*ino));
    strlcpy(ino->name, name, sizeof(ino->name));
    ino->name[sizeof(ino->name) - 1] = '\0';

    /* fs_node_t reflects what callers see via stat. */
    strlcpy(ino->node.name, name, sizeof(ino->node.name));
    ino->node.name[sizeof(ino->node.name) - 1] = '\0';
    ino->node.flags    = FS_FILE;
    ino->node.mask     = mode & 07777;
    ino->node.uid      = current_process ? current_process->euid : 0;
    ino->node.gid      = current_process ? current_process->egid : 0;
    ino->node.length   = 0;
    ino->node.impl     = (uintptr_t)ino;
    ino->node.read     = shmfs_read;
    ino->node.write    = shmfs_write;
    ino->node.truncate = shmfs_truncate;
    ino->node.open     = shmfs_node_open;
    ino->node.close    = shmfs_node_close;
    ino->node.mmap     = shmfs_node_mmap;
    shmfs_refresh_timestamps(&ino->node);

    mutex_lock(&shmfs_lock);
    /* Atomic existence check + insert: the O_CREAT|O_EXCL contract (OPTS
     * shm_open/23-1) requires that exactly one of N racing creators of the
     * same name wins.  Doing find + insert under one lock guarantees it. */
    if (shmfs_find_child(name)) {
        mutex_unlock(&shmfs_lock);
        kfree(ino, sizeof(*ino));
        return -EEXIST;
    }
    ino->node.inode = shmfs_next_inode++;

    unsigned b = shmfs_name_hash(name);
    ino->hash_next = shmfs_hash[b];
    shmfs_hash[b] = ino;

    ino->next = shmfs_children;
    shmfs_children = ino;

    /* Bump the directory's mtime/ctime so getdents callers see
     * a fresh entry. */
    shmfs_root_node.mtime = shmfs_root_node.ctime = get_time();
    mutex_unlock(&shmfs_lock);
    return 0;
}

static int shmfs_root_unlink(fs_node_t *node, const char *name) {
    (void)node;
    mutex_lock(&shmfs_lock);
    shmfs_inode_t **link = &shmfs_children;
    while (*link) {
        if (strcmp((*link)->name, name) == 0) {
            shmfs_inode_t *victim = *link;
            *link = victim->next;
            shmfs_hash_remove(victim);
            shmfs_root_node.mtime = shmfs_root_node.ctime = get_time();
            /* Gone from the directory listing immediately (POSIX
             * semantics).  Free the backing frames + struct only once no
             * fd (refcount) AND no mmap (map_count) still references it —
             * see shmfs_maybe_free_inode for why freeing on refcount==0
             * alone corrupts the buddy allocator. */
            victim->unlinked = 1;
            shmfs_maybe_free_inode(victim);
            mutex_unlock(&shmfs_lock);
            return 0;
        }
        link = &(*link)->next;
    }
    mutex_unlock(&shmfs_lock);
    return -ENOENT;
}

/* --------------------------------------------------------------
 * Filesystem hookup
 * -------------------------------------------------------------- */
static fs_node_t *shmfs_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    shmfs_refresh_timestamps(&shmfs_root_node);
    return &shmfs_root_node;
}

static filesystem_t shmfs_fs = {
    .name = "shmfs",
    .mount = &shmfs_mount,
    .caps = VFS_CAP_VIRTUAL,
};

void shmfs_init(void) {
    mutex_init(&shmfs_lock, "shmfs");
    shmfs_children = NULL;
    memset(shmfs_hash, 0, sizeof(shmfs_hash));
    memset(&shmfs_root_node, 0, sizeof(shmfs_root_node));
    strlcpy(shmfs_root_node.name, "shm", sizeof(shmfs_root_node.name));
    shmfs_root_node.name[sizeof(shmfs_root_node.name) - 1] = '\0';
    shmfs_root_node.flags   = FS_DIRECTORY;
    shmfs_root_node.mask    = 01777;    /* sticky world-writable, like /tmp */
    shmfs_root_node.uid     = 0;
    shmfs_root_node.gid     = 0;
    shmfs_root_node.inode   = 1;
    shmfs_root_node.readdir = shmfs_root_readdir;
    shmfs_root_node.finddir = shmfs_root_finddir;
    shmfs_root_node.mknod   = shmfs_root_mknod;
    shmfs_root_node.unlink  = shmfs_root_unlink;
    shmfs_root_node.statfs  = shmfs_statfs;
    shmfs_refresh_timestamps(&shmfs_root_node);

    vfs_register_filesystem(&shmfs_fs);
}
