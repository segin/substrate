#include <stddef.h>
#include <string.h>

#include <drivers/virtio/virtio.h>
#include <sys/9p.h>
#include <sys/errno.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

/* Rread header: size[4] type[1] tag[2] count[4] */
#define P9_RREAD_HDR_LEN 11

/*
 * [9P-25] Every request used to carry tag 0.  A tag is what lets a client
 * match a reply to the request it answers; with a single constant there is
 * nothing to check, so a reply arriving out of order -- or belonging to
 * another in-flight request -- was accepted as this one's.  The transport is
 * now serialised (virtio_9p.c), which removes the concurrency, but the tag is
 * still what proves the server answered THIS message.  P9_NOTAG is reserved
 * for Tversion, so allocation skips it.
 */
struct p9_fs {
    uint32_t next_fid;
    uint32_t root_fid;
    uint32_t msize;
    uint16_t next_tag;
};

static uint16_t p9_alloc_tag(struct p9_fs *fs) {
    uint16_t tag = fs->next_tag++;
    if (fs->next_tag == P9_NOTAG)
        fs->next_tag = 0;
    return tag;
}

/*
 * [9P-25] Fids were handed out by a bare post-increment that never recycled
 * and never checked for exhaustion, so a long-lived mount eventually wrapped
 * through P9_NOFID (0xFFFFFFFF) -- the reserved "no fid" value -- and then
 * kept going into fids the server already had open under different names.
 *
 * There is no fid table to recycle from yet (the client opens exactly one,
 * the root, and nothing else can allocate a node), so the honest fix is to
 * refuse rather than wrap.  When a walk/finddir path is added, this is the
 * single place a real free list has to go.
 */
static uint32_t p9_alloc_fid(struct p9_fs *fs) {
    if (fs->next_fid >= P9_NOFID)
        return P9_NOFID;
    return fs->next_fid++;
}

/*
 * Common reply validation: enough bytes for size[4] type[1] tag[2], the type
 * the caller expected, and the tag it sent.  Returns 1 when the reply can be
 * parsed further.
 */
static int p9_reply_ok(const uint8_t *rmsg, int rlen, uint8_t want_type,
                       uint16_t want_tag) {
    uint16_t got_tag;

    if (rlen < 7)
        return 0;
    if (rmsg[4] != want_type)
        return 0;
    memcpy(&got_tag, rmsg + 5, sizeof(got_tag));
    return got_tag == want_tag;
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

    /* [9P-09] virtio_9p_send now returns how many bytes the server actually
     * wrote; a reply too short to contain size[4] type[1] must not be
     * dereferenced. */
    int success = 0;
    int rlen = virtio_9p_send(msg, msize, rmsg, rsize_max);
    /* Tversion is the one message that legitimately uses NOTAG, and the
     * server must echo it. */
    if (p9_reply_ok(rmsg, rlen, P9_RVERSION, P9_NOTAG)) {
        success = 1;
        // Ideally we should check the negotiated version and msize
    }

    kfree(msg, msize);
    kfree(rmsg, rsize_max);
    return success;
}

static uint32_t p9_attach(struct p9_fs *fs) {
    // Tattach: size[4] Tattach[1] tag[2] fid[4] afid[4] uname[s] aname[s]
    uint32_t fid = p9_alloc_fid(fs);
    uint16_t tag;
    const char *uname = "root";

    if (fid == P9_NOFID)
        return P9_NOFID;            /* [9P-25] fid space exhausted */

    const char *aname = ""; // Empty for root
    uint16_t uname_len = strlen(uname);
    uint16_t aname_len = strlen(aname);

    uint32_t msize = 4 + 1 + 2 + 4 + 4 + 2 + uname_len + 2 + aname_len;
    uint8_t *msg = kmalloc(msize);
    if (!msg) return P9_NOFID;

    uint8_t *p = msg;
    *(uint32_t*)p = msize; p += 4;
    *p = P9_TATTACH; p += 1;
    tag = p9_alloc_tag(fs);
    *(uint16_t*)p = tag; p += 2;
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

    /* [9P-09] As above: bound the parse by what actually arrived. */
    uint32_t result_fid = P9_NOFID;
    int rlen = virtio_9p_send(msg, msize, rmsg, rsize_max);
    if (p9_reply_ok(rmsg, rlen, P9_RATTACH, tag)) {
        result_fid = fid;
    }

    kfree(msg, msize);
    kfree(rmsg, rsize_max);
    return result_fid;
}

/*
 * [9P-26] This was declared as
 *     static uint32_t p9_vfs_read(fs_node_t *, off_t, uint32_t, uint8_t *)
 * and assigned to fs_node_t::read, which is
 *     size_t (*)(struct fs_node *, off_t, size_t, uint8_t *).
 * That is a call through an incompatible function-pointer type; it happens to
 * work only because size_t is uint32_t on i386, and becomes an ABI mismatch
 * (64-bit size argument read out of a 32-bit slot) the moment this is built
 * for x86_64.  Match the real prototype.
 */
