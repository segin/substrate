#ifndef _VFS_H
#define _VFS_H

#include <stdint.h>
#include <sys/types.h>

typedef struct fs_node fs_node_t;
typedef struct filesystem filesystem_t;

struct fs_node {
    uint32_t flags;
    void *read;
    void *write;
};

struct filesystem {
    const char *name;
    void *mount;
};

#define FS_DIRECTORY 1

void vfs_register_filesystem(filesystem_t *fs);

#endif
