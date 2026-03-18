#include <kern/console.h>
#include <fs/minix/minix.h>
#include <vfs/vfs.h>
#include <string.h>
#include "tests.h"

static uint8_t mock_sb_buf[1024];

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset == 1024 && size <= 1024) { // Read superblock at 1024 (Block 1)
        memcpy(buffer, mock_sb_buf, size);
        return size;
    }
    // IMAP/ZMAP/Inode blocks - return zeros
    memset(buffer, 0, size);
    return size;
}

static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    // Just pretend write succeeded
    return size;
}

void run_minix_mount_tests(void) {
    kprint("TEST: Checking Minix Filesystem...\n");
    minix_init();
    
    filesystem_t *fs = vfs_get_filesystems();
    while (fs && strcmp(fs->name, "minix") != 0) {
        fs = fs->next;
    }
    
    if (!fs) {
        kprint("FAIL: Minix filesystem not registered\n");
        return;
    }
    kprint("PASS: Minix filesystem found\n");

    // Mock device
    fs_node_t device_node;
    memset(&device_node, 0, sizeof(fs_node_t));
    strcpy(device_node.name, "ram0");
    device_node.read = mock_read;
    device_node.write = mock_write;
    device_node.flags = FS_BLOCKDEVICE;

    // Test 1: Invalid Magic (Zeroed buffer)
    memset(mock_sb_buf, 0, 1024);
    if (fs->mount(NULL, 0, &device_node) == NULL) {
        kprint("PASS: Minix mount rejected invalid magic\n");
    } else {
        kprint("FAIL: Minix mount accepted invalid magic\n");
    }

    // Test 2: Valid Magic (V1)
    struct minix_superblock *sb = (struct minix_superblock *)mock_sb_buf;
    sb->s_magic = MINIX_V1_Magic;
    sb->s_nzones = 100;
    sb->s_ninodes = 100;
    
    fs_node_t *mount_node = fs->mount(NULL, 0, &device_node);
    if (mount_node) {
        kprint("PASS: Minix mount accepted valid V1 magic\n");
        if (mount_node->inode == MINIX_ROOT_INODE) {
             kprint("PASS: Minix root inode correct\n");
        } else {
             kprint("FAIL: Minix root inode incorrect\n");
        }

        // Test Mknod
        if (mount_node->mknod) {
            // IMAP is all zeros, so alloc should find inode 1 (or 0)
            // But root is 1. If we pretend 0 is free...
            // minix_alloc_inode should return valid number.
            if (mount_node->mknod(mount_node, "testdev", 0x21B6, 0x0301) == 0) {
                 kprint("PASS: Minix mknod executed\n");
            } else {
                 kprint("FAIL: Minix mknod failed\n");
            }
        } else {
             kprint("FAIL: Minix mknod callback not set\n");
        }

        // Test Unmount
        if (mount_node->unmount) {
             if (mount_node->unmount(mount_node) == 0) {
                 kprint("PASS: Minix unmount success\n");
             } else {
                 kprint("FAIL: Minix unmount returned error\n");
             }
        } else {
             kprint("FAIL: Minix unmount callback not set\n");
        }
    } else {
        kprint("FAIL: Minix mount failed with valid V1 magic\n");
    }
}
