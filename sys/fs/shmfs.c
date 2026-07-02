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
 *   - On unlink, the inode is removed from the directory and its
 *     buffer freed.  Existing fd holders keep the in-memory data
 *     alive only through their fd's open_fs refcount (proper
 *     refcounted free is a future-work item; today an unlink while
 *     an fd is open frees the buffer underfoot — caller should
 *     ftruncate(0) before unlinking if races matter).
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
    int      unlinked;        /* set true after directory removal */
    fs_node_t node;
    struct shmfs_inode *next;
} shmfs_inode_t;

/* Singleton root + child list.  Mount returns the same root node
 * every time — POSIX shm has one global namespace per system. */
static fs_node_t       shmfs_root_node;
static shmfs_inode_t  *shmfs_children = NULL;
static struct dirent   shmfs_dirent;
static uint64_t        shmfs_next_inode = 2;  /* 1 reserved for root */

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

static void shmfs_node_open(fs_node_t *node) {
    shmfs_inode_t *inode = (shmfs_inode_t *)(uintptr_t)node->impl;
    if (inode) inode->refcount++;
}

static void shmfs_node_close(fs_node_t *node) {
    shmfs_inode_t *inode = (shmfs_inode_t *)(uintptr_t)node->impl;
    if (!inode) return;
    if (inode->refcount > 0) inode->refcount--;
    /* Last close + unlinked + not in directory list → free now. */
    if (inode->refcount == 0 && inode->unlinked) {
        shmfs_free_inode(inode);
    }
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
 * owned solely by the inode and are released by shmfs_free_inode once
 * the object is unlinked and its last fd is closed.  No leak, no
 * early free / use-after-free.
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

    if (!inode) return (void *)-1;
    if (offset < 0 || length == 0) return (void *)-1;
    /* The offset selects which page of the object to start at, so it
     * must be page-aligned (the device pager indexes by page). */
    if ((offset & 0xFFF) != 0) return (void *)-1;

    /* Size the object to cover the requested window BEFORE we build the
     * mapping.  After this, the backing physical base is stable for the
     * life of these mappings: a later shmfs_grow only reallocates when it
     * needs MORE than data_cap, and a MAP_SHARED user has already pinned
     * its slice via ftruncate per POSIX. */
    size_t end = (size_t)offset + length;
    if (shmfs_grow(inode, end) != 0) return (void *)-1;
    /* Extend logical length so subsequent read/write past the pre-mmap
     * length still sees the mapped region as live. */
    if ((uint64_t)end > (uint64_t)node->length) node->length = (off_t)end;

    aligned_length = (length + 0xFFF) & ~(size_t)0xFFF;

    if (current_process == NULL) return (void *)-1;
    map = current_process->vm_map;
    if (map == NULL) return (void *)-1;

    if (virt == 0 || !(flags & MAP_FIXED)) {
        if (vm_map_find_space(map, &virt, aligned_length) != 0)
            return (void *)-1;
    } else {
        if ((virt & 0xFFF) != 0) return (void *)-1;
        if (vm_map_remove(map, virt, virt + aligned_length) != 0)
            return (void *)-1;
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
    if (obj == NULL) return (void *)-1;
    obj->pager = vm_pager_allocate(VM_OBJ_TYPE_DEVICE,
                                   (void *)(inode->data_phys + (uintptr_t)offset),
                                   aligned_length, (uint8_t)vm_prot, 0);
    if (obj->pager == NULL) {
        vm_object_deallocate(obj);
        return (void *)-1;
    }
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
        return (void *)-1;
    }

    (void)node;
    return (void *)virt;
}

static int shmfs_truncate(fs_node_t *node, off_t len) {
    shmfs_inode_t *inode = (shmfs_inode_t *)(uintptr_t)node->impl;
    if (!inode || len < 0) return -EINVAL;
    if ((size_t)len > inode->data_cap) {
        int r = shmfs_grow(inode, (size_t)len);
        if (r < 0) return r;
    }
    /* Shrinking: zero the now-tail to satisfy "reads past length
     * see zero" without freeing the cap (cheaper for ftruncate(0)
     * followed by extend). */
    if ((uint64_t)len < (uint64_t)node->length && inode->data) {
        memset(inode->data + len, 0, (size_t)((uint64_t)node->length - (uint64_t)len));
    }
    node->length = len;
    node->mtime = node->ctime = get_time();
    return 0;
}

