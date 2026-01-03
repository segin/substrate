#ifndef _FILE_H
#define _FILE_H

#include <stdint.h>
#include "../../vfs/vfs.h"

#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

typedef struct file {
    fs_node_t *node;
    off_t offset;
    int flags;
    int ref_count;
} file_t;

#endif
