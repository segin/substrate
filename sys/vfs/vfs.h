#ifndef _VFS_H
#define _VFS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/dirent.h>

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x03
#define FS_BLOCKDEVICE 0x04
#define FS_PIPE        0x05
#define FS_SYMLINK     0x06
#define FS_SOCKET      0x07   /* S_IFSOCK node (named AF_UNIX socket) */
#define FS_MOUNTPOINT  0x08

struct fs_node;
struct mount;
struct statfs;
struct statvfs;

typedef size_t (*read_type_t)(struct fs_node*, off_t, size_t, uint8_t*);
typedef size_t (*write_type_t)(struct fs_node*, off_t, size_t, const uint8_t*);
typedef void (*open_type_t)(struct fs_node*);
typedef void (*close_type_t)(struct fs_node*);
typedef struct dirent * (*readdir_type_t)(struct fs_node*, uint64_t);
typedef struct fs_node * (*finddir_type_t)(struct fs_node*, char *name);
typedef int (*ioctl_type_t)(struct fs_node*, uint32_t, void*);
typedef void * (*mmap_type_t)(struct fs_node*, void *addr, size_t length, int prot, int flags, off_t offset);
typedef int (*poll_type_t)(struct fs_node*, void *waiter);

// Symlink Operations
typedef int (*readlink_type_t)(struct fs_node*, char *buf, size_t size);
typedef int (*symlink_type_t)(struct fs_node*, const char *target, const char *name);
typedef int (*link_type_t)(struct fs_node*, struct fs_node*, const char*);
typedef int (*unlink_type_t)(struct fs_node*, const char *name);
typedef int (*rmdir_type_t)(struct fs_node*, const char *name);
typedef int (*mkdir_type_t)(struct fs_node*, const char *name, uint16_t permission);
typedef int (*mknod_type_t)(struct fs_node*, const char *name, uint16_t mode, uint32_t dev);
typedef int (*truncate_type_t)(struct fs_node*, off_t);
typedef int (*unmount_type_t)(struct fs_node*);
/* Update a live mount's flags in place (MNT_UPDATE / -o remount): apply the
 * new MNT_* flags to the mounted filesystem — notably flipping read-only vs
 * read-write.  Returns 0 on success or a negative errno (e.g. -EROFS if the
 * filesystem cannot honour read-write). */
typedef int (*remount_type_t)(struct fs_node*, uint32_t flags);
typedef int (*rename_type_t)(struct fs_node *old_parent, const char *old_name, struct fs_node *new_parent, const char *new_name);
typedef int (*statfs_type_t)(struct fs_node *node, struct statfs *buf);
typedef int (*chmod_type_t)(struct fs_node *node, uint32_t mode);

/* set-attribute interface for whole-vnode updates that don't fit
 * the per-attribute chmod/chown calls.  Today the only caller is
 * utimes/utimensat, which needs to write atime/mtime atomically;
 * but the struct + mask design accommodates future setuid/setgid
 * /size-truncate paths without churning fs_node_t every time.  */
struct fs_attr {
    uint32_t mask;      /* FS_ATTR_* — which fields are valid */
    int64_t  atime;     /* seconds since epoch */
    int64_t  mtime;
    int64_t  ctime;
    uint32_t atime_nsec;/* 0..999_999_999 — meaningful only when the
                         * backing fs records nsec (e.g. ext4 with
                         * EXTRA_ISIZE).  Backends that lack precision
                         * silently round.  */
    uint32_t mtime_nsec;
    uint32_t ctime_nsec;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    off_t    size;
};
#define FS_ATTR_ATIME   0x0001U
#define FS_ATTR_MTIME   0x0002U
#define FS_ATTR_CTIME   0x0004U
#define FS_ATTR_MODE    0x0008U
#define FS_ATTR_UID     0x0010U
#define FS_ATTR_GID     0x0020U
#define FS_ATTR_SIZE    0x0040U
/* "use current time for this field" — analogous to UTIME_NOW.  */
#define FS_ATTR_ATIME_NOW   0x0100U
#define FS_ATTR_MTIME_NOW   0x0200U

typedef int (*setattr_type_t)(struct fs_node *node, const struct fs_attr *a);
typedef int (*getattr_type_t)(struct fs_node *node, struct fs_attr *a);

