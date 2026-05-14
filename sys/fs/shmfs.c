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
#include <kern/time.h>
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
    uint8_t *data;
    size_t   data_cap;        /* allocated bytes */
    fs_node_t node;
    struct shmfs_inode *next;
} shmfs_inode_t;

/* Singleton root + child list.  Mount returns the same root node
 * every time — POSIX shm has one global namespace per system. */
static fs_node_t       shmfs_root_node;
static shmfs_inode_t  *shmfs_children = NULL;
static struct dirent   shmfs_dirent;
static uint64_t        shmfs_next_inode = 2;  /* 1 reserved for root */

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
 * -------------------------------------------------------------- */
static int shmfs_grow(shmfs_inode_t *inode, size_t want) {
    if (want <= inode->data_cap) return 0;
    /* Round up to a 4 KiB page boundary to amortise kmalloc churn. */
    size_t newcap = (want + 4095) & ~(size_t)4095;
    uint8_t *nb = (uint8_t *)kmalloc(newcap);
    if (!nb) return -ENOMEM;
    if (inode->data && inode->data_cap > 0) {
        memcpy(nb, inode->data, inode->data_cap);
        kfree(inode->data, inode->data_cap);
    }
    memset(nb + (inode->data_cap), 0, newcap - inode->data_cap);
    inode->data     = nb;
    inode->data_cap = newcap;
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
    ino->node.uid      = 0;            /* TODO: pull from current_process */
    ino->node.gid      = 0;
    ino->node.inode    = shmfs_next_inode++;
    ino->node.length   = 0;
    ino->node.impl     = (uintptr_t)ino;
    ino->node.read     = shmfs_read;
    ino->node.write    = shmfs_write;
    ino->node.truncate = shmfs_truncate;
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
            if (victim->data) kfree(victim->data, victim->data_cap);
            kfree(victim, sizeof(*victim));
            shmfs_root_node.mtime = shmfs_root_node.ctime = get_time();
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
    shmfs_refresh_timestamps(&shmfs_root_node);

    vfs_register_filesystem(&shmfs_fs);
}
