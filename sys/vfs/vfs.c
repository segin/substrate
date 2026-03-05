#include <vfs/vfs.h>
#include <vfs/vnode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/proc.h>

#include <string.h>
#include <kern/console.h>
#include <stdio.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <drivers/storage/blkdev.h>
#include <vm/vm_kmem.h>

struct mountlist mountlist;
fs_node_t *fs_root = 0; 
struct vnode *rootvnode = NULL;

static filesystem_t *filesystems = NULL;

// External filesystem init functions
extern void ext2_init(void);
extern void fat_init(void);
extern void exfat_init(void);
extern void minix_init(void);
extern void udf_init(void);
extern void devfs_init(void);
extern void procfs_init(void);
extern void sysfs_init(void);
extern void fuse_init(void);
extern void fuse_fs_init(void);
extern void p9_init(void);
extern void pseudo_init(void);
extern void full_init(void);

void vfs_init(void) {
    kprint("VFS: Initializing...\n");
    
    // Register real filesystem drivers
    ext2_init();
    fat_init();
    exfat_init();
    minix_init();
    udf_init();
    
    // Register pseudo-filesystems
    devfs_init();
    procfs_init();
    sysfs_init();
    pseudo_init();
    full_init();
    
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

int vfs_mount_legacy(const char *device, const char *path, const char *type, uint32_t flags, void *data) {
    // Find filesystem type
    filesystem_t *fs = filesystems;
    while (fs) {
        if (strcmp(fs->name, type) == 0) break;
        fs = fs->next;
    }
    if (!fs) {
        kprintf("VFS: Unknown filesystem type: %s\n", type);
        return -1;
    }

    // Lookup device node if device path is specified
    fs_node_t *dev_node = NULL;
    if (device && device[0] == '/') {
        // Parse path like /dev/storage/ram0
        // Start from devfs if path begins with /dev/
        if (strncmp(device, "/dev/", 5) == 0) {
            // Find devfs mount
            extern fs_node_t *devfs_root_node_ptr;
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
    if (!root) return -1;

    // Handle Root Mount
    fs_node_t *mountpoint = NULL;
    if (strcmp(path, "/") == 0) {
        fs_root = root;
    } else {
        // Handle mount on existing directory
        // We must lookup the mount point
        mountpoint = vfs_lookup(fs_root, path);
        
        if (!mountpoint) {
            kprintf("VFS: Mount point not found: %s\n", path);
            if (root && root->unmount) {
                root->unmount(root);
            }
            return -1; 
        }

        if ((mountpoint->flags & 0x7) != FS_DIRECTORY) {
             kprintf("VFS: Mount point is not a directory: %s\n", path);
             if (root->unmount) {
                 root->unmount(root);
             }
             return -1;
        }
        
        // Attach
        mountpoint->flags |= FS_MOUNTPOINT;
        mountpoint->ptr = root;
    }
    
    // Register in generic mount list
    struct mount *mp = kmalloc(sizeof(struct mount));
    if (mp) {
        memset(mp, 0, sizeof(struct mount));

        // Populate mount structure
        strncpy(mp->mnt_stat_path, path, sizeof(mp->mnt_stat_path));
        mp->mnt_stat_path[sizeof(mp->mnt_stat_path)-1] = '\0';

        strncpy(mp->mnt_stat.f_mntonname, path, sizeof(mp->mnt_stat.f_mntonname));
        mp->mnt_stat.f_mntonname[sizeof(mp->mnt_stat.f_mntonname)-1] = '\0';

        if (device) {
            strncpy(mp->mnt_stat.f_mntfromname, device, sizeof(mp->mnt_stat.f_mntfromname));
            mp->mnt_stat.f_mntfromname[sizeof(mp->mnt_stat.f_mntfromname)-1] = '\0';
        }

        if (type) {
            strncpy(mp->mnt_stat.f_fstypename, type, sizeof(mp->mnt_stat.f_fstypename));
            mp->mnt_stat.f_fstypename[sizeof(mp->mnt_stat.f_fstypename)-1] = '\0';
        }

        mp->mnt_node_root = root;
        mp->mnt_node_covered = mountpoint;

        // Set mount reference on root node
        root->mp = mp;

        TAILQ_INSERT_TAIL(&mountlist, mp, mnt_list);
    }

    return 0;
}

size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    if (node->read != 0) {
        size_t result = node->read(node, offset, size, buffer);
        
        // Update access time
        extern int64_t get_time(void);
        node->atime = get_time();
        
        return result;
    } else
        return 0;
}

size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    if (node->write != 0) {
        size_t result = node->write(node, offset, size, buffer);
        
        // Update modification and change times
        extern int64_t get_time(void);
        int64_t now = get_time();
        node->mtime = now;
        node->ctime = now;
        
        return result;
    } else
        return 0;
}

void open_fs(fs_node_t *node, uint8_t read, uint8_t write) {
    (void)read; (void)write;
    if (node->open != 0)
        node->open(node);
}

void close_fs(fs_node_t *node) {
    if (node->close != 0)
        node->close(node);
}

struct dirent *readdir_fs(fs_node_t *node, uint64_t index) {
    if ((node->flags & 0x7) == FS_DIRECTORY && node->readdir != 0)
        return node->readdir(node, index);
    else
        return 0;
}



// Internal with follow control
static fs_node_t *finddir_fs_internal(fs_node_t *node, char *name, int depth, int follow_symlinks);

// Check if a filesystem is busy
static int vfs_is_busy(struct mount *mp) {
    if (!mp) return 0;

    // Check all processes
    for (int i = 0; i < 16; i++) {
        process_t *p = &processes[i];
        if (p->pid == -1) continue;

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

// Maximum symlink recursion depth to prevent infinite loops
#define MAX_SYMLINK_DEPTH 8

static fs_node_t *finddir_fs_internal(fs_node_t *node, char *name, int depth, int follow_symlinks) {
    if (!node) return 0; // Safety
    
    // If this is a mountpoint, cross into the mounted filesystem
    if ((node->flags & FS_MOUNTPOINT) && node->ptr) {
        node = node->ptr;
    }

    if ((node->flags & 0x7) == FS_DIRECTORY && node->finddir != 0) {
        fs_node_t *result = node->finddir(node, name);
        
        if (result && !result->mp) {
            result->mp = node->mp;
        }

        // Resolve symlinks (with depth limit)
        if (result && (result->flags & 0x7) == FS_SYMLINK && result->readlink) {
            // Check if we should follow symlinks
            if (!follow_symlinks) return result;

            // Check recursion depth limit
            if (depth >= MAX_SYMLINK_DEPTH) {
                // Too many symlink levels - return the symlink node itself (ELOOP)
                return result;
            }
            
            static char link_target[256];
            int len = result->readlink(result, link_target, sizeof(link_target));
            if (len > 0) {
                link_target[len] = '\0';
                
                // Resolve the target path
                fs_node_t *target;
                if (link_target[0] == '/') {
                    // Absolute symlink - start from root
                    target = vfs_lookup(fs_root, link_target);
                } else {
                    // Relative symlink - recurse
                    target = finddir_fs_internal(node, link_target, depth + 1, 1); // Follow nested
                }
                
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
    if (path[0] == '/') path++; // Skip leading /
    if (path[0] == '\0') return root; // Root itself
    
    fs_node_t *current = root;
    static char component[256];
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
        
        // Lookup this component
        current = finddir_fs(current, component);
        if (!current) return NULL;
        
        // Skip trailing slash
        if (*p == '/') p++;
    }
    
    return current;
}

fs_node_t *vfs_lookup_lstat(fs_node_t *root, const char *path) {
    if (!path || !root) return NULL;
    if (path[0] == '/') path++; // Skip leading /
    if (path[0] == '\0') return root; // Root itself
    
    fs_node_t *current = root;
    static char component[256];
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
        
        // Check if this is the last component
        int is_last = (*p == '\0');
        
        // Lookup this component
        // If last, DO NOT follow symlinks
        current = finddir_fs_internal(current, component, 0, !is_last);
        if (!current) return NULL;
        
        // Skip trailing slash
        if (*p == '/') p++;
    }
    
    return current;
}


int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode) {
    if (uid == 0) return 0; // Root always has access

    uint32_t mask = 0;
    if (uid == node->uid) {
        if (mode & 4) mask |= 0400;
        if (mode & 2) mask |= 0200;
        if (mode & 1) mask |= 0100;
    } else if (gid == node->gid) {
        if (mode & 4) mask |= 0040;
        if (mode & 2) mask |= 0020;
        if (mode & 1) mask |= 0010;
    } else {
        if (mode & 4) mask |= 0004;
        if (mode & 2) mask |= 0002;
        if (mode & 1) mask |= 0001;
    }

    return (node->mask & mask) == mask ? 0 : -1;
}


int readlink_fs(fs_node_t *node, char *buf, size_t size) {
    if (node && node->readlink) {
        return node->readlink(node, buf, size);
    }
    return -1;
}

int symlink_fs(fs_node_t *parent, const char *target, const char *name) {
    if (parent && parent->symlink) {
        return parent->symlink(parent, target, name);
    }
    return -1;
}

int link_fs(fs_node_t *parent, fs_node_t *source, const char *name) {
    if (parent && parent->link) {
        return parent->link(parent, source, name);
    }
    return -1;
}

int unlink_fs(fs_node_t *node, const char *name) {
    if (node && node->unlink) {
        return node->unlink(node, name);
    }
    return -1;
}

void *mmap_fs(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset) {
    if (node && node->mmap) {
        return node->mmap(node, addr, length, prot, flags, offset);
    }
    return (void *)-1;
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
    struct nameidata nd;
    struct vattr va;
    struct vnode *vp;
    int error;

    if (!path) return -1;

    /* Initialize attributes for the new directory */
    VATTR_NULL(&va);
    va.va_type = VDIR;
    va.va_mode = permission & 0777;

    /* 
     * Lookup for creation.
     * We need the parent directory (nd.ni_dvp) and information about the 
     * component to be created (nd.ni_cnd).
     */
    NDINIT(&nd, CREATE, LOCKPARENT, UIO_SYSSPACE, path);
    error = namei(&nd);
    if (error)
        return error;

    /* Check if the entry already exists */
    if (nd.ni_vp != NULL) {
        vput(nd.ni_dvp);
        vrele(nd.ni_vp);
        return -17; /* EEXIST */
    }

    /* Perform the directory creation */
    error = VOP_MKDIR(nd.ni_dvp, &vp, &nd.ni_cnd, &va);
    
    /* VOP_MKDIR is expected to release the lock on parent if needed, 
     * but following BSD pattern, we usually vput the parent.
     */
    vput(nd.ni_dvp);
    
    if (error == 0) {
        /* Successfully created, we can release the new vnode */
        vput(vp);
    }

    return error;
}

int vfs_mknod(const char *path, uint16_t mode, uint32_t dev) {
    if (!path) return -1;
    
    char path_buf[256];
    strncpy(path_buf, path, sizeof(path_buf));
    path_buf[255] = '\0';
    
    // Split path
    char *last_slash = vfs_strrchr(path_buf, '/');
    char *name = NULL;
    fs_node_t *parent_node = NULL;
    
    if (last_slash) {
        *last_slash = '\0';
        name = last_slash + 1;
        if (*name == '\0') return -1; // Trailing slash invalid for node creation
        
        if (path_buf[0] == '\0') {
            parent_node = fs_root;
        } else {
            parent_node = vfs_lookup(fs_root, path_buf);
        }
    } else {
        parent_node = fs_root;
        name = path_buf;
    }
    
    if (!parent_node) return -1;
    if ((parent_node->flags & 0x7) != FS_DIRECTORY) return -1;
    
    if (!parent_node->mknod) return -1;
    
    return parent_node->mknod(parent_node, name, mode, dev);
}

int vfs_unmount_legacy(const char *path) {
    if (!path) return -1;
    
    // Lookup mount point (directory that was mounted ON)
    // We need to find the node that has FS_MOUNTPOINT flag.
    // If we use regular lookup, we might traverse into the mounted filesystem.
    // But we want the COVERED node.
    
    // NOTE: vfs_lookup usually traverses mountpoints.
    // We need a way to lookup without traversing the LAST mountpoint.
    
    // For now, let's assume we can match by path string if we had a mount list?
    // But we didn't implement a global mount list with paths yet (except implicit tree).
    
    // Workaround: Use vfs_lookup_lstat? No, that stops at symlinks.
    // We need to implement a lookup that returns the MOUNTPOINT node, not the root of the fs.
    
    // Actually, if we mount on /mnt, the node at /mnt (in root fs) has FS_MOUNTPOINT.
    // Its 'ptr' points to the new root.
    // When we lookup "/mnt", check if traversal logic handles it.
    
    // In finddir_fs_internal:
    // if ((node->flags & FS_MOUNTPOINT) && node->ptr) node = node->ptr;
    
    // So if we lookup "/mnt", we get the ROOT of the new fs.
    // We need the node BEFORE the jump.
    
    // Strategy: Lookup parent directory, then find entry, but manually check flags
    // without invoking the automatic traversal (or utilize a specialized finding function).
    
    char path_buf[256];
    strncpy(path_buf, path, sizeof(path_buf));
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
    TAILQ_FOREACH(mp_iter, &mountlist, mnt_list) {
        if (mp_iter->mnt_node_covered == mountpoint && mp_iter->mnt_node_root == root) {
            target_mp = mp_iter;
            break;
        }
    }

    if (target_mp) {
        if (vfs_is_busy(target_mp)) {
            return -16; // EBUSY
        }
    }

    // Detach
    mountpoint->ptr = NULL;
    mountpoint->flags &= ~FS_MOUNTPOINT;
    
    // Cleanup fs instance
    if (root && root->unmount) {
        root->unmount(root);
    }
    
    // Remove from mount list and free
    if (target_mp) {
        TAILQ_REMOVE(&mountlist, target_mp, mnt_list);
        kfree(target_mp, sizeof(struct mount));
    }

    return 0;
}
