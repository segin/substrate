# EXT2 Filesystem Specification

## Overview
EXT2 is the native filesystem for TestUnix. It uses a block-based allocation scheme with block groups.

## Implementation
- **Superblock:** Contains global filesystem information (total blocks, inodes, etc.).
- **Block Group Descriptors:** Store pointers to bitmaps and inode tables for each group.
- **Bitmaps:** Used for tracking allocation state of blocks and inodes.
- **Allocation Logic:**
    - `ext2_alloc_block()`: Searches bitmaps for free blocks.
    - `ext2_alloc_inode()`: Searches bitmaps for free inodes.

## API
### `uint32_t ext2_alloc_block(void)`
Allocates a free block and returns its index.

### `uint32_t ext2_alloc_inode(void)`
Allocates a free inode and returns its index.

## Constraints
- Allocation logic is currently stubbed.
- Support for multiple block groups is planned.
- No journaling support (EXT3).