typedef struct fs_node {
    char name[128];
    uint32_t mask;        // The permissions mask.
    uint32_t uid;         // The owning user.
    uint32_t gid;         // The owning group.
    uint32_t flags;       // Includes the node type.
    uint64_t inode;       // This is device-specific - provides a way for a filesystem to identify files.
    off_t    length;      // Size of the file, in bytes (64-bit).
    uint32_t rdev;        // Device ID (if character or block device).
    uintptr_t impl;       // An implementation-defined number.
    
    // Timestamps (64-bit for Year 2038 compliance)
    int64_t atime;        // Last access time
    int64_t mtime;        // Last modification time
    int64_t ctime;        // Last status change time
    
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    readdir_type_t readdir;
    finddir_type_t finddir;
    ioctl_type_t ioctl;
    mmap_type_t mmap;
    poll_type_t poll;
    readlink_type_t readlink;
    symlink_type_t symlink;
    link_type_t link;
    unlink_type_t unlink;
    rmdir_type_t rmdir;
    mkdir_type_t mkdir;
    mknod_type_t mknod;
    truncate_type_t truncate;
    unmount_type_t unmount;
    remount_type_t remount;
    rename_type_t rename;
    statfs_type_t statfs;
    chmod_type_t chmod;
    setattr_type_t setattr;
    getattr_type_t getattr;
    /* xattr read-side hooks.  Backends that don't support xattr
     * leave these NULL — the syscall layer returns -ENOTSUP.  */
    int (*getxattr)(struct fs_node *node, const char *name,
                    void *out, size_t out_size, size_t *result_size);
    int (*listxattr)(struct fs_node *node,
                     void *out, size_t out_size, size_t *result_size);
    struct fs_node *ptr; // Used by mountpoints and symlinks.
    struct mount *mp;    // Mount point this node belongs to.
} fs_node_t;

typedef struct fs_node * (*mount_type_t)(const char *device, uint32_t flags, void *data);

/*
 * Filesystem capability bitmap.  Each backend declares which
 * features it supports so userspace tools (and /proc/filesystems
 * style enumeration) can report capabilities consistently.
 */
#define VFS_CAP_NEEDS_DEV       0x00000001U /* needs a backing device */
#define VFS_CAP_RDONLY_ONLY     0x00000002U /* MNT_RDONLY is mandatory */
#define VFS_CAP_REMOUNT         0x00000004U /* supports MNT_UPDATE */
#define VFS_CAP_VIRTUAL         0x00000008U /* synthesised, no source */
#define VFS_CAP_NETWORK         0x00000010U /* network-backed */
#define VFS_CAP_USER_MOUNT      0x00000020U /* unprivileged users may mount */

struct blkdev;

typedef struct filesystem {
    char name[32];
    mount_type_t mount;
    uint32_t caps;                  /* VFS_CAP_* bitmap */
    /*
     * Read this filesystem's on-disk volume label from a raw block
     * device, without mounting it.  Writes a NUL-terminated string to
     * `label` (<= len bytes) and returns 0 if `dev` holds this kind of
     * filesystem; returns non-zero if the device is not recognised or
     * carries no label.  NULL for filesystems with no label concept.
     */
    int (*read_label)(struct blkdev *dev, char *label, size_t len);
    struct filesystem *next;
} filesystem_t;

/*
 * Volume-label support.  vfs_read_label() probes every registered
 * filesystem's read_label hook against a device and returns the first
 * match.  vfs_resolve_label() scans every block device for one whose
 * label equals `label` and writes its "/dev/storage/<name>" path to
 * `devpath` — this is what backs LABEL=<name> mount sources.
 */
int vfs_read_label(struct blkdev *dev, char *label, size_t len);
int vfs_resolve_label(const char *label, char *devpath, size_t len);

typedef struct vfs_mount {
    char path[128];
    fs_node_t *root;
    struct vfs_mount *next;
} vfs_mount_t;

// Standard VFS functions
size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
void open_fs(fs_node_t *node, uint8_t read, uint8_t write);
void close_fs(fs_node_t *node);
/* Fills *out with directory entry `index` and returns `out`, or NULL at end
 * of directory.  The caller supplies the storage: filesystem drivers hand
 * back a buffer they reuse, so a shared one races concurrent readers. */
