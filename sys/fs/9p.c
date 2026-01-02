#include "../vfs/vfs.h"
#include <sys/9p.h>
#include <string.h>
#include <stddef.h>

static uint32_t p9_vfs_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    // 1. Create TREAD message
    // 2. Send over transport (VirtIO/TCP)
    // 3. Parse RREAD response
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

static fs_node_t *p9_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    static fs_node_t p9_root;
    memset(&p9_root, 0, sizeof(fs_node_t));
    p9_root.flags = FS_DIRECTORY;
    p9_root.read = &p9_vfs_read;
    return &p9_root;
}

static filesystem_t p9_fs = {
    .name = "9p",
    .mount = &p9_mount,
};

void p9_init(void) {
    vfs_register_filesystem(&p9_fs);
}
