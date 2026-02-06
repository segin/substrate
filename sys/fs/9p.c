#include <vfs/vfs.h>
#include <sys/9p.h>
#include <string.h>
#include <stddef.h>
#include <vm/vm_kmem.h>

static uint32_t p9_vfs_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    // 1. Create TREAD message
    // 2. Send over transport (VirtIO/TCP)
    // 3. Parse RREAD response
    
    uint32_t msize = 4 + 1 + 2 + 4 + 8 + 4;
    uint8_t msg[msize];
    (void)node;
    
    // Helper pointers
    uint8_t *p = msg;
    
    // Size (inclusive)
    *(uint32_t*)p = msize; p += 4;
    // Type
    *p = P9_TREAD; p += 1;
    // Tag (0 for now)
    *(uint16_t*)p = 0; p += 2;
    // FID (Root FID 0 presumed attached)
    *(uint32_t*)p = 0; p += 4; // TODO: Real FID management
    // Offset
    *(uint64_t*)p = offset; p += 8;
    // Count
    *(uint32_t*)p = size; p += 4;
    
    // Response Buffer
    // RREAD: size[4] RREAD[1] tag[2] count[4] data[count]
    // We need enough space for header + data
    uint32_t header_size = 4 + 1 + 2 + 4;

    if (size > UINT32_MAX - header_size) {
        return 0;
    }

    uint32_t rsize_max = header_size + size;
    uint8_t *rmsg = kmalloc(rsize_max);
    if (!rmsg) {
        return 0;
    }

    uint32_t ret_count = 0;
    
    extern int virtio_9p_send(void *out_buf, uint32_t out_len, void *in_buf, uint32_t in_len);
    if (virtio_9p_send(msg, msize, rmsg, rsize_max) != 0) {
        goto cleanup;
    }
    
    // Parse Response
    p = rmsg;
    // uint32_t r_len = *(uint32_t*)p; 
    p += 4;
    uint8_t r_type = *p; p += 1;
    
    if (r_type == P9_RERROR) {
        goto cleanup;
    }
    
    if (r_type != P9_RREAD) {
        goto cleanup;
    }
    
    p += 2; // Skip Tag
    uint32_t count = *(uint32_t*)p; p += 4;
    
    if (count > size) count = size; // Should not happen
    
    memcpy(buffer, p, count);
    ret_count = count;

cleanup:
    kfree(rmsg, rsize_max);
    return ret_count;
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
