#ifndef _VFS_VFS_H
#define _VFS_VFS_H

#include <stdint.h>
#include <errno.h>

#define FS_CHARDEVICE 0x2000

typedef struct fs_node {
    char name[32];
    int flags;
    uintptr_t impl;
    int (*ioctl)(struct fs_node *node, uint32_t request, void *arg);
    void (*close)(struct fs_node *node);
    void (*open)(struct fs_node *node);
} fs_node_t;

void devfs_register_device(fs_node_t *node);

#endif
