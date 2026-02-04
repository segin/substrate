#ifndef _VFS_MOCK_H
#define _VFS_MOCK_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h> // Provides off_t

// Mock definitions for VFS
#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x03
#define FS_BLOCKDEVICE 0x04
#define FS_PIPE        0x05
#define FS_SYMLINK     0x06
#define FS_MOUNTPOINT  0x08

typedef struct fs_node fs_node_t;

// struct dirent conflict.
// We define it here. Ensure <dirent.h> is not included in the test file.
struct dirent {
    uint32_t ino;
    char name[256];
};

struct fs_node {
    char name[128];
    uint32_t mask;
    uint32_t uid;
    uint32_t gid;
    uint32_t flags;
    uint32_t inode;
    uint32_t length;
    uint32_t impl;
    uint32_t rdev;
    size_t (*read)(struct fs_node *, off_t, size_t, uint8_t *);
    size_t (*write)(struct fs_node *, off_t, size_t, const uint8_t *);
    void (*open)(struct fs_node *);
    void (*close)(struct fs_node *);
    struct dirent *(*readdir)(struct fs_node *, uint64_t);
    struct fs_node *(*finddir)(struct fs_node *, char *name);
    int (*readlink)(struct fs_node *, char *, size_t);
};

typedef struct filesystem {
    char name[20];
    struct fs_node *(*mount)(const char *device, uint32_t flags, void *data);
} filesystem_t;

void vfs_register_filesystem(filesystem_t *fs);

#endif
