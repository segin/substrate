#ifndef _VFS_H
#define _VFS_H

#include <stdint.h>
#include <stddef.h>

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x03
#define FS_BLOCKDEVICE 0x04
#define FS_PIPE        0x05
#define FS_SYMLINK     0x06
#define FS_MOUNTPOINT  0x08 

struct fs_node;

#include <sys/types.h>

/* ... */

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
typedef int (*mkdir_type_t)(struct fs_node*, const char *name, uint16_t permission);
typedef int (*mknod_type_t)(struct fs_node*, const char *name, uint16_t mode, uint32_t dev);
typedef int (*unmount_type_t)(struct fs_node*);

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
    mkdir_type_t mkdir;
    mknod_type_t mknod;
    unmount_type_t unmount;
    struct fs_node *ptr; // Used by mountpoints and symlinks.
} fs_node_t;

struct dirent {
    char name[128];
    uint64_t ino;
};

typedef struct fs_node * (*mount_type_t)(const char *device, uint32_t flags, void *data);

typedef struct filesystem {
    char name[32];
    mount_type_t mount;
    struct filesystem *next;
} filesystem_t;

typedef struct vfs_mount {
    char path[128];
    fs_node_t *root;
    struct vfs_mount *next;
} vfs_mount_t;

/* ... */

// Standard VFS functions
size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
void open_fs(fs_node_t *node, uint8_t read, uint8_t write);
void close_fs(fs_node_t *node);
struct dirent *readdir_fs(fs_node_t *node, uint64_t index);
fs_node_t *finddir_fs(fs_node_t *node, char *name);
int ioctl_fs(fs_node_t *node, uint32_t request, void *arg);
void *mmap_fs(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset);
int poll_fs(fs_node_t *node, void *waiter);
int readlink_fs(fs_node_t *node, char *buf, size_t size);
int symlink_fs(fs_node_t *parent, const char *target, const char *name);
int link_fs(fs_node_t *parent, fs_node_t *source, const char *name);
int unlink_fs(fs_node_t *node, const char *name);
int unlink_fs(fs_node_t *node, const char *name);
int vfs_mkdir(const char *path, uint16_t permission);
int vfs_mknod(const char *path, uint16_t mode, uint32_t dev);

int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode);

void vfs_register_filesystem(filesystem_t *fs);
filesystem_t *vfs_get_filesystems(void);
int vfs_mount(const char *device, const char *path, const char *type, uint32_t flags, void *data);
fs_node_t *vfs_lookup(fs_node_t *root, const char *path);
fs_node_t *vfs_lookup_lstat(fs_node_t *root, const char *path);
void vfs_init(void);

void devfs_init(void);
void devfs_register_device(fs_node_t *node);

extern fs_node_t *fs_root; // Global root node

#endif
