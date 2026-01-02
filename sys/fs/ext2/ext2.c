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

// TODO: Implement read, write, open, close, readdir, finddir for EXT2

