#include <kern/console.h>
#include <fs/minix/minix.h>
#include <vfs/vfs.h>
#include <string.h>
#include "tests.h"

extern void minix_init(void);

static uint8_t mock_sb_buf[1024];
static uint8_t mock_inode_buf[1024];
static uint8_t mock_dir_buf[1024];

#define MOCK_DIR_BLOCK 10
#define MOCK_INODE_START_BLOCK 4

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    uint32_t block = offset / 1024;

    if (block == 1) { // Superblock
        memcpy(buffer, mock_sb_buf, size);
        return size;
    }

    if (block == MOCK_INODE_START_BLOCK) { // Inodes
        memcpy(buffer, mock_inode_buf, size);
        return size;
    }

    if (block == MOCK_DIR_BLOCK) { // Directory Data
        memcpy(buffer, mock_dir_buf, size);
        return size;
    }

    // Default: zeros
    memset(buffer, 0, size);
    return size;
}

static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

void run_minix_readdir_tests(void) {
    kprint("TEST: Checking Minix Readdir Security Fix...\n");

    // Ensure Minix is registered
    filesystem_t *fs = vfs_get_filesystems();
    bool found = false;
    while (fs) {
        if (strcmp(fs->name, "minix") == 0) {
            found = true;
            break;
        }
        fs = fs->next;
    }

    if (!found) {
        minix_init();
        fs = vfs_get_filesystems();
        while (fs && strcmp(fs->name, "minix") != 0) fs = fs->next;
    }

    if (!fs) {
        kprint("FAIL: Minix filesystem not available\n");
        return;
    }

    // Setup Mock Superblock (V1)
    memset(mock_sb_buf, 0, 1024);
    struct minix_superblock *sb = (struct minix_superblock *)mock_sb_buf;
    sb->s_magic = MINIX_V1_Magic;
    sb->s_nzones = 100;
    sb->s_ninodes = 32;
    sb->s_imap_blocks = 1;
    sb->s_zmap_blocks = 1;
    sb->s_firstdatazone = 10;
    // Inodes start at 2 + 1 + 1 = 4.

    // Setup Mock Inode 1 (Root)
    memset(mock_inode_buf, 0, 1024);
    struct minix_inode_v1 *inode = (struct minix_inode_v1 *)mock_inode_buf;
    // Inode 1 is at index 0 of the block (technically inode 0 is reserved but Minix inodes are 1-based,
    // so inode 1 is usually at index 0 or 1 depending on implementation.
    // In minix.c: offset = ((inode_num - 1) % inodes_per_block) * size.
    // So inode 1 is at index 0.

    inode->i_mode = 0x4000 | 0777; // Directory
    inode->i_uid = 0;
    inode->i_size = 64; // Enough for 2 entries
    inode->i_nlinks = 1;
    inode->i_zone[0] = MOCK_DIR_BLOCK; // Point to data block 10

    // Setup Mock Directory Data
    // Entry 0: . (dot)
    // Entry 1: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA (30 chars)
    memset(mock_dir_buf, 0, 1024);
    struct minix_dirent_v1 *d = (struct minix_dirent_v1 *)mock_dir_buf;

    // Entry 0
    d[0].inode = 1;
    strcpy(d[0].name, ".");

    // Entry 1 - The Vulnerability Test Case
    d[1].inode = 2;
    // Fill with 30 'A's
    memset(d[1].name, 'A', 30);
    // Note: In memory, d[1].name is followed by d[2].inode.
    // We want to verify that minix_readdir doesn't overrun.
    // To be sure we are testing the fix, we should put non-null garbage after d[1].name.
    // But d[1].name is at the end of the struct.
    // The next byte is d[2].inode (if array).
    // Let's set d[2].inode (2 bytes) and d[2].name to 'B's.

    d[2].inode = 0x4242; // 'BB'
    memset(d[2].name, 'B', 30);

    // Mock Device
    fs_node_t device_node;
    memset(&device_node, 0, sizeof(fs_node_t));
    strcpy(device_node.name, "ram_test");
    device_node.read = mock_read;
    device_node.write = mock_write;
    device_node.flags = FS_BLOCKDEVICE;

    // Mount
    fs_node_t *root = fs->mount(NULL, 0, &device_node);
    if (!root) {
        kprint("FAIL: Mount failed\n");
        return;
    }

    // Read Directory Entry 1
    // minix_readdir takes an index (0, 1, 2...)
    // entry 0 is "."
    // entry 1 is "AAAA..."

    struct dirent *dir_ent = root->readdir(root, 1);
    if (!dir_ent) {
        kprint("FAIL: readdir(1) returned NULL\n");
    } else {
        // Verify name length and content
        int len = strlen(dir_ent->d_name);
        if (len != 30) {
        kprintf("FAIL: Name length is %d, expected 30\n", len);
        } else {
            // Verify content is 30 'A's
            bool match = true;
            for (int i = 0; i < 30; i++) {
                if (dir_ent->d_name[i] != 'A') {
                    match = false;
                    break;
                }
            }
            if (match && dir_ent->d_name[30] == '\0') {
                 kprint("PASS: Name is 30 chars 'A' and null-terminated\n");
            } else {
                 kprint("FAIL: Name content mismatch or not null terminated\n");
            }
        }

        // Also verify inode
        if (dir_ent->d_ino != 2) {
            kprintf("FAIL: Inode is %d, expected 2\n", (int)dir_ent->d_ino);
        }
    }

    if (root->unmount) root->unmount(root);
}
