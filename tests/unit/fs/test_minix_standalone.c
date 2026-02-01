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
#include "../../../sys/fs/minix/minix.c"

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

// Helper to inspect ramdisk
void hexdump_block(uint32_t block) {
    printf("Block %d:\n", block);
    for (int i = 0; i < 32; i++) {
        printf("%02X ", ramdisk[block * 1024 + i]);
    }
    printf("\n");
}

struct minix_dirent_v1 { uint16_t inode; char name[30]; } __attribute__((packed));

void init_minix_v1(void) {
    memset(ramdisk, 0, RAMDISK_SIZE);

    // Boot block (0)

    // Superblock (1)
    struct minix_superblock *sb = (struct minix_superblock *)(ramdisk + 1024);
    sb->s_ninodes = 100;
    sb->s_nzones = 100;
    sb->s_imap_blocks = 1;
    sb->s_zmap_blocks = 1;
    sb->s_firstdatazone = 4; // boot(1)+sb(1)+imap(1)+zmap(1) = 4 blocks used. Data starts at 4?
                             // wait, blocks are 0-indexed.
                             // 0: boot
                             // 1: sb
                             // 2: imap
                             // 3: zmap
                             // 4: inodes?
                             // minix.c: inode_start_block = 2 + s_imap_blocks + s_zmap_blocks = 2+1+1=4.
                             // So Inodes at 4.
                             // How many inode blocks? 100 inodes / (1024/32) = 100/32 = 3.125 -> 4 blocks.
                             // So inodes at 4, 5, 6, 7.
                             // Data starts at 8.
    sb->s_firstdatazone = 8;
    sb->s_log_zone_size = 0;
    sb->s_max_size = 0xFFFFFFFF;
    sb->s_magic = MINIX_V1_Magic;

    // Set Inode Bitmap (Block 2)
    // Bit 0 is unused (inode 0 is null). Bit 1 is Root.
    // 0000 0011 -> 0x03 (Inode 0 and 1 used).
    // Wait, is inode 0 used? Usually Minix inodes are 1-based. Inode 0 is not used.
    // Bitmap bit 0 corresponds to inode 1? Or inode 0?
    // Minix usually: bit 0 -> inode 1? No, usually bit 0 -> inode 0 (reserved).
    // Let's assume bit 0 -> inode 0, bit 1 -> inode 1.
    ramdisk[2 * 1024] = 0x03;

    // Set Zone Bitmap (Block 3)
    // Bit 0->zone 0. Zone 0 usually reserved.
    // s_firstdatazone is 8.
    // So zones 0..7 are metadata?
    // Minix zones refer to absolute blocks usually (in V1).
    // So bits 0..7 should be marked used.
    ramdisk[3 * 1024] = 0xFF; // Zones 0-7 used.

    // Root Inode (Inode 1) at Block 4
    // Inode 1 is at index (1-1) = 0.
    struct minix_inode_v1 *root_inode = (struct minix_inode_v1 *)(ramdisk + 4 * 1024);
    root_inode->i_mode = 0x41FF; // Directory + rwx
    root_inode->i_uid = 0;
    root_inode->i_size = 32 * 2; // 2 entries
    root_inode->i_time = 0;
    root_inode->i_nlinks = 2;
    root_inode->i_zone[0] = 8; // Data at zone 8

    // Root Directory Entries at Zone 8 (Block 8)
    struct minix_dirent_v1 *dirs = (struct minix_dirent_v1 *)(ramdisk + 8 * 1024);

    dirs[0].inode = 1; strcpy(dirs[0].name, ".");
    dirs[1].inode = 1; strcpy(dirs[1].name, "..");
}

