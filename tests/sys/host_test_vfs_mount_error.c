#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// Mock kprint using printf
void kprint(const char *str) {
    printf("%s", str);
}

// Handle vasprintf conflict
#define vasprintf kernel_vasprintf

// Mock get_time used in vfs.c
// Note: We need to define it before including vfs.c if vfs.c uses it.
// But vfs.c just declares extern int64_t get_time(void); inside functions.
#include <stdint.h>
int64_t get_time(void) { return 0; }

// Use -Isys to resolve <vfs/vfs.h> etc.
#include <vfs/vfs.h>

// Mock external init functions required by vfs_init
void ext2_init(void) {}
void fat_init(void) {}
void exfat_init(void) {}
void minix_init(void) {}
void udf_init(void) {}
void devfs_init(void) {}
void procfs_init(void) {}
void sysfs_init(void) {}
void fuse_init(void) {}
void fuse_fs_init(void) {}
void p9_init(void) {}
void pseudo_init(void) {}

// devfs_register_device is used in vfs.h or somewhere?
// It is declared in vfs.h. We define it here.
void devfs_register_device(fs_node_t *node) { (void)node; }

// devfs_root_node_ptr is extern in vfs.c
fs_node_t *devfs_root_node_ptr = NULL;

// Global to track unmount call
static int unmount_called = 0;

static fs_node_t dummy_root;

static fs_node_t *dummy_mount_fn(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    printf("DEBUG: dummy_mount called\n");
    return &dummy_root;
}

static int dummy_unmount_fn(fs_node_t *node) {
    (void)node;
    printf("DEBUG: dummy_unmount called\n");
    unmount_called = 1;
    return 0;
}

static filesystem_t dummy_fs = {
    .name = "dummy",
    .mount = dummy_mount_fn,
    .next = NULL
};

// Include the source under test
#include "../../sys/vfs/vfs.c"

int main() {
    // Register our dummy filesystem
    // filesystems is static in vfs.c, so we can't access it directly unless we use accessor
    // or call vfs_register_filesystem which is in vfs.c
    vfs_register_filesystem(&dummy_fs);

    // Setup a fake root
    static fs_node_t my_root;
    memset(&my_root, 0, sizeof(my_root));
    my_root.flags = FS_DIRECTORY;
    // No finddir => vfs_lookup fails for any child
    fs_root = &my_root;

    // Setup dummy_root unmount handler
    dummy_root.unmount = dummy_unmount_fn;

    printf("Test: Calling vfs_mount with non-existent path...\n");
    // "dummy" fs type matches our registered dummy_fs
    int ret = vfs_mount("dummy_dev", "/nonexistent", "dummy", 0, NULL);

    if (ret != -1) {
        printf("FAIL: vfs_mount should return -1, got %d\n", ret);
        return 1;
    }

    if (unmount_called) {
        printf("SUCCESS: unmount was called.\n");
        return 0;
    } else {
        printf("FAIL: unmount was NOT called.\n");
        return 1;
    }
}
