#include <kern/console.h>
#include <fs/minix/minix.h>
#include <vfs/vfs.h>
#include <string.h>
#include "tests.h"

extern void minix_init(void);

#define MOCK_DISK_SIZE (100 * 1024)
static uint8_t mock_disk[MOCK_DISK_SIZE];

static bool fail_inode_write = false;
static bool fail_dir_write = false;

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset + size > MOCK_DISK_SIZE) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (offset + size > MOCK_DISK_SIZE) return 0;

    // Block 4: Inode Table (for this config)
    if (fail_inode_write && offset >= 4 * 1024 && offset < 5 * 1024) {
        kprint("MOCK: Failing inode write\n");
        return 0;
    }

    // Block 10: Directory Data (Zone 10)
    if (fail_dir_write && offset >= 10 * 1024 && offset < 11 * 1024) {
        kprint("MOCK: Failing directory write\n");
        return 0;
    }

    memcpy(mock_disk + offset, buffer, size);
    return size;
}

static void setup_mock_minix(void) {
    memset(mock_disk, 0, MOCK_DISK_SIZE);

    // Superblock at Block 1 (1024)
    struct minix_superblock *sb = (struct minix_superblock *)(mock_disk + 1024);
    sb->s_magic = MINIX_V1_Magic;
    sb->s_ninodes = 32;
    sb->s_nzones = 20;
    sb->s_imap_blocks = 1; // Block 2
    sb->s_zmap_blocks = 1; // Block 3
    sb->s_firstdatazone = 10;
    sb->s_log_zone_size = 0;
    sb->s_max_size = 1024 * 1024;

    // Initialize Root Inode (Inode 1) at Block 4
    // Block 4 offset 0
    struct minix_inode_v1 *root_inode = (struct minix_inode_v1 *)(mock_disk + 4 * 1024);
    root_inode->i_mode = 0x4000 | 0755; // Directory
    root_inode->i_uid = 0;
    root_inode->i_gid = 0;
    root_inode->i_nlinks = 1;
    root_inode->i_size = 0; // Empty
    root_inode->i_zone[0] = 10; // Data zone 10

    // Set IMAP (Block 2)
    // Bit 0: Reserved/Unused
    // Bit 1: Inode 1 (Root) - Used
    // Bit 2: Inode 2 - Free (Target for test)
    mock_disk[2 * 1024] = 0x02; // Binary 0000 0010. Bit 1 set.
}

void run_minix_inode_tests(void) {
    kprint("TEST: Checking Minix Inode Error Handling...\n");
    minix_init();

    filesystem_t *fs = vfs_get_filesystems();
    while (fs && strcmp(fs->name, "minix") != 0) {
        fs = fs->next;
    }

    if (!fs) {
        kprint("FAIL: Minix filesystem not registered\n");
        return;
    }

    // Mock device node
    fs_node_t device_node;
    memset(&device_node, 0, sizeof(fs_node_t));
    strcpy(device_node.name, "ram_test");
    device_node.read = mock_read;
    device_node.write = mock_write;
    device_node.flags = FS_BLOCKDEVICE;

    // --- Scenario 1: Fail Inode Write ---
    kprint("Scenario 1: Fail Inode Write\n");
    setup_mock_minix();
    fail_inode_write = true;
    fail_dir_write = false;

    fs_node_t *root = fs->mount(NULL, 0, &device_node);
    if (!root) {
        kprint("FAIL: Mount failed\n");
        return;
    }

    // Attempt to create a node
    // mknod calls minix_mknod
    if (root->mknod) {
        int res = root->mknod(root, "test1", 0, 0);
        if (res == -1) {
            kprint("PASS: mknod returned error as expected\n");
        } else {
            kprint("FAIL: mknod succeeded unexpectedly\n");
        }

        // Verify Inode 2 is free in IMAP (Block 2)
        // Inode 2 -> Bit 2.
        // Byte 0 should be 0x02 (Only Root Inode 1 set).
        // If allocation succeeded but not freed, it would be 0x06 (0000 0110).
        uint8_t imap_byte = mock_disk[2 * 1024];
        if ((imap_byte & 0x04) == 0) {
            kprint("PASS: Inode bit cleared (freed)\n");
        } else {
            kprint("FAIL: Inode bit still set (leaked)\n");
        }
    } else {
        kprint("FAIL: No mknod support\n");
    }

    if (root->unmount) root->unmount(root);

    // --- Scenario 2: Fail Directory Add ---
    kprint("Scenario 2: Fail Directory Add\n");
    setup_mock_minix();
    fail_inode_write = false;
    fail_dir_write = true;

    root = fs->mount(NULL, 0, &device_node);

    if (root->mknod) {
        int res = root->mknod(root, "test2", 0, 0);
        if (res == -1) {
            kprint("PASS: mknod returned error as expected\n");
        } else {
            kprint("FAIL: mknod succeeded unexpectedly\n");
        }

        uint8_t imap_byte = mock_disk[2 * 1024];
        if ((imap_byte & 0x04) == 0) {
            kprint("PASS: Inode bit cleared (freed)\n");
        } else {
            kprint("FAIL: Inode bit still set (leaked)\n");
        }
    }

    if (root->unmount) root->unmount(root);
}