void init_minix_v2(void) {
    memset(ramdisk, 0, RAMDISK_SIZE);

    struct minix_superblock *sb = (struct minix_superblock *)(ramdisk + 1024);
    sb->s_ninodes = 100;
    sb->s_zones = 100; // V2 uses s_zones
    sb->s_imap_blocks = 1;
    sb->s_zmap_blocks = 1;
    sb->s_firstdatazone = 8;
    sb->s_log_zone_size = 0;
    sb->s_max_size = 0xFFFFFFFF;
    sb->s_magic = MINIX_V2_Magic;

    ramdisk[2 * 1024] = 0x03;
    ramdisk[3 * 1024] = 0xFF;

    // Root Inode (Inode 1) at Block 4
    // V2 Inode size = 64 bytes
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

bool test_minix_symlink_v1(void) {
    printf("Test: Minix V1 Symlink\n");
    init_minix_v1();

    printf("Mounting...\n");
    fs_node_t *root = minix_mount("ramdisk", 0, &ramdisk_node);
    if (!root) { printf("FAIL: Failed to mount Minix\n"); return false; }

    printf("Mount successful. Root inode: %ld\n", root->inode);

    // Create symlink
    // root is the directory
    // "mylink" is the name
    // "/target/path" is the target
    int ret = minix_symlink(root, "mylink", "/target/path");
    if (ret != 0) {
        printf("FAIL: minix_symlink returned %d\n", ret);
        kfree(root, sizeof(fs_node_t));
        return false;
    } else {
        printf("minix_symlink returned success\n");
    }

    // Verify directory entry exists
    // We can use minix_finddir
    fs_node_t *link_node = minix_finddir(root, "mylink");
    if (!link_node) {
        printf("FAIL: Created symlink not found in dir\n");
        kfree(root, sizeof(fs_node_t));
        return false;
    }

    // Verify node properties
    if ((link_node->flags & FS_SYMLINK) == 0) {
        printf("FAIL: Node is not marked as symlink (flags=%x)\n", link_node->flags);
        // continue
    }

    // Verify target content
    char buf[64] = {0};
    ssize_t len = minix_readlink(link_node, buf, sizeof(buf));
    if (len < 0) {
        printf("FAIL: readlink failed\n");
    } else {
        if (strcmp(buf, "/target/path") != 0) {
            printf("FAIL: Link target mismatch: '%s', expected '/target/path'\n", buf);
        } else {
            printf("PASS: Link target verified\n");
        }
    }

    kfree(link_node, sizeof(fs_node_t));
    kfree(root, sizeof(fs_node_t));
    return (ret == 0);
}

bool test_minix_symlink_v2(void) {
    printf("Test: Minix V2 Symlink\n");
    init_minix_v2();

    printf("Mounting...\n");
    fs_node_t *root = minix_mount("ramdisk", 0, &ramdisk_node);
    if (!root) { printf("FAIL: Failed to mount Minix\n"); return false; }

    printf("Mount successful. Root inode: %ld\n", root->inode);

    int ret = minix_symlink(root, "mylink_v2", "/target/path/v2");
    if (ret != 0) {
        printf("FAIL: minix_symlink returned %d\n", ret);
        kfree(root, sizeof(fs_node_t));
        return false;
    } else {
        printf("minix_symlink returned success\n");
    }

    fs_node_t *link_node = minix_finddir(root, "mylink_v2");
    if (!link_node) {
        printf("FAIL: Created symlink not found in dir\n");
        kfree(root, sizeof(fs_node_t));
        return false;
    }

    if ((link_node->flags & FS_SYMLINK) == 0) {
        printf("FAIL: Node is not marked as symlink (flags=%x)\n", link_node->flags);
    }

    char buf[64] = {0};
    ssize_t len = minix_readlink(link_node, buf, sizeof(buf));
    if (len < 0) {
        printf("FAIL: readlink failed\n");
    } else {
        if (strcmp(buf, "/target/path/v2") != 0) {
            printf("FAIL: Link target mismatch: '%s', expected '/target/path/v2'\n", buf);
        } else {
            printf("PASS: Link target verified\n");
        }
    }

    kfree(link_node, sizeof(fs_node_t));
    kfree(root, sizeof(fs_node_t));
    return (ret == 0);
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0); // Disable buffering
    printf("Starting test_minix...\n");
    bool pass = true;
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
