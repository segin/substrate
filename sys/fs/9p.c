#include <vfs/vfs.h>
#include <sys/9p.h>
#include <string.h>
#include <stddef.h>
#include <vm/vm_kmem.h>
#include <vfs/vfs.h>
#include <drivers/virtio/virtio.h>
#include <sys/errno.h>

struct p9_fs {
    uint32_t next_fid;
    uint32_t root_fid;
    uint32_t msize;
};

static uint32_t p9_alloc_fid(struct p9_fs *fs) {
    return fs->next_fid++;
}

static int p9_version(struct p9_fs *fs) {
    // Tversion: size[4] Tversion[1] tag[2] msize[4] version[s]
    // version = "9P2000.L" or "9P2000"
    const char *version_str = "9P2000.L";
    uint16_t version_len = strlen(version_str);
    uint32_t msize = 4 + 1 + 2 + 4 + 2 + version_len;

    uint8_t *msg = kmalloc(msize);
    if (!msg) return 0;

    uint8_t *p = msg;
    *(uint32_t*)p = msize; p += 4;
    *p = P9_TVERSION; p += 1;
    *(uint16_t*)p = P9_NOTAG; p += 2; // Version uses NOTAG
    *(uint32_t*)p = fs->msize; p += 4; // Proposed msize
    *(uint16_t*)p = version_len; p += 2;
    memcpy(p, version_str, version_len);

    // Rversion: size[4] Rversion[1] tag[2] msize[4] version[s]
    // We allocate enough for a reasonable response
    uint32_t rsize_max = 512;
    uint8_t *rmsg = kmalloc(rsize_max);
    if (!rmsg) {
        kfree(msg, msize);
        return 0;
    }

    int success = 0;
    if (virtio_9p_send(msg, msize, rmsg, rsize_max) == 0) {
        p = rmsg + 4;
        if (*p == P9_RVERSION) {
            success = 1;
            // Ideally we should check the negotiated version and msize
        }
    }

    kfree(msg, msize);
    kfree(rmsg, rsize_max);
    return success;
}

static uint32_t p9_attach(struct p9_fs *fs) {
    // Tattach: size[4] Tattach[1] tag[2] fid[4] afid[4] uname[s] aname[s]
    uint32_t fid = p9_alloc_fid(fs);
    const char *uname = "root";
    const char *aname = ""; // Empty for root
    uint16_t uname_len = strlen(uname);
    uint16_t aname_len = strlen(aname);

    uint32_t msize = 4 + 1 + 2 + 4 + 4 + 2 + uname_len + 2 + aname_len;
    uint8_t *msg = kmalloc(msize);
    if (!msg) return P9_NOFID;

    uint8_t *p = msg;
    *(uint32_t*)p = msize; p += 4;
    *p = P9_TATTACH; p += 1;
    *(uint16_t*)p = 0; p += 2; // Tag 0
    *(uint32_t*)p = fid; p += 4;
    *(uint32_t*)p = P9_NOFID; p += 4; // No authentication
    *(uint16_t*)p = uname_len; p += 2;
    memcpy(p, uname, uname_len); p += uname_len;
    *(uint16_t*)p = aname_len; p += 2;
    memcpy(p, aname, aname_len);

    // Rattach: size[4] Rattach[1] tag[2] qid[13]
    uint32_t rsize_max = 4 + 1 + 2 + 13;
    uint8_t *rmsg = kmalloc(rsize_max);
    if (!rmsg) {
        kfree(msg, msize);
        // Should probably free fid here but we don't have free_fid yet
        return P9_NOFID;
    }

    uint32_t result_fid = P9_NOFID;
    if (virtio_9p_send(msg, msize, rmsg, rsize_max) == 0) {
        p = rmsg + 4;
        if (*p == P9_RATTACH) {
            result_fid = fid;
        }
    }

    kfree(msg, msize);
    kfree(rmsg, rsize_max);
    return result_fid;
}

static uint32_t p9_vfs_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    // 1. Create TREAD message
    // 2. Send over transport (VirtIO/TCP)
    // 3. Parse RREAD response
    
    uint32_t msize = 4 + 1 + 2 + 4 + 8 + 4;
    uint8_t msg[msize];
    
    // Helper pointers
    uint8_t *p = msg;
    
    // Size (inclusive)
    *(uint32_t*)p = msize; p += 4;
    // Type
    *p = P9_TREAD; p += 1;
    // Tag (0 for now)
    *(uint16_t*)p = 0; p += 2;
    // FID
    struct p9_fs *fs = (struct p9_fs *)node->impl;
    *(uint32_t*)p = fs->root_fid; p += 4;
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

static int p9_unmount(fs_node_t *root) {
    if (!root) return -1;
    struct p9_fs *fs = (struct p9_fs *)root->impl;
    if (!fs) return -1;

    // Tclunk: size[4] Tclunk[1] tag[2] fid[4]
    uint32_t msize = 4 + 1 + 2 + 4;
    uint8_t msg[11];
    uint8_t *p = msg;
    *(uint32_t*)p = msize; p += 4;
    *p = P9_TCLUNK; p += 1;
    *(uint16_t*)p = 0; p += 2;
    *(uint32_t*)p = fs->root_fid;
    
    uint8_t rmsg[7]; // Rclunk: size[4] RCLUNK[1] tag[2]
    virtio_9p_send(msg, msize, rmsg, 7);

    kfree(fs, sizeof(struct p9_fs));
    return 0;
}

static fs_node_t *p9_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    struct p9_fs *fs = kmalloc(sizeof(struct p9_fs));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(struct p9_fs));
    fs->msize = 8192;
    fs->next_fid = 1;

    fs_node_t *p9_root = kmalloc(sizeof(fs_node_t));
    if (!p9_root) {
        kfree(fs, sizeof(struct p9_fs));
        return NULL;
    }
    memset(p9_root, 0, sizeof(fs_node_t));

    // Perform handshake
    if (!p9_version(fs)) {
        kfree(p9_root, sizeof(fs_node_t));
        kfree(fs, sizeof(struct p9_fs));
        return NULL;
    }

    uint32_t root_fid = p9_attach(fs);
    if (root_fid == P9_NOFID) {
        kfree(p9_root, sizeof(fs_node_t));
        kfree(fs, sizeof(struct p9_fs));
        return NULL;
    }

    fs->root_fid = root_fid;

    p9_root->flags = FS_DIRECTORY;
    p9_root->read = &p9_vfs_read;
    p9_root->unmount = &p9_unmount;
    p9_root->impl = (uintptr_t)fs;

    return p9_root;
}

static filesystem_t p9_fs = {
    .name = "9p",
    .mount = &p9_mount,
};

void p9_init(void) {
    vfs_register_filesystem(&p9_fs);
}
