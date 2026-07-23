#include <vfs/vfs.h>
#include <vfs/vnode.h>
#include <vfs/buf.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/statvfs.h>
#include <pm/pm.h>

#include <string.h>
#include <kern/console.h>
#include <kern/time.h>
#include <stdio.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <fs/ext2/ext2.h>
#include <fs/fat/fat.h>
#include <drivers/storage/blkdev.h>
#include <fs/exfat/exfat.h>
#include <fs/minix/minix.h>
#include <fs/udf/udf.h>
#include <fs/procfs.h>
#include <fs/sysfs.h>
#include <fs/pseudofs.h>
#include <fs/fuse.h>
#include <fs/9p.h>
#include <sys/kern_syscalls.h>
#include <exec/perso/personality.h>
#include <drivers/console/pty.h>
#include <drivers/devices/full.h>
#include <drivers/devices/cpuid.h>
#include <drivers/console/pty.h>
#include <drivers/storage/blkdev.h>
#include <vm/vm_kmem.h>

struct mountlist mountlist;
fs_node_t *fs_root = 0;
struct vnode *rootvnode = NULL;

/*
 * vfs_mount_lock guards the (FS_MOUNTPOINT flag, node->ptr) tuple on
 * any node that participates in the mount graph, plus mountlist.
 *
 * Acquired both by mount-point traversal (vfs_cross_mountpoint and
 * inline lookups in vfs_lookup) and by mount/umount mutators.  The
 * critical sections are tiny: read two fields, or update two fields
 * and the list — no sleeping operations, no recursion.
 */
static spinlock_t vfs_mount_lock = SPINLOCK_INIT("vfs_mount");

static filesystem_t *filesystems = NULL;

static char *vfs_strrchr(const char *s, int c);

static fs_node_t *vfs_cross_mountpoint(fs_node_t *node) {
    if (!node) return NULL;

    /* Fast path: a node without FS_MOUNTPOINT crosses to itself, so don't take
     * the mount spinlock on every path component (this runs once per component
     * of every lookup; the common case — ext2 files/dirs, where real crossings
     * are already resolved by the (inode,mp) mountlist scan in the caller — is
     * not a flagged mountpoint).  The flag is a single bit: an unlocked read
     * either sees the pre-mount state (treat as non-mountpoint, a valid
     * serialization) or the set bit, in which case we take the lock to read
     * node->ptr safely. */
    if (!(node->flags & FS_MOUNTPOINT)) return node;

    spinlock_acquire(&vfs_mount_lock);
    fs_node_t *target = ((node->flags & FS_MOUNTPOINT) && node->ptr)
                            ? node->ptr : node;
    spinlock_release(&vfs_mount_lock);
    return target;
}

/*
 * ".." across a mount point.  If `node` is the root of a (sub)mounted
 * filesystem, return the mountpoint's PARENT directory so ".." escapes the
 * mounted fs to the covered directory's parent — e.g. /proc/.. -> / , the
 * standard POSIX behaviour (without this, ".." at a mount root stayed inside
 * the mounted fs and /proc/../etc failed).  Returns NULL when `node` is not a
 * sub-mount root, so the caller falls back to the normal finddir("..").
 *
 * Re-resolves the parent by PATH rather than dereferencing mnt_node_covered,
 * which is documented unsafe to dereference (the covered node may have been
 * recycled).  The recomputed parent path contains no "..", so the recursive
 * vfs_lookup() cannot re-enter this escape — the recursion is bounded.
 */
static fs_node_t *vfs_mount_root_parent(fs_node_t *node) {
    if (!node) return NULL;
    struct mount *mnt, *found = NULL;
    char ppath[128];
    ppath[0] = '\0';
    /* Walk and snapshot the matched mount's path under vfs_mount_lock so a
     * concurrent unmount can't unlink/free it mid-traversal (A28). */
    spinlock_acquire(&vfs_mount_lock);
    TAILQ_FOREACH(mnt, &mountlist, mnt_list) {
        if (mnt->mnt_covered_ino != 0 && mnt->mnt_node_root &&
            node->inode == mnt->mnt_node_root->inode &&
            node->mp == mnt->mnt_node_root->mp) {
            found = mnt;
            strlcpy(ppath, found->mnt_stat_path, sizeof(ppath));
            break;
        }
    }
    spinlock_release(&vfs_mount_lock);
    if (!found || ppath[0] != '/') return NULL;

    ppath[sizeof(ppath) - 1] = '\0';
    char *slash = vfs_strrchr(ppath, '/');
    if (!slash) return NULL;
    if (slash == ppath) ppath[1] = '\0';   /* "/proc" -> "/"  */
    else *slash = '\0';                     /* "/a/b"  -> "/a" */
    return vfs_lookup(fs_root, ppath);
}

void vfs_init(void) {
    kprint("VFS: Initializing...\n");

    bio_init();
    
    // Register real filesystem drivers
    ext2_init();
    fat_init();
    exfat_init();
    minix_init();
        sysv_init();
    udf_init();
    
    // Register pseudo-filesystems
    devfs_init();
    procfs_init();
    cpuid_init();
    sysfs_init();
    shmfs_init();    /* mount point at /dev/shm — kmain mounts it after devfs */
    pseudo_init();
    full_init();

    /* PTY subsystem — depends on devfs being up so /dev/ptmx and
     * /dev/pts/ are reachable. */
    pty_init();
    
    // Register network/special filesystems
    fuse_init();
    fuse_fs_init();
    p9_init();
    
    kprint("VFS: Ready.\n");
    namei_init();
    nchinit();
    TAILQ_INIT(&mountlist);
}

void vfs_register_filesystem(filesystem_t *fs) {
    fs->next = filesystems;
    filesystems = fs;
}

filesystem_t *vfs_get_filesystems(void) {
    return filesystems;
}

/*
 * Probe `dev` against every registered filesystem's read_label hook and
 * return the first that recognises it and yields a non-empty label.
 */
int vfs_read_label(struct blkdev *dev, char *label, size_t len) {
    if (!dev || !label || len == 0) {
        return -EINVAL;
    }
    for (filesystem_t *fs = filesystems; fs; fs = fs->next) {
        if (fs->read_label &&
            fs->read_label(dev, label, len) == 0 && label[0] != '\0') {
            return 0;
        }
    }
    return -ENOENT;
}

/*
 * Find a block device whose volume label is exactly `label` and write its
 * "/dev/storage/<name>" path to `devpath`.  Backs LABEL=<name> mounts.
 */