static size_t p9_vfs_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    // 1. Create TREAD message
    // 2. Send over transport (VirtIO/TCP)
    // 3. Parse RREAD response

    struct p9_fs *fs;
    uint32_t fid;
    uint16_t tag;
    uint32_t req_size;

    uint32_t msize = 4 + 1 + 2 + 4 + 8 + 4;
    uint8_t msg[msize];

    // Helper pointers
    uint8_t *p = msg;

    if (!node || !buffer)
        return 0;

    fs = (struct p9_fs *)node->impl;
    if (!fs)
        return 0;

    /*
     * [9P-25] The fid is what names the file on the server, and this sent
     * fs->root_fid for EVERY node -- so the moment a second node exists,
     * reading any file returns the contents of the mount root instead.  The
     * node carries its own fid; the root node is created with root_fid, and
     * any node a future walk/finddir creates must set its own.  A node with
     * no fid is not readable rather than silently the root.
     */
    fid = (uint32_t)node->inode;
    if (fid == P9_NOFID)
        return 0;

    /* Tread's count field is 32-bit, and the reply must fit the negotiated
     * msize; clamp instead of truncating silently. */
    if (size > UINT32_MAX)
        size = UINT32_MAX;
    req_size = (uint32_t)size;
    if (fs->msize > P9_RREAD_HDR_LEN &&
        req_size > fs->msize - P9_RREAD_HDR_LEN) {
        req_size = fs->msize - P9_RREAD_HDR_LEN;
    }
    if (req_size == 0)
        return 0;

    // Size (inclusive)
    *(uint32_t*)p = msize; p += 4;
    // Type
    *p = P9_TREAD; p += 1;
    // Tag
    tag = p9_alloc_tag(fs);
    *(uint16_t*)p = tag; p += 2;
    // FID
    *(uint32_t*)p = fid; p += 4;
    // Offset
    *(uint64_t*)p = offset; p += 8;
    // Count
    *(uint32_t*)p = req_size; p += 4;
    
    // Response Buffer
    // RREAD: size[4] RREAD[1] tag[2] count[4] data[count]
    // We need enough space for header + data
    uint32_t header_size = 4 + 1 + 2 + 4;

    if (req_size > UINT32_MAX - header_size) {
        return 0;
    }

    uint32_t rsize_max = header_size + req_size;
    uint8_t *rmsg = kmalloc(rsize_max);
    if (!rmsg) {
        return 0;
    }

    uint32_t ret_count = 0;
    
    /*
     * [9P-09] This is the one that mattered.  virtio_9p_send returned only
     * 0/-1, the reply's own size[4] field was read and then explicitly
     * discarded (the `r_len` line was commented out), and `count` was taken
     * from the reply body and clamped ONLY against the caller's requested
     * size.  A server answering a 4096-byte Tread with an 11-byte Rread that
     * claims count=4096 therefore had 4085 bytes of freshly-kmalloc'd,
     * never-initialised kernel heap memcpy'd out to the caller.  Wire data
     * from the server was trusted completely.
     *
     * Now the transport reports how many bytes actually arrived, and the
     * payload must fit inside them: the header is size[4] type[1] tag[2]
     * count[4] = 11 bytes, so at most rlen - 11 bytes of data can be real.
     */
    int rlen = virtio_9p_send(msg, msize, rmsg, rsize_max);
    if (rlen < P9_RREAD_HDR_LEN) {
        goto cleanup;
    }

    /*
     * [9P-25] Parse only after the reply is confirmed to be an Rread carrying
     * OUR tag.  The type alone was checked before, so a reply belonging to a
     * different request -- or a stale one still in the ring -- was copied out
     * as if it answered this read.
     */
    if (!p9_reply_ok(rmsg, rlen, P9_RREAD, tag)) {
        goto cleanup;
    }

    p = rmsg + P9_RREAD_HDR_LEN - 4;    /* at count[4] */
    uint32_t count = *(uint32_t*)p; p += 4;

    uint32_t avail = (uint32_t)rlen - P9_RREAD_HDR_LEN;
    if (count > avail)    count = avail;      /* server over-claimed */
    if (count > req_size) count = req_size;   /* never exceed what was asked */

    memcpy(buffer, p, count);
    ret_count = count;

cleanup:
    kfree(rmsg, rsize_max);
    return ret_count;
}

static int p9_unmount(fs_node_t *root) {
    if (!root) return -EINVAL;
    struct p9_fs *fs = (struct p9_fs *)root->impl;
    if (!fs) return -EINVAL;

    // Tclunk: size[4] Tclunk[1] tag[2] fid[4]
    uint32_t msize = 4 + 1 + 2 + 4;
    uint8_t msg[11];
    uint8_t *p = msg;
    uint16_t tag = p9_alloc_tag(fs);
    *(uint32_t*)p = msize; p += 4;
    *p = P9_TCLUNK; p += 1;
    *(uint16_t*)p = tag; p += 2;
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
    fs->next_tag = 0;

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
    /* [9P-25] The node's own fid.  p9_vfs_read reads it from here instead of
     * unconditionally using fs->root_fid, so a node added by a future
     * walk/finddir cannot silently read the mount root. */
    p9_root->inode = root_fid;

    return p9_root;
}

static filesystem_t p9_fs = {
    .name = "9p",
    .mount = &p9_mount,
};

void p9_init(void) {
    vfs_register_filesystem(&p9_fs);
}
