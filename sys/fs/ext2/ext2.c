#include "ext2.h"
#include "../../drivers/video/vga.h" // Temporary include for debug printing

void ext2_init(void) {
    // Initialized by VFS or dev driver
}

uint32_t ext2_alloc_block(void) {
    // In a real driver, we would:
    // 1. Read block group descriptors.
    // 2. Find a group with free blocks.
    // 3. Read the block bitmap for that group.
    // 4. Find the first 0 bit.
    // 5. Set the bit and write back.
    // 6. Update counts in superblock and descriptor.
    return 0; // Stub
}

void ext2_free_block(uint32_t block) {
    (void)block;
    // Reverse of alloc
}

uint32_t ext2_alloc_inode(void) {
    // Similar to block allocation but for inodes
    return 0; // Stub
}

void ext2_free_inode(uint32_t inode) {
    (void)inode;
}

int ext2_add_entry(fs_node_t *dir, const char *name, uint32_t inode) {
    // 1. Read directory data blocks.
    // 2. Find empty space or end of block.
    // 3. Create ext2_dirent_t structure.
    // 4. Update parent directory inode (size).
    (void)dir; (void)name; (void)inode;
    return 0; // Stub
}

int ext2_remove_entry(fs_node_t *dir, const char *name) {
    // 1. Read directory data blocks.
    // 2. Search for entry by name.
    // 3. Update rec_len of previous entry to skip removed one.
    (void)dir; (void)name;
    return 0; // Stub
}


int ext2_readlink(fs_node_t *node, char *buf, size_t size) {
    (void)node; (void)buf; (void)size;
    // 1. If fast symlink (size < 60), data is in inode.i_block
    // 2. If slow symlink, read data block.
    return -1; // Stub
}

int ext2_symlink(fs_node_t *parent, const char *target, const char *name) {
    (void)parent; (void)target; (void)name;
    // 1. Alloc inode
    // 2. Write target path
    // 3. Add to parent dir
    return -1; // Stub
}

// TODO: Implement read, write, open, close, readdir, finddir for EXT2