int vfs_resolve_label(const char *label, char *devpath, size_t len) {
    if (!label || !devpath || len == 0) {
        return -EINVAL;
    }
    static const char prefix[] = "/dev/storage/";
    const size_t plen = sizeof(prefix) - 1;
    char buf[64];
    for (blkdev_t *dev = blkdev_first(); dev; dev = dev->next) {
        if (vfs_read_label(dev, buf, sizeof(buf)) != 0) {
            continue;
        }
        if (strcmp(buf, label) != 0) {
            continue;
        }
        size_t nlen = strlen(dev->name);
        if (plen + nlen + 1 > len) {
            return -ENAMETOOLONG;
        }
        memcpy(devpath, prefix, plen);
        memcpy(devpath + plen, dev->name, nlen + 1);
        return 0;
    }
    return -ENODEV;
}

/*
 * Remount an existing filesystem in place (MNT_UPDATE / `mount -o remount`):
 * find the topmost mount at `path`, ask the filesystem to honour the new flags
 * (chiefly read-only <-> read-write), then record them on the mount.
 */
static int vfs_remount(const char *path, uint32_t flags) {
    struct mount *mp = NULL, *m;
    spinlock_acquire(&vfs_mount_lock);
    TAILQ_FOREACH(m, &mountlist, mnt_list) {
        if (strcmp(m->mnt_stat_path, path) == 0)
            mp = m;                 /* keep the last (topmost) match */
    }
    spinlock_release(&vfs_mount_lock);
    if (!mp)
        return -EINVAL;             /* nothing mounted at this path */

    uint32_t newflags = flags & ~MNT_UPDATE;
    fs_node_t *root = mp->mnt_node_root;

    /* Let the filesystem apply the change first; if it refuses (e.g. -EROFS
     * because it cannot safely write), leave the mount untouched. */
    if (root && root->remount) {
        int r = root->remount(root, newflags);
        if (r != 0)
            return r;
    }

    mp->mnt_flag = newflags;
    if (newflags & MNT_RDONLY) mp->mnt_stat.f_flags |= MNT_RDONLY;
    else                       mp->mnt_stat.f_flags &= ~MNT_RDONLY;
    return 0;
}

int vfs_mount_legacy(const char *device, const char *path, const char *type, uint32_t flags, void *data) {
    if (!type || !path) {
        return -EINVAL;
    }

    /* Reject conflicting flags before doing any work. */
    if ((flags & MNT_RDONLY) && (flags & MNT_ASYNC)) {
        return -EINVAL;
    }
    if ((flags & MNT_SYNCHRONOUS) && (flags & MNT_ASYNC)) {
        return -EINVAL;
    }

    /* MNT_UPDATE: don't create a new mount — re-apply flags to the existing
     * one (read-only <-> read-write conversion). */
    if (flags & MNT_UPDATE)
        return vfs_remount(path, flags);

    /*
     * NOTE: Critical-path overlay protection (preventing user code
     * from shadowing /dev, /proc, /sys with a hostile filesystem)
     * lives in kern_mount() — i.e. only on the syscall path.  The
     * kernel boot sequence calls vfs_mount_legacy() directly to
     * mount devfs/procfs/sysfs on those very paths, so the check
     * cannot live here.
     */

    // Find filesystem type
    filesystem_t *fs = filesystems;
    while (fs) {
        if (strcmp(fs->name, type) == 0) break;
        fs = fs->next;
    }
    if (!fs) {
        kprintf("VFS: mount(%s on %s): unknown filesystem type\n",
                type, path);
        return -EUNKNOWNFS;
    }

    /*
     * Translate a LABEL=<name> source into its "/dev/storage/<dev>" path by
     * scanning block devices for a matching on-disk volume label (Linux's
     * `mount LABEL=root /mnt`).  Everything downstream sees the device path.
     */
    char label_dev[80];
    if (device && strncmp(device, "LABEL=", 6) == 0) {
        int lr = vfs_resolve_label(device + 6, label_dev, sizeof(label_dev));
        if (lr != 0) {
            kprintf("VFS: mount: no filesystem with volume label '%s'\n",
                    device + 6);
            return lr;
        }
        kprintf("VFS: LABEL=%s resolved to %s\n", device + 6, label_dev);
        device = label_dev;
    }

    // Lookup device node if device path is specified
    fs_node_t *dev_node = NULL;
    if (device && device[0] == '/') {
        // Parse path like /dev/storage/ram0
        // Start from devfs if path begins with /dev/
        if (strncmp(device, "/dev/", 5) == 0) {
            // Find devfs mount
            fs_node_t *devfs_root = devfs_root_node_ptr;
            if (devfs_root) {
                const char *subpath = device + 5; // skip "/dev/"
                // Parse path components
                char component[64];
                fs_node_t *current = devfs_root;
                while (*subpath && current) {
                    // Get next component
                    int i = 0;
                    while (*subpath && *subpath != '/' && i < 63) {
                        component[i++] = *subpath++;
                    }
                    component[i] = '\0';
                    if (*subpath == '/') subpath++;
                    
                    if (i > 0 && current->finddir) {
                        current = current->finddir(current, component);
                        if (!current) break;
                    }
                }
                dev_node = current;
            }
        }

        /*
         * Early boot fallback: block devices can be registered before devfs
         * initializes its directory tree. If /dev/storage/<name> wasn't found
         * in devfs yet, resolve directly from the blkdev registry.
         */
        if (!dev_node && strncmp(device, "/dev/storage/", 13) == 0) {
            const char *dev_name = device + 13;
            if (*dev_name && strchr(dev_name, '/') == NULL) {
                blkdev_t *bdev = blkdev_get(dev_name);
                if (bdev) {
                    dev_node = &bdev->node;
                }
            }
        }
    }

    // Call mount implementation with device node as data
    fs_node_t *root = fs->mount(device, flags, dev_node ? dev_node : data);
    if (!root) {
        kprintf("VFS: mount(%s on %s): filesystem init failed\n",
                type, path);
        return -EIO;
    }

    // Handle Root Mount
    fs_node_t *mountpoint = NULL;
    if (strcmp(path, "/") == 0) {
        fs_root = root;
    } else {
        // Handle mount on existing directory
        // We must lookup the mount point
        mountpoint = vfs_lookup(fs_root, path);
        
        if (!mountpoint) {
            kprintf("VFS: mount(%s on %s): mount point not found\n",
                    type, path);
            if (root && root->unmount) {
                root->unmount(root);
            }
            return -ENOENT;
        }

        if ((mountpoint->flags & 0x7) != FS_DIRECTORY) {
             kprintf("VFS: mount(%s on %s): mount point is not a directory\n",
                     type, path);
             if (root->unmount) {
                 root->unmount(root);
             }
             return -ENOTDIR;
        }
        
        // Attach (locked — concurrent traversals must see both fields
        // updated together).
        spinlock_acquire(&vfs_mount_lock);
        mountpoint->ptr = root;
        mountpoint->flags |= FS_MOUNTPOINT;
        spinlock_release(&vfs_mount_lock);
    }
    
    // Register in generic mount list
    struct mount *mp = kmalloc(sizeof(struct mount));
    if (mp) {
        memset(mp, 0, sizeof(struct mount));

        // Populate mount structure
        strlcpy(mp->mnt_stat_path, path, sizeof(mp->mnt_stat_path));
        mp->mnt_stat_path[sizeof(mp->mnt_stat_path)-1] = '\0';

        strlcpy(mp->mnt_stat.f_mntonname, path, sizeof(mp->mnt_stat.f_mntonname));
        mp->mnt_stat.f_mntonname[sizeof(mp->mnt_stat.f_mntonname)-1] = '\0';

        if (device) {
            strlcpy(mp->mnt_stat.f_mntfromname, device, sizeof(mp->mnt_stat.f_mntfromname));
            mp->mnt_stat.f_mntfromname[sizeof(mp->mnt_stat.f_mntfromname)-1] = '\0';
        }

        if (type) {
            strlcpy(mp->mnt_stat.f_fstypename, type, sizeof(mp->mnt_stat.f_fstypename));
            mp->mnt_stat.f_fstypename[sizeof(mp->mnt_stat.f_fstypename)-1] = '\0';
        }

        mp->mnt_node_root = root;
        mp->mnt_node_covered = mountpoint;

        /* Snapshot the covered node's identity for mount crossing.
         * We cannot dereference mnt_node_covered later because
         * filesystems like ext2 reuse a fixed-size node cache;
         * the slot may be recycled and overwritten. */
        if (mountpoint) {
            mp->mnt_covered_ino = mountpoint->inode;
            mp->mnt_covered_mp  = mountpoint->mp;
        }

        // Set mount reference on root node
        root->mp = mp;

        spinlock_acquire(&vfs_mount_lock);
        TAILQ_INSERT_TAIL(&mountlist, mp, mnt_list);
        spinlock_release(&vfs_mount_lock);
        kprintf("VFS: mount table add %s (%s)\n",
                mp->mnt_stat.f_mntonname,
                mp->mnt_stat.f_fstypename[0] ? mp->mnt_stat.f_fstypename : "unknown");
    } else {
        kprintf("VFS: failed to allocate mount entry for %s (%s)\n",
                path ? path : "(null)", type ? type : "unknown");
    }

    return 0;
}

