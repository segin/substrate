#include <vfs/vfs.h>
#include <sys/fuse.h>
#include <kern/sched.h>
#include <string.h>
#include <stddef.h>

#define FUSE_QUEUE_SIZE 16
static struct fuse_in_header request_queue[FUSE_QUEUE_SIZE];
static int fuse_q_head = 0;
static int fuse_q_tail = 0;

static uint32_t fuse_dev_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    if (size < sizeof(struct fuse_in_header)) return 0;

    // Block until a request is available
    while (fuse_q_head == fuse_q_tail) {
        sched_sleep(&request_queue);
    }

    struct fuse_in_header *req = &request_queue[fuse_q_tail];
    memcpy(buffer, req, sizeof(struct fuse_in_header));
    fuse_q_tail = (fuse_q_tail + 1) % FUSE_QUEUE_SIZE;

    return sizeof(struct fuse_in_header);
}

static uint32_t fuse_dev_write(fs_node_t *node, off_t offset, uint32_t size, const uint8_t *buffer) {
    (void)node; (void)offset;
    // Process response from userspace
    struct fuse_out_header *out = (struct fuse_out_header *)buffer;
    (void)out;
    // In a real system, we'd find the original request and wake the waiting thread.
    return size;
}

static fs_node_t fuse_device_node;

void fuse_init(void) {
    memset(&fuse_device_node, 0, sizeof(fs_node_t));
    strcpy(fuse_device_node.name, "fuse");
    fuse_device_node.flags = FS_CHARDEVICE;
    fuse_device_node.read = &fuse_dev_read;
    fuse_device_node.write = &fuse_dev_write;
    
    devfs_register_device(&fuse_device_node);
}

// VFS Bridge Implementation (Stub)
static uint32_t fuse_vfs_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    // 1. Create FUSE_READ request
    // 2. Enqueue in request_queue
    // 3. Sleep until response arrives
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

static fs_node_t *fuse_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    static fs_node_t fuse_root;
    memset(&fuse_root, 0, sizeof(fs_node_t));
    fuse_root.flags = FS_DIRECTORY;
    fuse_root.read = &fuse_vfs_read;
    return &fuse_root;
}

static filesystem_t fuse_fs = {
    .name = "fuse",
    .mount = &fuse_mount,
};

void fuse_fs_init(void) {
    vfs_register_filesystem(&fuse_fs);
}
