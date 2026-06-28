#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void kprint(const char *str) { (void)str; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }

#define vasprintf kernel_vasprintf

#include <stdint.h>
#include <sys/types.h>
typedef long register_t;
int64_t get_time(void) { return 0; }
int64_t kern_time(int64_t *tloc) { (void)tloc; return 0; }

#include <vfs/vfs.h>
#include <sys/proc.h>

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
void cpuid_init(void) {}
void shmfs_init(void) {}
void full_init(void) {}

void devfs_register_device(fs_node_t *node) { (void)node; }
fs_node_t *devfs_root_node_ptr = NULL;

void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

thread_t *current_thread = NULL;
process_t *current_process = NULL;
const char *perso_name(int perso) { (void)perso; return "mock"; }

process_t *proc_first(void) { return NULL; }
process_t *proc_next(process_t *p) { (void)p; return NULL; }

static int unmount_called = 0;
static fs_node_t dummy_root;

static fs_node_t *dummy_mount_fn(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    if (device && strcmp(device, "fail_init") == 0) return NULL;
    return &dummy_root;
}

static int dummy_unmount_fn(fs_node_t *node) {
    (void)node;
    unmount_called = 1;
    return 0;
}

static filesystem_t dummy_fs = {
    .name = "dummy",
    .mount = dummy_mount_fn,
    .next = NULL
};

// Instead of struct blkdev redefinitions which clash, we'll let blkdev.h be included
// and define our mocks using the real types
void namei_init(void) {}
void nchinit(void) {}
void bio_init(void) {}
void sysv_init(void) {}
void pty_init(void) {}
void kernel_kfree(void *ptr, size_t size) { (void)size; free(ptr); }
#define kfree kernel_kfree

// Include first to get real blkdev_t definition
#include "../../sys/vfs/vfs.c"

static blkdev_t mock_blkdev_root;
blkdev_t *blkdev_first(void) { return &mock_blkdev_root; }
blkdev_t *blkdev_get(const char *name) {
    if (name && strcmp(name, mock_blkdev_root.name) == 0) return &mock_blkdev_root;
    return NULL;
}

#define ASSERT_EQ(actual, expected) \
    if ((actual) != (expected)) { \
        printf("FAIL: %s:%d: Expected %d, got %d\n", __FILE__, __LINE__, (expected), (actual)); \
        return 1; \
    }

fs_node_t *mock_finddir(fs_node_t *node, const char *name) {
    (void)node;
    static fs_node_t file_node;
    file_node.flags = FS_FILE;

    static fs_node_t dir_node;
    dir_node.flags = FS_DIRECTORY;

    if (strcmp(name, "file") == 0) return &file_node;
    if (strcmp(name, "dir") == 0) return &dir_node;
    return NULL;
}

int main() {
    printf("Testing vfs_mount_legacy...\n");

    TAILQ_INIT(&mountlist);
    vfs_register_filesystem(&dummy_fs);

    ASSERT_EQ(vfs_mount_legacy(NULL, NULL, NULL, 0, NULL), -EINVAL);
    ASSERT_EQ(vfs_mount_legacy("dev", NULL, "dummy", 0, NULL), -EINVAL);
    ASSERT_EQ(vfs_mount_legacy("dev", "/path", NULL, 0, NULL), -EINVAL);

    ASSERT_EQ(vfs_mount_legacy("dev", "/path", "dummy", MNT_RDONLY | MNT_ASYNC, NULL), -EINVAL);
    ASSERT_EQ(vfs_mount_legacy("dev", "/path", "dummy", MNT_SYNCHRONOUS | MNT_ASYNC, NULL), -EINVAL);

    ASSERT_EQ(vfs_mount_legacy("dev", "/path", "unknown_fs", 0, NULL), -EUNKNOWNFS);

    ASSERT_EQ(vfs_mount_legacy("fail_init", "/path", "dummy", 0, NULL), -EIO);

    static fs_node_t my_root;
    memset(&my_root, 0, sizeof(my_root));
    my_root.flags = FS_DIRECTORY;
    my_root.finddir = (struct fs_node *(*)(struct fs_node *, char *)) mock_finddir;

    fs_root = &my_root;
    dummy_root.unmount = dummy_unmount_fn;

    unmount_called = 0;
    ASSERT_EQ(vfs_mount_legacy("dev", "/nonexistent", "dummy", 0, NULL), -ENOENT);
    ASSERT_EQ(unmount_called, 1);

    unmount_called = 0;
    ASSERT_EQ(vfs_mount_legacy("dev", "/file", "dummy", 0, NULL), -ENOTDIR);
    ASSERT_EQ(unmount_called, 1);

    unmount_called = 0;
    ASSERT_EQ(vfs_mount_legacy("dev", "/dir", "dummy", 0, NULL), 0);
    ASSERT_EQ(unmount_called, 0);

    ASSERT_EQ(vfs_mount_legacy("dev", "/", "dummy", 0, NULL), 0);

    printf("PASS\n");
    return 0;
}