size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    if (node->read != 0) {
        size_t result = node->read(node, offset, size, buffer);
        node->atime = get_time();
        return result;
    } else
        return 0;
}

size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    if (node->write != 0) {
        size_t result = node->write(node, offset, size, buffer);
        int64_t now = get_time();
        node->mtime = now;
        node->ctime = now;
        return result;
    } else
        return 0;
}

/* Leak instrumentation — counts open_fs / close_fs invocations to
 * see whether they balance across many fork+exec+exit cycles.
 * Read by proc_exit when `debug=vm_leak` is on. */
unsigned long fs_open_count  = 0;
unsigned long fs_close_count = 0;

void open_fs(fs_node_t *node, uint8_t read, uint8_t write) {
    (void)read; (void)write;
    __sync_fetch_and_add(&fs_open_count, 1);
    if (node->open != 0)
        node->open(node);
}

void close_fs(fs_node_t *node) {
    /* Tolerate NULL: callers like file_close_ptr pass f->f_data, which
     * is intentionally NULL for stub-only file types (e.g. socket
     * fds from compat.c sock_alloc_fd) — without this guard, exiting
     * a process that opened such an fd panics in fd_close_all. */
    if (!node) return;
    __sync_fetch_and_add(&fs_close_count, 1);
    if (node->close != 0)
        node->close(node);
}

struct dirent *readdir_fs(fs_node_t *node, uint64_t index) {
    if ((node->flags & 0x7) == FS_DIRECTORY && node->readdir != 0) {
        struct dirent *de = node->readdir(node, index);
        if (de)
            node->atime = get_time();
        return de;
    } else
        return 0;
}



// Internal with follow control
static fs_node_t *finddir_fs_internal(fs_node_t *node, char *name, int depth, int follow_symlinks);

// Check if a filesystem is busy
static int vfs_is_busy(struct mount *mp) {
    if (!mp) return 0;

    // Check all processes
    FOREACH_PROC(p) {
        if (p->cwd_node && p->cwd_node->mp == mp) return 1;
        if (p->root_node && p->root_node->mp == mp) return 1;

        for (int j = 0; j < MAX_FD; j++) {
            if (p->fds[j] && p->fds[j]->f_data) {
                fs_node_t *fn = (fs_node_t*)p->fds[j]->f_data;
                if (fn->mp == mp) return 1;
            }
        }
    }
    return 0;
}

fs_node_t *finddir_fs(fs_node_t *node, char *name) {
    return finddir_fs_internal(node, name, 0, 1);
}

/*
 * Maximum symlink recursion depth.  Linux uses 8, but our stack frames
 * are larger (vfs_lookup's local 512-byte ppath buffer dominates) and
 * each "absolute symlink" cycle creates 4–6 nested frames between
 * finddir_fs_internal's absolute branch, vfs_lookup's prefix recursion,
 * and the directory walk loop.  Six full cycles already overflow the
 * 8 KB kernel stack — deeper here means an unrecoverable PF in kernel
 * mode rather than a clean ELOOP.  Drop to 4 until we shrink the
 * frame sizes (or move ppath to kmalloc).
 */
#define MAX_SYMLINK_DEPTH 4

