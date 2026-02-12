#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Define HOST_TEST to enable any conditional logic in kernel headers
#ifndef HOST_TEST
#define HOST_TEST
#endif

// Mock Kernel Functions
void kprint(const char *msg) {
    printf("[KERNEL] %s", msg);
}

// Simple Mock Memory Management
void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Needed types
#include <sys/types.h>
#include <vfs/vfs.h>

// Mock VFS functions used by minix.c
size_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    if (node->read) return node->read(node, offset, size, buffer);
    return 0;
}

size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    if (node->write) return node->write(node, offset, size, buffer);
    return 0;
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Rename colliding kernel function
#define vasprintf kernel_vasprintf

// Include the source under test
// This allows us to access static functions like minix_read_inode
#include <fs/minix/minix.c"

// ------------------------------------------------------------------
// Test Helpers
// ------------------------------------------------------------------

#define RAMDISK_SIZE (1024 * 1024) // 1MB
uint8_t ramdisk[RAMDISK_SIZE];

size_t ramdisk_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset >= RAMDISK_SIZE) return 0;
    if (offset + size > RAMDISK_SIZE) size = RAMDISK_SIZE - offset;
    memcpy(buffer, ramdisk + offset, size);
    return size;
}

size_t ramdisk_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (offset >= RAMDISK_SIZE) return 0;
    if (offset + size > RAMDISK_SIZE) size = RAMDISK_SIZE - offset;
    memcpy(ramdisk + offset, buffer, size);
    return size;
}

fs_node_t ramdisk_node = {
    .read = ramdisk_read,
    .write = ramdisk_write,
    .flags = FS_BLOCKDEVICE
};

void init_minix_v1(void) {
    memset(ramdisk, 0, RAMDISK_SIZE);
    struct minix_superblock *sb = (struct minix_superblock *)(ramdisk + 1024);
    sb->s_ninodes = 100;
    sb->s_nzones = 100;
    sb->s_imap_blocks = 1;
    sb->s_zmap_blocks = 1;
    sb->s_firstdatazone = 8;
    sb->s_log_zone_size = 0;
    sb->s_max_size = 0xFFFFFFFF;
    sb->s_magic = MINIX_V1_Magic;
    ramdisk[2 * 1024] = 0x03;
    ramdisk[3 * 1024] = 0xFF;
    struct minix_inode_v1 *root_inode = (struct minix_inode_v1 *)(ramdisk + 4 * 1024);
    root_inode->i_mode = 0x41FF;
    root_inode->i_uid = 0;
    root_inode->i_size = 32 * 2;
    root_inode->i_time = 0;
    root_inode->i_nlinks = 2;
    root_inode->i_zone[0] = 8;
    struct minix_dirent_v1 *dirs = (struct minix_dirent_v1 *)(ramdisk + 8 * 1024);
    dirs[0].inode = 1; strcpy(dirs[0].name, ".");
    dirs[1].inode = 1; strcpy(dirs[1].name, "..");
}

void init_minix_v2(void) {
    memset(ramdisk, 0, RAMDISK_SIZE);
    struct minix_superblock *sb = (struct minix_superblock *)(ramdisk + 1024);
    sb->s_ninodes = 100;
    sb->s_zones = 100;
    sb->s_imap_blocks = 1;
    sb->s_zmap_blocks = 1;
    sb->s_firstdatazone = 8;
    sb->s_log_zone_size = 0;
    sb->s_max_size = 0xFFFFFFFF;
    sb->s_magic = MINIX_V2_Magic;
    ramdisk[2 * 1024] = 0x03;
    ramdisk[3 * 1024] = 0xFF;
    struct minix_inode_v2 *root_inode = (struct minix_inode_v2 *)(ramdisk + 4 * 1024);
    root_inode->i_mode = 0x41FF;
    root_inode->i_uid = 0;
    root_inode->i_size = 32 * 2;
    root_inode->i_atime = 0;
    root_inode->i_mtime = 0;
    root_inode->i_ctime = 0;
    root_inode->i_nlinks = 2;
    root_inode->i_zone[0] = 8;
    struct minix_dirent_v1 *dirs = (struct minix_dirent_v1 *)(ramdisk + 8 * 1024);
    dirs[0].inode = 1; strcpy(dirs[0].name, ".");
    dirs[1].inode = 1; strcpy(dirs[1].name, "..");
}