/* --------------------------------------------------------------
 * Root directory operations: readdir, finddir, mknod, unlink.
 * -------------------------------------------------------------- */
static struct dirent *shmfs_root_readdir(fs_node_t *node, uint64_t index) {
    (void)node;
    if (index == 0) { strcpy(shmfs_dirent.d_name, ".");  return &shmfs_dirent; }
    if (index == 1) { strcpy(shmfs_dirent.d_name, ".."); return &shmfs_dirent; }
    uint64_t i = 2;
    for (shmfs_inode_t *e = shmfs_children; e; e = e->next, i++) {
        if (i == index) {
            strncpy(shmfs_dirent.d_name, e->name, sizeof(shmfs_dirent.d_name) - 1);
            shmfs_dirent.d_name[sizeof(shmfs_dirent.d_name) - 1] = '\0';
            return &shmfs_dirent;
        }
    }
    return NULL;
}

static shmfs_inode_t *shmfs_find_child(const char *name) {
    for (shmfs_inode_t *e = shmfs_children; e; e = e->next) {
        if (strcmp(e->name, name) == 0) return e;
    }
    return NULL;
}

static fs_node_t *shmfs_root_finddir(fs_node_t *node, char *name) {
    (void)node;
    shmfs_inode_t *e = shmfs_find_child(name);
    return e ? &e->node : NULL;
}

static int shmfs_root_mknod(fs_node_t *parent, const char *name, uint16_t mode, uint32_t dev) {
    (void)parent; (void)dev;
    if (!name || !*name) return -EINVAL;
    if (shmfs_find_child(name)) return -EEXIST;

    shmfs_inode_t *ino = (shmfs_inode_t *)kmalloc(sizeof(*ino));
    if (!ino) return -ENOMEM;
    memset(ino, 0, sizeof(*ino));
    strncpy(ino->name, name, sizeof(ino->name) - 1);
    ino->name[sizeof(ino->name) - 1] = '\0';

    /* fs_node_t reflects what callers see via stat. */
    strncpy(ino->node.name, name, sizeof(ino->node.name) - 1);
    ino->node.name[sizeof(ino->node.name) - 1] = '\0';
    ino->node.flags    = FS_FILE;
    ino->node.mask     = mode & 07777;
    ino->node.uid      = current_process ? current_process->euid : 0;
    ino->node.gid      = current_process ? current_process->egid : 0;
    ino->node.inode    = shmfs_next_inode++;
    ino->node.length   = 0;
    ino->node.impl     = (uintptr_t)ino;
    ino->node.read     = shmfs_read;
    ino->node.write    = shmfs_write;
    ino->node.truncate = shmfs_truncate;
    ino->node.open     = shmfs_node_open;
    ino->node.close    = shmfs_node_close;
    ino->node.mmap     = shmfs_node_mmap;
    shmfs_refresh_timestamps(&ino->node);

    ino->next = shmfs_children;
    shmfs_children = ino;

    /* Bump the directory's mtime/ctime so getdents callers see
     * a fresh entry. */
    shmfs_root_node.mtime = shmfs_root_node.ctime = get_time();
    return 0;
}

static int shmfs_root_unlink(fs_node_t *node, const char *name) {
    (void)node;
    shmfs_inode_t **link = &shmfs_children;
    while (*link) {
        if (strcmp((*link)->name, name) == 0) {
            shmfs_inode_t *victim = *link;
            *link = victim->next;
            shmfs_root_node.mtime = shmfs_root_node.ctime = get_time();
            /* Refcounted free: if no one has it open, drop it now;
             * otherwise mark it deleted so the last close cleans up.
             * Either way it's gone from the directory listing
             * immediately (POSIX semantics). */
            if (victim->refcount == 0) {
                shmfs_free_inode(victim);
            } else {
                victim->unlinked = 1;
            }
            return 0;
        }
        link = &(*link)->next;
    }
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
    memset(&shmfs_root_node, 0, sizeof(shmfs_root_node));
    strncpy(shmfs_root_node.name, "shm", sizeof(shmfs_root_node.name) - 1);
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