static fs_node_t *finddir_fs_internal(fs_node_t *node, char *name, int depth, int follow_symlinks) {
    if (!node) return 0; // Safety

    // If this is a mountpoint, cross into the mounted filesystem
    node = vfs_cross_mountpoint(node);

    if ((node->flags & 0x7) == FS_DIRECTORY && node->finddir != 0) {
        fs_node_t *result = node->finddir(node, name);

        if (result && !result->mp) {
            result->mp = node->mp;
        }

        /*
         * Mountpoint crossing for terminal lookups:
         * path "/dev" should resolve to devfs root, not the covered directory.
         */
        result = vfs_cross_mountpoint(result);

        // Resolve symlinks (with depth limit)
        if (result && (result->flags & 0x7) == FS_SYMLINK && result->readlink) {
            // Check if we should follow symlinks
            if (!follow_symlinks) return result;

            // Check recursion depth limit
            if (depth >= MAX_SYMLINK_DEPTH) {
                // Too many symlink levels - return NULL to signal ELOOP (finding #32)
                return NULL;
            }
            
            char link_target[256];
            int len = result->readlink(result, link_target, sizeof(link_target) - 1);
            if (len <= 0) {
                return NULL;
            }
            if ((size_t)len >= sizeof(link_target)) return NULL;
            link_target[len] = '\0';

            {
                // Resolve the target path.
                //
                // Both branches walk a full path (which may have
                // multiple '/'-separated components, ".." entries,
                // etc.), so both must call vfs_lookup() — the
                // relative branch used to call finddir_fs_internal
                // with the whole "../../libexec/ld.elf_so" string as
                // a single entry name, asking the FS for a child
                // literally named that, which obviously failed.
                //
                // Resolution base for relative targets is the
                // symlink's parent directory (`node`), not fs_root.
                //
                // Use the per-thread counter to bound the chain
                // across the re-entry into vfs_lookup().
                fs_node_t *target;
                if (current_thread &&
                    current_thread->vfs_symlink_depth >= MAX_SYMLINK_DEPTH) {
                    return NULL;
                }
                if (current_thread) current_thread->vfs_symlink_depth++;
                if (link_target[0] == '/') {
                    target = vfs_lookup(fs_root, link_target);
                } else {
                    target = vfs_lookup(node, link_target);
                }
                if (current_thread) current_thread->vfs_symlink_depth--;

                if (target) {
                    return target;
                }
                // If symlink resolution fails, return the symlink node itself (or NULL? Linux returns ENOENT)
                // Returning result roughly mimics getting the link itself so we can see it exists but is broken?
                // Correct behavior is usually ENOENT, but returning the node is safer for some "ls" ops.
            }
        }
        return result;
    }
    return 0;
}

// Lookup a path from a root node
fs_node_t *vfs_lookup(fs_node_t *root, const char *path) {
    if (!path || !root) return NULL;
    
    /*
     * Implement personality shadowing:
     * Non-native processes try /perso/<name>/<path> first.
     *
     * Skip when the path is already under /perso/ — otherwise the
     * recursive lookup below re-enters this branch and rewrites the
     * path again ("/perso/linux/perso/linux/..."), each call adding
     * ~800 bytes of stack, blowing past the 8 KB kernel stack and
     * corrupting saved return addresses on the way down.  The earlier
     * "Internal direct lookup to avoid infinite recursion" comment
     * promised this guard but the code never enforced it.
     */
    if (current_process && current_process->perso_id != 0 && path[0] == '/' &&
        strncmp(path, "/perso/", 7) != 0) {
        const char *pname = perso_name(current_process->perso_id);
        if (pname) {
            char ppath[512];
            char lname[32];
            size_t k, count = 0;
            for (k = 0; pname[k] && count < 31; k++) {
                char c = pname[k];
                if ((c >= 'A' && c <= 'Z')) {
                    lname[count++] = c + 32;
                } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                    lname[count++] = c;
                }
                // Skip everything else (slashes, dots, etc)
            }
            lname[count] = '\0';

            if (count > 0) {
                snprintf(ppath, sizeof(ppath), "/perso/%s%s", lname, path);
                fs_node_t *pnode = vfs_lookup(fs_root, ppath);
                if (pnode) return pnode;
            }
        }
    }

    if (path[0] == '/') path++; // Skip leading /
    if (path[0] == '\0') return root; // Root itself
    
    fs_node_t *current = root;
    char component[256];
    const char *p = path;
    
    while (*p) {
        // Extract next path component
        int i = 0;
        while (*p && *p != '/' && i < 255) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        if (i == 0) {
            if (*p == '/') p++;
            continue;
        }

        if (component[0] == '.' && component[1] == '\0') {
            if (*p == '/') p++;
            continue;
        }

        if (component[0] == '.' && component[1] == '.' && component[2] == '\0') {
            /* At a mount root, ".." escapes to the mountpoint's parent
             * (e.g. /proc/.. -> /); otherwise the normal in-fs parent. */
            fs_node_t *esc = vfs_mount_root_parent(current);
            fs_node_t *parent = esc ? esc : finddir_fs(current, "..");
            if (parent) current = parent;
            if (*p == '/') p++;
            continue;
        }
        
        // Lookup this component
        current = finddir_fs(current, component);
        if (!current) return NULL;

        /*
         * Mount crossing by node identity.
         *
         * Filesystems like ext2 return freshly-allocated fs_node_t
         * instances on each finddir, so pointer comparison and the
         * FS_MOUNTPOINT flag alone are unreliable.  Compare the
         * (mp, inode) tuple — which uniquely identifies a directory
         * across the entire VFS — against each mount's snapshot of
         * the covered directory's identity taken at mount time.
         * This works for both absolute and relative path lookups.
         */
        {
            struct mount *mnt;
            spinlock_acquire(&vfs_mount_lock);
            TAILQ_FOREACH(mnt, &mountlist, mnt_list) {
                if (mnt->mnt_covered_ino != 0 &&
                    current->inode == mnt->mnt_covered_ino &&
                    current->mp == mnt->mnt_covered_mp) {
                    current = mnt->mnt_node_root;
                    break;
                }
            }
            spinlock_release(&vfs_mount_lock);
        }

        /* Flag-based fast path for nodes with FS_MOUNTPOINT set */
        current = vfs_cross_mountpoint(current);

        // Skip trailing slash
        if (*p == '/') p++;
    }

    return current;
}

fs_node_t *vfs_lookup_ref(fs_node_t *root, const char *path) {
    fs_node_t *n = vfs_lookup(root, path);
    if (n) open_fs(n, 1, 0);
    return n;
}

fs_node_t *vfs_lookup_lstat_ref(fs_node_t *root, const char *path) {
    fs_node_t *n = vfs_lookup_lstat(root, path);
    if (n) open_fs(n, 1, 0);
    return n;
}

