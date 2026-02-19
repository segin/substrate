#pragma once
#include <sys/types.h>
#include <stdint.h>

typedef struct fs_node {
    char name[32];
    int flags;
    uintptr_t impl;
    void *read;
    void *write;
    void *ioctl;
    void *open;
    void *close;
} fs_node_t;
#define FS_CHARDEVICE 0x02
void devfs_register_device(fs_node_t *node);
