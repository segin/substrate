#ifndef _KERN_FILE_H
#define _KERN_FILE_H

#include <stdint.h>

/* File flags */
#define FREAD   0x0001
#define FWRITE  0x0002

struct fs_node;

typedef struct file {
    struct fs_node *f_data;
    void *f_ops;
    int f_flag;
    int f_count;
} file_t;

file_t *file_alloc(void);
void file_close(file_t *f);

#endif