fs_node_t *vfs_lookup_lstat(fs_node_t *root, const char *path) {
    if (!path || !root) return NULL;
    if (path[0] == '/') path++; // Skip leading /
    if (path[0] == '\0') return root; // Root itself
    
    fs_node_t *current = root;
    char component[256];
    const char *p = path;
    
    while (*p) {
        // Extract next path component
        int i = 0;
        while (*p && *p != '/' && i < 255) {
            component[i++] = *p++;
        }
        component[i] = '\0';
        
        if (i == 0) {
            if (*p == '/') p++;
            continue;
        }

        if (component[0] == '.' && component[1] == '\0') {
            if (*p == '/') p++;
            continue;
        }

        if (component[0] == '.' && component[1] == '.' && component[2] == '\0') {
            /* At a mount root, ".." escapes to the mountpoint's parent
             * (e.g. /proc/.. -> /); otherwise the normal in-fs parent. */
            fs_node_t *esc = vfs_mount_root_parent(current);
            fs_node_t *parent = esc ? esc : finddir_fs_internal(current, "..", 0, 1);
            if (parent) current = parent;
            if (*p == '/') p++;
            continue;
        }
        
        // Check if this is the last component
        int is_last = (*p == '\0');
        
        // Lookup this component
        // If last, DO NOT follow symlinks
        current = finddir_fs_internal(current, component, 0, !is_last);
        if (!current) return NULL;

        /* Mount crossing by node identity (see vfs_lookup comment) */
        {
            struct mount *mnt;
            spinlock_acquire(&vfs_mount_lock);
            TAILQ_FOREACH(mnt, &mountlist, mnt_list) {
                if (mnt->mnt_covered_ino != 0 &&
                    current->inode == mnt->mnt_covered_ino &&
                    current->mp == mnt->mnt_covered_mp) {
                    current = mnt->mnt_node_root;
                    break;
                }
            }
            spinlock_release(&vfs_mount_lock);
        }

        /* Flag-based fast path for nodes with FS_MOUNTPOINT set */
        current = vfs_cross_mountpoint(current);

        // Skip trailing slash
        if (*p == '/') p++;
    }
    
    return current;
}


/*
 * POSIX permission check.  Mode is a bitmask of R_OK (4) / W_OK (2) /
 * X_OK (1).  The caller passes the process's primary uid / gid plus
 * its supplementary group list; we consider all of them when picking
 * which permission class applies.
 *
 * POSIX class selection is strictly tiered: owner class is checked
 * if uid matches, otherwise group class if the file's gid appears in
 * { primary gid } ∪ supplementary groups, otherwise other.  Once a
 * class is selected we test only that class — even if a less-
 * privileged class would grant access, POSIX requires the deny.
 *
 * Root (uid 0) bypasses read/write checks but still needs at least
 * one execute bit set somewhere when X_OK is requested.
 */
int vfs_check_permissions_groups(fs_node_t *node, uint32_t uid, uint32_t gid,
                                 const uint32_t *groups, int ngroups, int mode)
{
    if (uid == 0) {
        if ((mode & 1) && (node->mask & 0111) == 0) {
            return -1;
        }
        return 0;
    }

    uint32_t mask = 0;
    if (uid == node->uid) {
        if (mode & 4) mask |= 0400;
        if (mode & 2) mask |= 0200;
        if (mode & 1) mask |= 0100;
    } else {
        int in_group = (gid == node->gid);
        if (!in_group && groups != NULL) {
            for (int i = 0; i < ngroups; i++) {
                if (groups[i] == node->gid) {
                    in_group = 1;
                    break;
                }
            }
        }

        if (in_group) {
            if (mode & 4) mask |= 0040;
            if (mode & 2) mask |= 0020;
            if (mode & 1) mask |= 0010;
        } else {
            if (mode & 4) mask |= 0004;
            if (mode & 2) mask |= 0002;
            if (mode & 1) mask |= 0001;
        }
    }

    return (node->mask & mask) == mask ? 0 : -1;
}

/*
 * Legacy shim: no supplementary groups.  Kept for callers that don't
 * have a process context (rare), and so the ABI doesn't break for
 * out-of-tree users.  New code should call the _groups form.
 */
int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode) {
    return vfs_check_permissions_groups(node, uid, gid, NULL, 0, mode);
}

int vfs_may_open_groups(fs_node_t *node, uint32_t uid, uint32_t gid,
                        const uint32_t *groups, int ngroups, int flags) {
    int mode = 0;
    int accmode;

    if (node == NULL) {
        return -1;
    }

    accmode = flags & O_ACCMODE;
    if (accmode == O_RDONLY) {
        mode |= 4;
    } else if (accmode == O_WRONLY) {
        mode |= 2;
    } else if (accmode == O_RDWR) {
        mode |= 4 | 2;
    }

    if (flags & O_TRUNC) {
        mode |= 2;
    }

    if (mode == 0) {
        return 0;
    }

    return vfs_check_permissions_groups(node, uid, gid, groups, ngroups, mode);
}

int vfs_may_open(fs_node_t *node, uint32_t uid, uint32_t gid, int flags) {
    return vfs_may_open_groups(node, uid, gid, NULL, 0, flags);
}

int vfs_chmod_node(fs_node_t *node, uint32_t mode) {
    uint32_t old_mask;
    int64_t old_ctime;
    int ret;

    if (node == NULL) {
        return -EINVAL;
    }

    old_mask = node->mask;
    old_ctime = node->ctime;

    node->mask = mode & 07777U;
    node->ctime = get_time();

    if (node->chmod == NULL) {
        return 0;
    }

    ret = node->chmod(node, node->mask);
    if (ret != 0) {
        node->mask = old_mask;
        node->ctime = old_ctime;
        return ret;
    }

    return 0;
}


int readlink_fs(fs_node_t *node, char *buf, size_t size) {
    if (node && node->readlink) {
        return node->readlink(node, buf, size);
    }
    return -ENOSYS;
}

int symlink_fs(fs_node_t *parent, const char *target, const char *name) {
    if (parent && parent->symlink) {
        return parent->symlink(parent, target, name);
    }
    return -ENOSYS;
}

int link_fs(fs_node_t *parent, fs_node_t *source, const char *name) {
    if (parent && parent->link) {
        return parent->link(parent, source, name);
    }
    return -ENOSYS;
}

int unlink_fs(fs_node_t *node, const char *name) {
    if (node && node->unlink) {
        return node->unlink(node, name);
    }
    return -ENOSYS;
}

int rmdir_fs(fs_node_t *node, const char *name) {
    if (node && node->rmdir) {
        return node->rmdir(node, name);
    }
    return -ENOSYS;
}

