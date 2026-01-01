#ifndef _FILE_H
#define _FILE_H

#include <stdint.h>
#include "../vfs/vfs.h"

typedef struct file {
    fs_node_t *node;
    uint32_t offset;
    int flags;
    int ref_count;
} file_t;

#endif
