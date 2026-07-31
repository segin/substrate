#include <stddef.h>
#include <stdio.h>
#include <sys/errno.h>
#include <string.h>

#include <kern/console.h>
#include <kern/sched.h>
#include <sys/fuse.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

#define FUSE_QUEUE_SIZE 16
static struct fuse_in_header request_queue[FUSE_QUEUE_SIZE];
static int fuse_q_head = 0;
static int fuse_q_tail = 0;

/*
 * [FUSE-10] This used to be:
 *
 *     while (fuse_q_head == fuse_q_tail)
 *         sched_sleep(&request_queue);
 *
 * fuse_q_head is written nowhere in the kernel -- it is initialised to 0 and
 * only ever compared -- and nothing calls sched_wakeup(&request_queue).  The
 * condition is therefore permanently true and the sleep is permanently
 * unsatisfiable.  Worse, sched_sleep here is not interruptible, so a process
 * that opened /dev/fuse and read from it parked a thread that even SIGKILL
 * could not recover.  /dev/fuse is registered by vfs_init(), so this was
 * reachable by any user, not dormant.
 *
 * There is no request path to wait for: fuse_vfs_read never enqueues
 * anything.  Until one exists, say so instead of hanging.
 */
static size_t fuse_dev_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    (void)request_queue; (void)fuse_q_head; (void)fuse_q_tail;
    return (size_t)-ENOSYS;
}

/*
 * [FUSE-27] This cast an unvalidated user buffer straight to a 16-byte
 * struct fuse_out_header with no length check, then returned `size` --
 * reporting success for a reply nothing reads.  Nothing dispatches replies
 * (there are no outstanding requests to match them against), so the honest
 * answer is ENOSYS; the length check stays so that a future implementation
 * cannot inherit the unchecked cast.
 */
static size_t fuse_dev_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset;

    if (!buffer || size < sizeof(struct fuse_out_header))
        return (size_t)-EINVAL;

    return (size_t)-ENOSYS;
}

static fs_node_t fuse_device_node;

void fuse_init(void) {
    memset(&fuse_device_node, 0, sizeof(fs_node_t));
    strlcpy(fuse_device_node.name, "fuse", sizeof(fuse_device_node.name));
    fuse_device_node.name[sizeof(fuse_device_node.name) - 1] = '\0';
    fuse_device_node.flags = FS_CHARDEVICE;
    fuse_device_node.read = &fuse_dev_read;
    fuse_device_node.write = &fuse_dev_write;
    
    devfs_register_device(&fuse_device_node);
}

// VFS Bridge Implementation (Stub)
static size_t fuse_vfs_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    // 1. Create FUSE_READ request
    // 2. Enqueue in request_queue
    // 3. Sleep until response arrives
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

static int fuse_unmount(fs_node_t *root) {
    if (root) {
        kfree(root, sizeof(fs_node_t));
    }
    return 0;
}

/*
 * [FUSE-28] This used to succeed unconditionally: no FUSE_INIT handshake, no
 * /dev/fuse session bound to the mount, no owning uid, and the node it
 * returned had NULL readdir and NULL finddir.  Mounting it over a real
 * directory therefore replaced that directory with an empty one nothing could
 * enumerate or look up -- `mount -t fuse none /etc` would hide /etc from the
 * whole system, and the mount reported success while doing it.
 *
 * There is no request path (see fuse_dev_read), so no mount can be serviced.
 * Refuse instead of shadowing a directory with a black hole.  Returning NULL
 * is how a mount handler reports failure to vfs_mount.
 */
static fs_node_t *fuse_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    (void)fuse_vfs_read; (void)fuse_unmount;
    kprintf("fuse: mount refused -- the FUSE request path is not implemented\n");
    return NULL;
}

static filesystem_t fuse_fs = {
    .name = "fuse",
    .mount = &fuse_mount,
};

void fuse_fs_init(void) {
    vfs_register_filesystem(&fuse_fs);
}
