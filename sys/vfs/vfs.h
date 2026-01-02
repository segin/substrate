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

typedef uint32_t (*read_type_t)(struct fs_node*, uint32_t, uint32_t, uint8_t*);
typedef uint32_t (*write_type_t)(struct fs_node*, uint32_t, uint32_t, uint8_t*);
typedef void (*open_type_t)(struct fs_node*);
typedef void (*close_type_t)(struct fs_node*);
typedef struct dirent * (*readdir_type_t)(struct fs_node*, uint32_t);
typedef struct fs_node * (*finddir_type_t)(struct fs_node*, char *name);
typedef int (*ioctl_type_t)(struct fs_node*, uint32_t, void*);

// Symlink Operations
typedef int (*readlink_type_t)(struct fs_node*, char *buf, size_t size);
typedef int (*symlink_type_t)(struct fs_node*, const char *target, const char *name);

typedef struct fs_node {
    char name[128];
    uint32_t mask;        // The permissions mask.
    uint32_t uid;         // The owning user.
    uint32_t gid;         // The owning group.
    uint32_t flags;       // Includes the node type.
    uint32_t inode;       // This is device-specific - provides a way for a filesystem to identify files.
    uint32_t length;      // Size of the file, in bytes.
    uint32_t impl;        // An implementation-defined number.
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    readdir_type_t readdir;
    finddir_type_t finddir;
    ioctl_type_t ioctl;
    readlink_type_t readlink;
    symlink_type_t symlink;
    struct fs_node *ptr; // Used by mountpoints and symlinks.
} fs_node_t;

struct dirent {
    char name[128];
    uint32_t ino;
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

// Standard VFS functions
uint32_t read_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t write_fs(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void open_fs(fs_node_t *node, uint8_t read, uint8_t write);
void close_fs(fs_node_t *node);
struct dirent *readdir_fs(fs_node_t *node, uint32_t index);
fs_node_t *finddir_fs(fs_node_t *node, char *name);
int ioctl_fs(fs_node_t *node, uint32_t request, void *arg);
int readlink_fs(fs_node_t *node, char *buf, size_t size);
int symlink_fs(fs_node_t *parent, const char *target, const char *name);

int vfs_check_permissions(fs_node_t *node, uint32_t uid, uint32_t gid, int mode);

void vfs_register_filesystem(filesystem_t *fs);
int vfs_mount(const char *device, const char *path, const char *type, uint32_t flags, void *data);
fs_node_t *vfs_lookup(fs_node_t *root, const char *path);
void vfs_init(void);

void devfs_init(void);
void devfs_register_device(fs_node_t *node);

extern fs_node_t *fs_root; // Global root node

#endif
