#ifndef _VFS_VFS_H
#define _VFS_VFS_H
#include <stddef.h>
#include <stdint.h>
typedef struct fs_node fs_node_t;
typedef long off_t;
#define FS_CHARDEVICE 0x0002
struct fs_node {
    char name[32];
    int flags;
    size_t (*read)(struct fs_node *, off_t, size_t, uint8_t *);
    size_t (*write)(struct fs_node *, off_t, size_t, const uint8_t *);
    int rdev;
};
#endif
