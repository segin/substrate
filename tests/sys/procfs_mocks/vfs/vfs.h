#ifndef _VFS_H
#define _VFS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02

typedef struct fs_node {
    char name[128];
    uint32_t flags;
    uint64_t inode;
    uintptr_t impl;
    void *read;
    void *write;
    void *open;
    void *close;
    void *readdir;
    void *finddir;
} fs_node_t;

struct dirent {
    char d_name[256];
    uint64_t d_ino;
};

typedef struct fs_node * (*mount_type_t)(const char *device, uint32_t flags, void *data);

typedef struct filesystem {
    char name[32];
    mount_type_t mount;
    struct filesystem *next;
} filesystem_t;

void vfs_register_filesystem(filesystem_t *fs);
filesystem_t *vfs_get_filesystems(void);

#endif
