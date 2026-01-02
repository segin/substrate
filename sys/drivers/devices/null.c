#include "../../vfs/vfs.h"
#include <string.h>

// /dev/null
static uint32_t null_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0; // EOF
}

static uint32_t null_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size; // Discarded
}

// /dev/zero
static uint32_t zero_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    memset(buffer, 0, size);
    return size;
}

static fs_node_t null_node;
static fs_node_t zero_node;

void null_init(void) {
    memset(&null_node, 0, sizeof(fs_node_t));
    strcpy(null_node.name, "null");
    null_node.flags = FS_CHARDEVICE;
    null_node.read = &null_read;
    null_node.write = &null_write;
    devfs_register_device(&null_node);

    memset(&zero_node, 0, sizeof(fs_node_t));
    strcpy(zero_node.name, "zero");
    zero_node.flags = FS_CHARDEVICE;
    zero_node.read = &zero_read;
    zero_node.write = &null_write; // Reuse discard logic
    devfs_register_device(&zero_node);
}