int rename_fs(fs_node_t *old_parent, const char *old_name, fs_node_t *new_parent, const char *new_name) {
    if (old_parent && old_parent->rename) {
        return old_parent->rename(old_parent, old_name, new_parent, new_name);
    }
    return -ENOSYS;
}

int statfs_fs(fs_node_t *node, struct statfs *buf) {
    if (node && node->statfs) {
        return node->statfs(node, buf);
    }
    return -ENOSYS;
}

/*
 * Synthesize a struct statvfs from the underlying filesystem's statfs.
 *
 * Filesystems only implement node->statfs (BSD form) — the POSIX form
 * is derived here so individual filesystem drivers don't have to grow a
 * second method.  All non-trivially-derivable fields are taken from
 * statfs as-is.
 */
int statvfs_fs(fs_node_t *node, struct statvfs *buf) {
    if (!node || !buf) {
        return -EINVAL;
    }

    struct statfs sf;
    int err = statfs_fs(node, &sf);
    if (err) {
        return err;
    }

    memset(buf, 0, sizeof(*buf));
    /* Use f_iosize (or fall back to f_bsize) as the I/O hint.  Both
     * fields exist in our struct statfs; the BSD convention is that
     * f_bsize is the fundamental block and f_iosize is the I/O hint. */
    buf->f_bsize    = (unsigned long)(sf.f_iosize ? sf.f_iosize : sf.f_bsize);
    buf->f_frsize   = (unsigned long)sf.f_bsize;
    buf->f_blocks   = sf.f_blocks;
    buf->f_bfree    = sf.f_bfree;
    buf->f_bavail   = sf.f_bavail;
    buf->f_files    = sf.f_files;
    buf->f_ffree    = sf.f_ffree;
    buf->f_favail   = sf.f_ffree; /* same value — no separate quota */
    buf->f_fsid     = (unsigned long)sf.f_fsid;
    buf->f_flag     = 0;
    if (sf.f_flags & MNT_RDONLY) buf->f_flag |= ST_RDONLY;
    if (sf.f_flags & MNT_NOSUID) buf->f_flag |= ST_NOSUID;
    /* Conservative default: most filesystems we host accept up to 255
     * bytes per name component.  Filesystems that disagree should
     * extend their statfs callback. */
    buf->f_namemax  = 255;
    strlcpy(buf->f_fstypename, sf.f_fstypename, sizeof(buf->f_fstypename));
    strlcpy(buf->f_basetype,   sf.f_fstypename, sizeof(buf->f_basetype));
    return 0;
}

static int vfs_resolve_parent_path(const char *path, fs_node_t **parent_out,
                                   char *name_out, size_t name_out_size) {
    fs_node_t *root;
    fs_node_t *cwd;
    fs_node_t *parent = NULL;
    const char *last_slash;
    char dir[256];

    if (!path || !parent_out || !name_out || name_out_size == 0) {
        return -EINVAL;
    }
    if (path[0] == '\0') {
        return -EINVAL;
    }

    root = (current_process && current_process->root_node) ? current_process->root_node : fs_root;
    cwd = (current_process && current_process->cwd_node) ? current_process->cwd_node : root;
    if (!root || !cwd) {
        return -ENOENT;
    }

    last_slash = vfs_strrchr(path, '/');
    if (!last_slash) {
        parent = cwd;
        if (strlcpy(name_out, path, name_out_size) >= name_out_size) {
            return -ENAMETOOLONG;
        }
    } else if (last_slash == path) {
        parent = root;
        if (strlcpy(name_out, path + 1, name_out_size) >= name_out_size) {
            return -ENAMETOOLONG;
        }
    } else {
        size_t dirlen = (size_t)(last_slash - path);
        fs_node_t *lookup_root = (path[0] == '/') ? root : cwd;

        if (dirlen >= sizeof(dir)) {
            return -ENAMETOOLONG;
        }
        memcpy(dir, path, dirlen);
        dir[dirlen] = '\0';
        if (strlcpy(name_out, last_slash + 1, name_out_size) >= name_out_size) {
            return -ENAMETOOLONG;
        }
        parent = vfs_lookup(lookup_root, dir);
    }

    if (!parent) {
        return -ENOENT;
    }
    if (name_out[0] == '\0') {
        return -EINVAL;
    }

    *parent_out = parent;
    return 0;
}

int mknod_fs(fs_node_t *node, const char *name, uint16_t mode, uint32_t dev) {
    if (node && node->mknod) {
        return node->mknod(node, name, mode, dev);
    }
    return -ENOSYS;
}

void *mmap_fs(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    if (node && node->mmap) {
        return node->mmap(node, addr, length, prot, flags, offset);
    }
    return (void *)-ENOSYS;
}

int poll_fs(fs_node_t *node, void *waiter) {
    if (!node) return POLLNVAL;

    if (node->poll) {
        return node->poll(node, waiter);
    }

    // Default behavior: Regular files and directories are always readable/writable
    if ((node->flags & 0x7) == FS_FILE || (node->flags & 0x7) == FS_DIRECTORY) {
        return POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM;
    }

    return 0;
}

/* setattr_fs / getattr_fs — generic dispatch through the fs_node
 * op vector.  If the backend implements setattr, call it; that
 * function is responsible for both persisting the change and
 * mirroring it back into the in-memory fs_node fields.  If the
 * backend lacks a setattr op (devfs, procfs, sysfs, ramfs), apply
 * the change to the in-memory node only — that's enough to make
 * stat() report sane timestamps in the lifetime of the mount.
 *
 * The "set this field to current time" semantics (FS_ATTR_*_NOW)
 * are resolved here once, then handed to the backend so backends
 * don't each have to call get_realtime themselves.  */
int setattr_fs(fs_node_t *node, const struct fs_attr *a) {
    if (!node || !a) return -EINVAL;

    struct fs_attr eff = *a;
    if (eff.mask & (FS_ATTR_ATIME_NOW | FS_ATTR_MTIME_NOW)) {
        int64_t now = (int64_t)kern_time(NULL);
        if (eff.mask & FS_ATTR_ATIME_NOW) {
            eff.atime = now;
            eff.mask  = (eff.mask | FS_ATTR_ATIME) & ~FS_ATTR_ATIME_NOW;
        }
        if (eff.mask & FS_ATTR_MTIME_NOW) {
            eff.mtime = now;
            eff.mask  = (eff.mask | FS_ATTR_MTIME) & ~FS_ATTR_MTIME_NOW;
        }
    }

    if (node->setattr) {
        return node->setattr(node, &eff);
    }

    /* Generic in-memory fallback.  Updates the cached fields so a
     * follow-up stat() sees the change; persistence requires the
     * backend's setattr op.  */
    if (eff.mask & FS_ATTR_ATIME) node->atime = eff.atime;
    if (eff.mask & FS_ATTR_MTIME) node->mtime = eff.mtime;
    if (eff.mask & FS_ATTR_CTIME) node->ctime = eff.ctime;
    if (eff.mask & FS_ATTR_MODE)  node->mask  = eff.mode & 07777;
    if (eff.mask & FS_ATTR_UID)   node->uid   = eff.uid;
    if (eff.mask & FS_ATTR_GID)   node->gid   = eff.gid;
    /* SIZE is more invasive (truncation); only honored if the
     * backend implements it explicitly.  */
    return 0;
}

