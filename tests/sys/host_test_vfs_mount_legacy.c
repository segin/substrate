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

static int fail_kmalloc = 0;
void *kmalloc(size_t size) {
    if (fail_kmalloc) return NULL;
    return malloc(size);
}

void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

thread_t *current_thread = NULL;
process_t *current_process = NULL;
const char *perso_name(int perso) { (void)perso; return "mock"; }

process_t *proc_first(void) { return NULL; }
process_t *proc_next(process_t *p) { (void)p; return NULL; }

static int unmount_called = 0;
static fs_node_t dummy_root;


static fs_node_t *last_mount_dev_node = NULL;
static fs_node_t *dummy_mount_fn(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags; (void)data;
    if (device && strcmp(device, "fail_init") == 0) return NULL;
    last_mount_dev_node = (fs_node_t *)data;
    return &dummy_root;
}


static int dummy_unmount_fn(fs_node_t *node) {
    (void)node;
    unmount_called = 1;
    return 0;
}




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
static blkdev_t mock_blkdev_ram1;

blkdev_t *blkdev_first(void) { return &mock_blkdev_root; }
blkdev_t *blkdev_get(const char *name) {
    blkdev_t *b = blkdev_first();
    while (b) {
        if (strcmp(name, b->name) == 0) return b;
        b = b->next;
    }
    return NULL;
}



#define ASSERT_EQ(actual, expected) \
    if ((long)(actual) != (long)(expected)) { \
        printf("FAIL: %s:%d: Expected %ld, got %ld\n", __FILE__, __LINE__, (long)(expected), (long)(actual)); \
        return 1; \
    }




fs_node_t *mock_finddir(fs_node_t *node, char *name) {

    (void)node;
    static fs_node_t file_node;
    file_node.flags = FS_FILE;

    static fs_node_t dir_node;
    dir_node.flags = FS_DIRECTORY;

    if (strcmp(name, "file") == 0) return &file_node;
    if (strcmp(name, "dir") == 0) return &dir_node;
    return NULL;
}

static fs_node_t mock_dev_ram0;

fs_node_t *mock_devfs_finddir(fs_node_t *node, char *name) {
    (void)node;
    static fs_node_t storage_dir;
    storage_dir.flags = FS_DIRECTORY;
    storage_dir.finddir = mock_devfs_finddir;

    if (strcmp(name, "storage") == 0) return &storage_dir;
    if (strcmp(name, "ram0") == 0) return &mock_dev_ram0;
    return NULL;
}



static int mock_read_label(struct blkdev *dev, char *label, size_t len) {

    if (strcmp(dev->name, "mock_labelled") == 0) {
        strncpy(label, "root", len);
        return 0;
    }
    return -1;
}

static filesystem_t dummy_fs = {
    .name = "dummy",
    .mount = dummy_mount_fn,
    .read_label = mock_read_label,
    .next = NULL
};

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
    fs_root = &my_root;

    // Test label resolution (LABEL=root)
    strncpy(mock_blkdev_root.name, "mock_labelled", sizeof(mock_blkdev_root.name));
    mock_blkdev_root.next = &mock_blkdev_ram1;
    strncpy(mock_blkdev_ram1.name, "ram1", sizeof(mock_blkdev_ram1.name));
    mock_blkdev_ram1.next = NULL;

    last_mount_dev_node = NULL;

    ASSERT_EQ(vfs_mount_legacy("LABEL=root", "/dir", "dummy", 0, NULL), 0);

    // Since /dev/storage/mock_labelled is generated but devfs isn't matching it, it falls back to early boot fallback.
    ASSERT_EQ(last_mount_dev_node, &mock_blkdev_root.node);

    // Test label resolution failure (LABEL=missing)
    ASSERT_EQ(vfs_mount_legacy("LABEL=missing", "/dir", "dummy", 0, NULL), -19); // -ENODEV

    // Test devfs node lookup
    static fs_node_t my_devfs_root;
    memset(&my_devfs_root, 0, sizeof(my_devfs_root));
    my_devfs_root.flags = FS_DIRECTORY;
    my_devfs_root.finddir = mock_devfs_finddir;
    devfs_root_node_ptr = &my_devfs_root;

    last_mount_dev_node = NULL;
    ASSERT_EQ(vfs_mount_legacy("/dev/storage/ram0", "/dir", "dummy", 0, NULL), 0);
    ASSERT_EQ(last_mount_dev_node, &mock_dev_ram0);

    // Test early boot block device fallback (devfs active, but node not found)
    last_mount_dev_node = NULL;
    ASSERT_EQ(vfs_mount_legacy("/dev/storage/ram1", "/dir", "dummy", 0, NULL), 0);
    ASSERT_EQ(last_mount_dev_node, &mock_blkdev_ram1.node);

    // Test kmalloc failure for mount entry allocation
    fail_kmalloc = 1;
    // Expected to return 0 because mount is structurally successful, but a warning is printed.
    ASSERT_EQ(vfs_mount_legacy("dev", "/", "dummy", 0, NULL), 0);
    fail_kmalloc = 0;

    printf("PASS\n");

    return 0;
}
