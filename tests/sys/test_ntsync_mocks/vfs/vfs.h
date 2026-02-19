#ifndef _VFS_H
#define _VFS_H

#include <stdint.h>
#include <sys/types.h>

#define FS_CHARDEVICE  0x03

struct fs_node;

typedef int (*ioctl_type_t)(struct fs_node*, uint32_t, void*);
typedef void (*open_type_t)(struct fs_node*);
typedef void (*close_type_t)(struct fs_node*);

typedef struct fs_node {
    char name[128];
    uint32_t flags;
    uintptr_t impl;
    ioctl_type_t ioctl;
    open_type_t open;
    close_type_t close;
} fs_node_t;

void devfs_register_device(fs_node_t *node);

#endif
