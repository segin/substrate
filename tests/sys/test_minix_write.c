#include <kern/console.h>
#include <fs/minix/minix.h>
#include <vfs/vfs.h>
#include <string.h>
#include "tests.h"

extern void minix_init(void);

#define MOCK_DISK_SIZE (100 * 1024)
static uint8_t mock_disk[MOCK_DISK_SIZE];
static bool write_inode_occurred = false;

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset + size > MOCK_DISK_SIZE) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (offset + size > MOCK_DISK_SIZE) return 0;
    memcpy(mock_disk + offset, buffer, size);

    // Check if writing to inode table (Block 4 usually for small FS)
    // Superblock (1) + IMAP (1) + ZMAP (1) + Boot (0) -> Inodes start at Block 4 ?
    // Inode start = 2 + imap + zmap.
    // We will set imap=1, zmap=1. So start = 4.
    if (offset >= 4 * 1024 && offset < 5 * 1024) {
        write_inode_occurred = true;
    }
    return size;
}

void run_minix_write_tests(void) {
    kprint("TEST: Checking Minix Inode Write-back...\n");
    minix_init();

    filesystem_t *fs = vfs_get_filesystems();
    while (fs && strcmp(fs->name, "minix") != 0) {
        fs = fs->next;
    }

    if (!fs) {
        kprint("FAIL: Minix filesystem not registered\n");
        return;
    }

    // Initialize mock disk
    memset(mock_disk, 0, MOCK_DISK_SIZE);

    // Superblock at Block 1 (1024)
    struct minix_superblock *sb = (struct minix_superblock *)(mock_disk + 1024);
    sb->s_magic = MINIX_V1_Magic;
    sb->s_ninodes = 16;
    sb->s_nzones = 20;
    sb->s_imap_blocks = 1;
    sb->s_zmap_blocks = 1;
    sb->s_firstdatazone = 10; // Data starts at block 10
    sb->s_log_zone_size = 0;
    sb->s_max_size = 1024 * 1024;

    // Initialize Root Inode (Inode 1) at Block 4
    // Block 4 offset 0
    struct minix_inode_v1 *root_inode = (struct minix_inode_v1 *)(mock_disk + 4 * 1024);
    root_inode->i_mode = 0x4000 | 0755; // Directory
    root_inode->i_uid = 0;
    root_inode->i_gid = 0;
    root_inode->i_nlinks = 1;
    root_inode->i_size = 32; // Initial size (empty dir entry maybe?)
    root_inode->i_zone[0] = 10; // Point to data zone 10

    // Set IMAP/ZMAP to show inode 1 and zone 10 as used
    mock_disk[2 * 1024] = 0x01; // Inode 1 used (bit 0? minix starts at 1? usually bit 0 is inode 1)
    mock_disk[3 * 1024] = 0x01; // Zone 0 used? Zone mapping is weird.

    // Mock device node
    fs_node_t device_node;
    memset(&device_node, 0, sizeof(fs_node_t));
    strcpy(device_node.name, "ram1");
    device_node.read = mock_read;
    device_node.write = mock_write;
    device_node.flags = FS_BLOCKDEVICE;

    // Mount
    fs_node_t *root = fs->mount(NULL, 0, &device_node);
    if (!root) {
        kprint("FAIL: Mount failed\n");
        return;
    }

    if (root->length != 32) {
        kprint("FAIL: Root inode size incorrect\n");
        // return;
    }

    // Now write to root inode to extend it
    // We are cheating a bit: treating directory as file we can write to.
    // Minix directories are just files.

    char *data = "This is a test write to extend the file size beyond 32 bytes.";
    size_t len = strlen(data); // ~60 bytes

    write_inode_occurred = false;

    // Write at offset 32
    if (root->write) {
        size_t written = root->write(root, 32, len, (uint8_t *)data);
        if (written != len) {
             kprint("FAIL: Write failed\n");
        } else {
             if (root->length == 32 + len) {
                 kprint("PASS: Inode length updated in memory\n");
             } else {
                 kprint("FAIL: Inode length not updated in memory\n");
             }

             if (write_inode_occurred) {
                 // Verify on disk
                 struct minix_inode_v1 *disk_inode = (struct minix_inode_v1 *)(mock_disk + 4 * 1024);
                 if (disk_inode->i_size == 32 + len) {
                     kprint("PASS: Inode written back to disk with correct size\n");
                 } else {
                     kprint("FAIL: Inode written back but size incorrect on disk\n");
                 }
             } else {
                 kprint("FAIL: Inode write-back did not occur\n");
             }
        }
    } else {
        kprint("FAIL: Root node has no write op\n");
    }

    if (root->unmount) root->unmount(root);
}
