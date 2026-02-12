#include <vfs/vfs.h>
#include <kern/console.h>
#include <sys/errno.h>
#include <string.h>
#include <kern/panic.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <vm/vm_kmem.h>

extern int sys_open(const char *path, int flags, int mode);
extern int sys_close(int fd);
extern int vfs_mkdir(const char *path, uint16_t permission);
extern int vfs_mount_legacy(const char *device, const char *path, const char *type, uint32_t flags, void *data);
extern int vfs_unmount_legacy(const char *path);
extern void vfs_register_filesystem(filesystem_t *fs);

static fs_node_t *dummy_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    fs_node_t *root = kmalloc(sizeof(fs_node_t));
    if (!root) return NULL;
    memset(root, 0, sizeof(fs_node_t));
    root->flags = FS_DIRECTORY;
    root->inode = 1;
    strcpy(root->name, "/");
    return root;
}

static filesystem_t dummy_fs = {
    .name = "testbusy",
    .mount = dummy_mount,
    .next = NULL
};

void run_vfs_busy_tests(void) {
    kprint("TEST: vfs_busy starting...\n");

    // Register dummy FS
    vfs_register_filesystem(&dummy_fs);

    // Create mount point
    // We assume fs_root is mounted and writable, usually ramfs or ext2
    if (vfs_mkdir("/test_busy_mnt", 0755) != 0) {
        // Might exist
    }

    // Mount
    kprint("TEST: Mounting testbusy...\n");
    int ret = vfs_mount_legacy(NULL, "/test_busy_mnt", "testbusy", 0, NULL);
    if (ret != 0) {
        kprint("FAIL: vfs_mount_legacy failed: ");
        // kprint_int(ret);
        kprint("\n");
        return;
    }

    // 1. Open the mount point root directory
    kprint("TEST: Opening file on mount...\n");
    int fd = sys_open("/test_busy_mnt", 0, 0); // O_RDONLY
    if (fd < 0) {
        kprint("FAIL: sys_open failed\n");
        // Try to cleanup
        vfs_unmount_legacy("/test_busy_mnt");
        return;
    }

    // 2. Try to unmount - should fail because fd is open
    kprint("TEST: Attempting unmount (should fail)...\n");
    ret = vfs_unmount_legacy("/test_busy_mnt");
    if (ret == -16) { // EBUSY
        kprint("PASS: vfs_unmount_legacy returned EBUSY as expected\n");
    } else {
        kprint("FAIL: vfs_unmount_legacy returned unexpected code (expected -16)\n");
    }

    // 3. Close file
    kprint("TEST: Closing file...\n");
    sys_close(fd);

    // 4. Try to unmount - should succeed
    kprint("TEST: Attempting unmount (should succeed)...\n");
    ret = vfs_unmount_legacy("/test_busy_mnt");
    if (ret == 0) {
        kprint("PASS: vfs_unmount_legacy succeeded after close\n");
    } else {
        kprint("FAIL: vfs_unmount_legacy returned error after close\n");
    }

    // Cleanup mount point?
    // vfs_rmdir("/test_busy_mnt");
}