struct dirent *readdir_fs(fs_node_t *node, uint64_t index, struct dirent *out);
void vfs_readdir_init(void);
fs_node_t *finddir_fs(fs_node_t *node, char *name);
int ioctl_fs(fs_node_t *node, uint32_t request, void *arg);
void *mmap_fs(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset);
int poll_fs(fs_node_t *node, void *waiter);
int readlink_fs(fs_node_t *node, char *buf, size_t size);
int setattr_fs(fs_node_t *node, const struct fs_attr *a);
int getattr_fs(fs_node_t *node, struct fs_attr *a);
int symlink_fs(fs_node_t *parent, const char *target, const char *name);
int link_fs(fs_node_t *parent, fs_node_t *source, const char *name);
int unlink_fs(fs_node_t *node, const char *name);
int rmdir_fs(fs_node_t *node, const char *name);
int rename_fs(fs_node_t *old_parent, const char *old_name, fs_node_t *new_parent, const char *new_name);
int statfs_fs(fs_node_t *node, struct statfs *buf);
int statvfs_fs(fs_node_t *node, struct statvfs *buf);
int mknod_fs(fs_node_t *node, const char *name, uint16_t mode, uint32_t dev);
int vfs_mkdir(const char *path, uint16_t permission);
int vfs_rmdir(const char *path);
int vfs_mknod(const char *path, uint16_t mode, uint32_t dev);

int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode);
int vfs_check_permissions_groups(fs_node_t *node, uint32_t uid, uint32_t gid,
                                 const uint32_t *groups, int ngroups, int mode);
int vfs_may_open(fs_node_t *node, uint32_t uid, uint32_t gid, int flags);
int vfs_may_open_groups(fs_node_t *node, uint32_t uid, uint32_t gid,
                        const uint32_t *groups, int ngroups, int flags);
int vfs_chmod_node(fs_node_t *node, uint32_t mode);

void vfs_register_filesystem(filesystem_t *fs);
filesystem_t *vfs_get_filesystems(void);
int vfs_mount_legacy(const char *device, const char *path, const char *type, uint32_t flags, void *data);
int vfs_unmount_legacy(const char *path);
int vfs_unmount_legacy_flags(const char *path, int flags);
void vfs_unmount_all(void);
/* Force-unmount every filesystem backed by a removed block device (hot-unplug);
 * called from blkdev_unregister before the blkdev is torn down. */
void vfs_force_unmount_dev(struct blkdev *dev);
fs_node_t *vfs_lookup(fs_node_t *root, const char *path);
fs_node_t *vfs_lookup_lstat(fs_node_t *root, const char *path);

/*
 * Reference-counted lookup variants.  On success the returned node
 * has had open_fs() called on it, and the caller MUST balance with
 * close_fs() once it stops using the node.  Use these (rather than
 * vfs_lookup / vfs_lookup_lstat) whenever the caller intends to
 * close_fs the result — otherwise the unbalanced close drops a
 * reference borrowed from somewhere else (notably fs_root, which
 * the mount path pinned at boot) and silently turns the affected
 * cache slot into a recyclable one.  See sys/kern/syscall.c
 * kern_stat / kern_lstat / kern_readlink for the canonical use.
 */
fs_node_t *vfs_lookup_ref(fs_node_t *root, const char *path);
fs_node_t *vfs_lookup_lstat_ref(fs_node_t *root, const char *path);
void vfs_init(void);

void devfs_init(void);
void devfs_register_device(fs_node_t *node);
/*
 * Personality-scoped registration.  perso_mask == 0 means universal (the
 * default; identical to the non-_perso variants).  A non-zero mask is a
 * bitmask over personality ids 0..31 (PERS_NATIVE=0, PERS_NETBSD=2,
 * PERS_LINUX=3, PERS_FREEBSD=9, ...): the node/alias is then visible only
 * to a process whose perso_id bit is set, and a perso-specific entry
 * overrides (shadows) a same-name universal one for those processes.
 */
void devfs_register_device_perso(fs_node_t *node, uint32_t perso_mask);
void devfs_unregister_device(fs_node_t *node);
int devfs_register_alias(const char *path, const char *target);
int devfs_register_alias_perso(const char *path, const char *target,
                               uint32_t perso_mask);
/* Removes the alias registered at `path` with this exact perso_mask.  The
 * mask matters: a universal alias and a personality-scoped override of the
 * same name coexist, and passing the wrong one removes the wrong entry
 * (DEVFS-33).  Use 0 for aliases made with devfs_register_alias(). */
void devfs_unregister_alias(const char *path, uint32_t perso_mask);

/* shmfs — POSIX shared-memory filesystem.  Mounted at /dev/shm
 * immediately after devfs by sys/kern/main.c. */
void shmfs_init(void);
uint64_t shmfs_resident_bytes(void);

extern fs_node_t *fs_root; // Global root node

extern unsigned long fs_open_count, fs_close_count;
extern unsigned long namecache_enter_count;
extern unsigned long namecache_evict_count;
extern unsigned long namecache_purge_count;
extern int           vfs_cache_count;

void sysv_init(void);
extern fs_node_t *devfs_root_node_ptr;

#endif
