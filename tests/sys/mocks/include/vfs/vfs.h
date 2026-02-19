#ifndef _VFS_MOCK_H
#define _VFS_MOCK_H
#include <stdint.h>
#include <sys/types.h>

typedef struct fs_node {
    size_t (*read)(struct fs_node *node, off_t offset, size_t size, uint8_t *buffer);
    size_t (*write)(struct fs_node *node, off_t offset, size_t size, const uint8_t *buffer);
    void *internal_data;
    uint64_t size;
} fs_node_t;

#endif
