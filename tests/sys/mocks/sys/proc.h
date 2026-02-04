#ifndef _SYS_PROC_H
#define _SYS_PROC_H

#include <vfs/vfs.h>

typedef struct process {
    fs_node_t *cwd_node;
    fs_node_t *root_node;
} process_t;

extern process_t *current_process;

#endif
