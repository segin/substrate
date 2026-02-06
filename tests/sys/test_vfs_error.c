#include <kern/console.h>
#include <vfs/vfs.h>
#include <sys/mount.h>
#include <string.h>
#include <vm/vm_kmem.h>

static int mock_unmount_called = 0;

static int mock_unmount_fn(fs_node_t *node) {
    (void)node;
    mock_unmount_called = 1;
    return 0;
}

static fs_node_t *mock_mount_fn(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;

    fs_node_t *node = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    memset(node, 0, sizeof(fs_node_t));
    strcpy(node->name, "mock_root");
    node->flags = FS_DIRECTORY;
    node->unmount = mock_unmount_fn;
    return node;
}

static filesystem_t mock_fs = {
    .name = "mock_fail",
    .mount = mock_mount_fn,
    .next = NULL
};

void run_vfs_error_tests(void) {
    kprint("TEST: vfs_error_tests starting...\n");

    // Register mock filesystem
    vfs_register_filesystem(&mock_fs);

    // Test 1: Mount point not found
    kprint("TEST: Mount point not found (expecting unmount)... ");
    mock_unmount_called = 0;

    // Attempt to mount to a non-existent path
    int ret = vfs_mount_legacy(NULL, "/nonexistent/path", "mock_fail", 0, NULL);

    if (ret != -1) {
        kprint("FAILED (vfs_mount_legacy succeeded unexpectedly)\n");
    } else {
        if (mock_unmount_called) {
            kprint("PASS (unmount called)\n");
        } else {
            kprint("FAIL (unmount NOT called)\n");
        }
    }

    // Test 2: Mount point not a directory
    // Create a file first
    // Assuming root fs allows file creation or we use an existing file.
    // Let's rely on creating a file if possible, or assume /dev/null exists?
    // Usually /dev/null is character device.
    // vfs.c check: if ((mountpoint->flags & 0x7) != FS_DIRECTORY)

    kprint("TEST: Mount point not a directory (expecting unmount)... ");
    mock_unmount_called = 0;

    // Find a non-directory node. /dev/null is usually present after devfs init.
    fs_node_t *node = vfs_lookup(fs_root, "/dev/null");
    if (node && (node->flags & 0x7) != FS_DIRECTORY) {
        ret = vfs_mount_legacy(NULL, "/dev/null", "mock_fail", 0, NULL);
         if (ret != -1) {
            kprint("FAILED (vfs_mount_legacy succeeded on non-dir)\n");
        } else {
            if (mock_unmount_called) {
                kprint("PASS (unmount called)\n");
            } else {
                kprint("FAIL (unmount NOT called)\n");
            }
        }
    } else {
        kprint("SKIPPED (could not find non-directory node /dev/null)\n");
    }

    kprint("TEST: vfs_error_tests finished.\n");
}