bool test_minix_link_v1(void) {
    printf("Test: Minix V1 Hard Link\n");
    init_minix_v1();
    fs_node_t *root = minix_mount("ramdisk", 0, &ramdisk_node);
    if (!root) return false;

    if (minix_mknod(root, "file1", 0x81FF, 0) != 0) {
        printf("FAIL: mknod failed\n"); return false;
    }
    fs_node_t *file1 = minix_finddir(root, "file1");
    if (!file1) { printf("FAIL: file1 not found\n"); return false; }

    if (minix_link(root, file1, "link1") != 0) {
        printf("FAIL: minix_link failed\n"); return false;
    }

    struct minix_inode_v1 *cached = (struct minix_inode_v1 *)file1->ptr;
    if (cached->i_nlinks != 2) {
        printf("FAIL: Cached link count is %d, expected 2\n", cached->i_nlinks);
        return false;
    }

    fs_node_t file1_fresh;
    memset(&file1_fresh, 0, sizeof(file1_fresh));
    if (minix_read_inode((minix_fs_t*)root->impl, file1->inode, &file1_fresh) != 0) {
        printf("FAIL: re-read inode failed\n"); return false;
    }
    struct minix_inode_v1 *fresh = (struct minix_inode_v1 *)file1_fresh.ptr;
    if (fresh->i_nlinks != 2) {
        printf("FAIL: On-disk link count is %d, expected 2\n", fresh->i_nlinks);
        return false;
    }
    kfree(file1_fresh.ptr, sizeof(struct minix_inode_v1));
    kfree(file1->ptr, sizeof(struct minix_inode_v1));
    kfree(file1, sizeof(fs_node_t));
    minix_unmount(root);
    return true;
}

bool test_minix_link_v2(void) {
    printf("Test: Minix V2 Hard Link\n");
    init_minix_v2();
    fs_node_t *root = minix_mount("ramdisk", 0, &ramdisk_node);
    if (!root) return false;

    if (minix_mknod(root, "file2", 0x81FF, 0) != 0) {
        printf("FAIL: mknod failed\n"); return false;
    }
    fs_node_t *file2 = minix_finddir(root, "file2");
    if (!file2) return false;

    if (minix_link(root, file2, "link2") != 0) {
        printf("FAIL: minix_link failed\n"); return false;
    }

    struct minix_inode_v2 *cached = (struct minix_inode_v2 *)file2->ptr;
    if (cached->i_nlinks != 2) {
        printf("FAIL: Cached link count is %d, expected 2\n", cached->i_nlinks);
        return false;
    }

    fs_node_t file2_fresh;
    memset(&file2_fresh, 0, sizeof(file2_fresh));
    if (minix_read_inode((minix_fs_t*)root->impl, file2->inode, &file2_fresh) != 0) return false;
    struct minix_inode_v2 *fresh = (struct minix_inode_v2 *)file2_fresh.ptr;
    if (fresh->i_nlinks != 2) {
        printf("FAIL: On-disk link count is %d, expected 2\n", fresh->i_nlinks);
        return false;
    }
    kfree(file2_fresh.ptr, sizeof(struct minix_inode_v2));
    kfree(file2->ptr, sizeof(struct minix_inode_v2));
    kfree(file2, sizeof(fs_node_t));
    minix_unmount(root);
    return true;
}

bool test_minix_symlink_v1(void) {
    printf("Test: Minix V1 Symlink\n");
    init_minix_v1();

    fs_node_t *root = minix_mount("ramdisk", 0, &ramdisk_node);
    if (!root) return false;

    int ret = minix_symlink(root, "mylink", "/target/path");
    if (ret != 0) {
        printf("FAIL: minix_symlink returned %d\n", ret);
        return false;
    }

    fs_node_t *link_node = minix_finddir(root, "mylink");
    if (!link_node) {
        printf("FAIL: Created symlink not found in dir\n");
        return false;
    }

    if ((link_node->flags & FS_SYMLINK) == 0) {
        printf("FAIL: Node is not marked as symlink (flags=%x)\n", link_node->flags);
    }

    char buf[64] = {0};
    ssize_t len = minix_readlink(link_node, buf, sizeof(buf));
    if (len < 0 || strcmp(buf, "/target/path") != 0) {
        printf("FAIL: Link target mismatch\n");
        return false;
    }

    kfree(link_node->ptr, sizeof(struct minix_inode_v1));
    kfree(link_node, sizeof(fs_node_t));
    minix_unmount(root);
    return true;
}

bool test_minix_symlink_v2(void) {
    printf("Test: Minix V2 Symlink\n");
    init_minix_v2();

    fs_node_t *root = minix_mount("ramdisk", 0, &ramdisk_node);
    if (!root) return false;

    int ret = minix_symlink(root, "mylink_v2", "/target/path/v2");
    if (ret != 0) {
        printf("FAIL: minix_symlink returned %d\n", ret);
        return false;
    }

    fs_node_t *link_node = minix_finddir(root, "mylink_v2");
    if (!link_node) {
        printf("FAIL: Created symlink not found in dir\n");
        return false;
    }

    if ((link_node->flags & FS_SYMLINK) == 0) {
        printf("FAIL: Node is not marked as symlink (flags=%x)\n", link_node->flags);
    }

    char buf[64] = {0};
    ssize_t len = minix_readlink(link_node, buf, sizeof(buf));
    if (len < 0 || strcmp(buf, "/target/path/v2") != 0) {
        printf("FAIL: Link target mismatch\n");
        return false;
    }

    kfree(link_node->ptr, sizeof(struct minix_inode_v2));
    kfree(link_node, sizeof(fs_node_t));
    minix_unmount(root);
    return true;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("Starting test_minix...\n");
    bool pass = true;
    pass &= test_minix_link_v1();
    pass &= test_minix_link_v2();
    pass &= test_minix_symlink_v1();
    pass &= test_minix_symlink_v2();

    if (pass) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("TESTS FAILED\n");
        return 1;
    }
}