int getattr_fs(fs_node_t *node, struct fs_attr *a) {
    if (!node || !a) return -EINVAL;
    if (node->getattr) return node->getattr(node, a);

    /* Generic: pull from the cached fs_node fields.  */
    a->mask  = FS_ATTR_ATIME | FS_ATTR_MTIME | FS_ATTR_CTIME |
               FS_ATTR_MODE  | FS_ATTR_UID   | FS_ATTR_GID   |
               FS_ATTR_SIZE;
    a->atime = node->atime;
    a->mtime = node->mtime;
    a->ctime = node->ctime;
    a->mode  = node->mask;
    a->uid   = node->uid;
    a->gid   = node->gid;
    a->size  = node->length;
    return 0;
}


// Helper to find last occurrence of character
static char *vfs_strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    if (*s == c) last = s; // check terminator... unlikely but standard logic
    return (char *)last;
}

int vfs_mkdir(const char *path, uint16_t permission) {
    fs_node_t *parent_node = NULL;
    char name[128];
    int ret;

    ret = vfs_resolve_parent_path(path, &parent_node, name, sizeof(name));
    if (ret != 0) {
        return ret;
    }
    if ((parent_node->flags & 0x7) != FS_DIRECTORY) {
        return -ENOTDIR;
    }
    if (parent_node->finddir && parent_node->finddir(parent_node, name) != NULL) {
        return -EEXIST;
    }
    if (!parent_node->mkdir) {
        return -EOPNOTSUPP;
    }

    return parent_node->mkdir(parent_node, name, permission);
}

int vfs_rmdir(const char *path) {
    fs_node_t *parent_node = NULL;
    char name[128];
    int ret;

    ret = vfs_resolve_parent_path(path, &parent_node, name, sizeof(name));
    if (ret != 0) {
        return ret;
    }
    if ((parent_node->flags & 0x7) != FS_DIRECTORY) {
        return -ENOTDIR;
    }
    
    // Check if it exists
    fs_node_t *node = parent_node->finddir(parent_node, name);
    if (!node) {
        return -ENOENT;
    }
    if ((node->flags & 0x7) != FS_DIRECTORY) {
        return -ENOTDIR;
    }
    
    if (!parent_node->rmdir) {
        return -EOPNOTSUPP;
    }

    return parent_node->rmdir(parent_node, name);
}

int vfs_mknod(const char *path, uint16_t mode, uint32_t dev) {
    fs_node_t *parent_node = NULL;
    char name[128];
    int ret;

    ret = vfs_resolve_parent_path(path, &parent_node, name, sizeof(name));
    if (ret != 0) {
        return ret;
    }
    if ((parent_node->flags & 0x7) != FS_DIRECTORY) {
        return -ENOTDIR;
    }
    if (parent_node->finddir && parent_node->finddir(parent_node, name) != NULL) {
        return -EEXIST;
    }
    if (!parent_node->mknod) {
        return -EOPNOTSUPP;
    }

    return parent_node->mknod(parent_node, name, mode, dev);
}

int vfs_unmount_legacy(const char *path) {
    return vfs_unmount_legacy_flags(path, 0);
}

int vfs_unmount_legacy_flags(const char *path, int flags) {
    if (!path) return -EINVAL;
    
    /*
     * Lookup mount point (the directory that was mounted ON).
     * We need to find the node that has the mount point flag.
     * If we use standard lookup, we might traverse into the mounted filesystem.
     * But we want the COVERED node.
     *
     * NOTE: Standard lookup usually traverses mount points.
     * We need a way to lookup without traversing the LAST mount point.
     *
     * For now, let's assume we can match by path string if we had a mount list.
     * But we didn't implement a global mount list with paths yet (except implicit tree).
     *
     * Workaround: Use standard lookup lstat. No, that stops at symlinks.
     * We need to implement a lookup that returns the mount point node, not the root of the filesystem.
     *
     * Actually, if we mount on /mnt, the node at /mnt (in root filesystem) has the mount point flag.
     * Its internal pointer points to the new root.
     *
     * Strategy: Lookup parent directory, then find entry, but manually check flags
     * without invoking the automatic traversal (or utilize a specialized finding function).
     */
    
    char path_buf[256];
    strlcpy(path_buf, path, sizeof(path_buf));
    path_buf[255] = '\0';
    
    // Split path
    char *last_slash = vfs_strrchr(path_buf, '/');
    char *name = NULL;
    fs_node_t *parent_node = NULL;
    
    if (last_slash) {
        *last_slash = '\0';
        name = last_slash + 1;
        if (*name == '\0') return -1; // "foo/" -> invalid for unmount usually
        
        if (path_buf[0] == '\0') {
             // "/foo" -> parent is root
             parent_node = fs_root;
        } else {
             parent_node = vfs_lookup(fs_root, path_buf);
        }
    } else {
        // "foo" -> parent is root (cwd not fully supported here yet)
        parent_node = fs_root;
        name = path_buf;
    }
    
    if (!parent_node) return -1;
    if ((parent_node->flags & 0x7) != FS_DIRECTORY) return -1;
    
    // Now find child 'name' in 'parent_node'
    // But DO NOT traverse if it is a mountpoint.
    // We need access to the underlying finddir.
    
    if (!parent_node->finddir) return -1;
    
    fs_node_t *mountpoint = parent_node->finddir(parent_node, name);
    if (!mountpoint) return -1;
    
    // Check if it is a mountpoint
    if (!(mountpoint->flags & FS_MOUNTPOINT)) {
        // Not a mountpoint
        return -22; // EINVAL
    }
    
    // Capture root of mounted fs
    fs_node_t *root = mountpoint->ptr;

    // Find the mount structure first to check busy status
    struct mount *target_mp = NULL;
    struct mount *mp_iter;
    spinlock_acquire(&vfs_mount_lock);
    TAILQ_FOREACH(mp_iter, &mountlist, mnt_list) {
        if (mp_iter->mnt_node_covered == mountpoint && mp_iter->mnt_node_root == root) {
            target_mp = mp_iter;
            break;
        }
    }
    spinlock_release(&vfs_mount_lock);

    if (target_mp) {
        if (vfs_is_busy(target_mp)) {
            /* MNT_FORCE bypasses the busy check.  Caller is asserting
             * that they understand stale fds will start failing once
             * the underlying fs is gone. */
            if (!(flags & MNT_FORCE)) {
                return -EBUSY;
            }
            kprintf("VFS: forced unmount of %s while busy (MNT_FORCE)\n",
                    path);
        }
    }

    // Detach (locked so concurrent vfs_cross_mountpoint either sees
    // a fully attached mount or a fully detached node — never the
    // FS_MOUNTPOINT flag with ptr cleared, which would race-deref NULL).
    spinlock_acquire(&vfs_mount_lock);
    mountpoint->flags &= ~FS_MOUNTPOINT;
    mountpoint->ptr = NULL;
    spinlock_release(&vfs_mount_lock);
    
    // Cleanup fs instance
    if (root && root->unmount) {
        root->unmount(root);
    }
    
    // Remove from mount list and free.  Under vfs_mount_lock so a concurrent
    // path-lookup traversal never walks the TAILQ mid-unlink or dereferences
    // a just-freed mount (A28).  kfree stays under the lock (non-sleeping UMA
    // free path) — once TAILQ_REMOVE'd under the lock the mount is unreachable.
    if (target_mp) {
        spinlock_acquire(&vfs_mount_lock);
        TAILQ_REMOVE(&mountlist, target_mp, mnt_list);
        kfree(target_mp, sizeof(struct mount));
        spinlock_release(&vfs_mount_lock);
    }

    return 0;
}

