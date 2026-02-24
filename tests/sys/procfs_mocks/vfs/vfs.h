#ifndef _VFS_H
#define _VFS_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

struct dirent;
typedef struct fs_node {
    char name[128];
    uint32_t flags;
    uint32_t inode;
    uintptr_t impl; /* Changed from void* to match real kernel */
    struct fs_node *parent;
    size_t (*read)(struct fs_node *, off_t, size_t, uint8_t *);
    struct dirent *(*readdir)(struct fs_node *, uint64_t);
    struct fs_node *(*finddir)(struct fs_node *, char *);
    void (*close)(struct fs_node *);
    void (*open)(struct fs_node *);
    void (*write)(struct fs_node *, off_t, size_t, uint8_t *);
} fs_node_t;

typedef struct filesystem {
    const char *name;
    struct filesystem *next;
    fs_node_t *(*mount)(const char *, uint32_t, void *);
} filesystem_t;

void vfs_register_filesystem(filesystem_t *fs);
filesystem_t *vfs_get_filesystems(void);

struct dirent {
    char d_name[256];
    uint32_t d_ino;
};

#define FS_FILE 1
#define FS_DIRECTORY 2
#endif