/* Unmount-ordering key for a mount-point path; larger sorts first:
 *   /dev      -> -1  : unmounted dead last, after root even, so the
 *                      device nodes (console, boot disk) stay
 *                      reachable through the entire teardown
 *   /         ->  0  : root
 *   otherwise ->  count of '/' separators (its depth)
 * so a child filesystem is always torn down before the parent it
 * nests under, and /dev outlives every other mount. */
static int vfs_mount_depth(const char *p) {
    if (strcmp(p, "/dev") == 0) return -1;
    if (p[0] == '/' && p[1] == '\0') return 0;   /* root */
    int d = 0;
    for (const char *c = p; *c; c++)
        if (*c == '/') d++;
    return d;
}

/*
 * vfs_unmount_all — unmount every mounted filesystem, deepest mount
 * point first.  Called from sys_reboot() on the way down: a nested
 * mount is always unwound before the parent it sits on, so each
 * backing store is left clean.  Forced, because by reboot time every
 * process is already being killed and a lingering reference must not
 * veto power-off.  /dev is unmounted dead last so device nodes stay
 * usable throughout; the root "/" is declined by
 * vfs_unmount_legacy_flags, which is fine — sync() already flushed it
 * and the machine is about to stop.
 */
void vfs_unmount_all(void) {
    static char paths[32][128];
    int n = 0;
    struct mount *mp;

    spinlock_acquire(&vfs_mount_lock);
    TAILQ_FOREACH(mp, &mountlist, mnt_list) {
        if (n >= 32) break;
        strlcpy(paths[n], mp->mnt_stat_path, sizeof(paths[n]));
        paths[n][sizeof(paths[n]) - 1] = '\0';
        n++;
    }
    spinlock_release(&vfs_mount_lock);

    /* Insertion sort, deepest path first.  n is a handful of mounts. */
    for (int i = 1; i < n; i++) {
        char key[128];
        strlcpy(key, paths[i], sizeof(key));
        key[sizeof(key) - 1] = '\0';
        int j = i - 1;
        while (j >= 0 && vfs_mount_depth(paths[j]) < vfs_mount_depth(key)) {
            strlcpy(paths[j + 1], paths[j], sizeof(paths[j + 1]));
            j--;
        }
        strlcpy(paths[j + 1], key, sizeof(paths[j + 1]));
    }

    for (int i = 0; i < n; i++) {
        kprint("reboot: unmounting ");
        kprint(paths[i][0] ? paths[i] : "/");
        kprint("\n");
        vfs_unmount_legacy_flags(paths[i], MNT_FORCE);
    }
}

/*
 * vfs_force_unmount_dev - force-unmount every filesystem backed by a block
 * device that has just been removed (hot-unplug).  Called from
 * blkdev_unregister() before the blkdev struct is torn down, so a filesystem
 * mounted on a vanished device does not later dereference a freed/zeroed
 * backing blkdev and fault.  The device is already marked dead (I/O returns
 * -EIO), so the unmount's flush fails fast rather than hanging on gone media.
 *
 * A mount is matched to the device by the basename of its f_mntfromname (the
 * "/dev/storage/<name>" the fs was mounted from) against dev->name.  MNT_FORCE
 * mutates the mount list, so we re-scan from the top after each unmount, and
 * bail if a target mount survives an unmount attempt (avoids an infinite loop).
 */
void vfs_force_unmount_dev(struct blkdev *dev) {
    if (!dev || dev->name[0] == '\0') return;

    char last[128];
    last[0] = '\0';

    for (;;) {
        struct mount *mp;
        char path[128];
        path[0] = '\0';

        spinlock_acquire(&vfs_mount_lock);
        TAILQ_FOREACH(mp, &mountlist, mnt_list) {
            const char *from = mp->mnt_stat.f_mntfromname;
            const char *base = strrchr(from, '/');
            base = base ? base + 1 : from;
            if (base[0] != '\0' && strcmp(base, dev->name) == 0 &&
                mp->mnt_stat_path[0] == '/') {
                strlcpy(path, mp->mnt_stat_path, sizeof(path));
                break;
            }
        }
        spinlock_release(&vfs_mount_lock);

        if (path[0] == '\0')
            return;                     /* no (more) mounts on this device */
        if (strcmp(path, last) == 0) {  /* previous force-unmount didn't remove it */
            kprintf("VFS: could not force-unmount %s on removed device %s\n",
                    path, dev->name);
            return;
        }
        strlcpy(last, path, sizeof(last));

        kprintf("VFS: block device %s removed -> forced unmount of %s\n",
                dev->name, path);
        vfs_unmount_legacy_flags(path, MNT_FORCE);
    }
}
