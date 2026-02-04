# EXT2 Filesystem Specification

## Overview
EXT2 is the native filesystem for Substrate. It uses a block-based allocation scheme with block groups.

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

### `int ext2_add_entry(fs_node_t *dir, const char *name, uint32_t inode)`
Adds a directory entry to the specified directory.

### `int ext2_remove_entry(fs_node_t *dir, const char *name)`
Removes an entry from the directory.

## Concurrency
The driver is designed to be thread-safe for concurrent access to different files/inodes.
- **I/O Buffers:** Uses dynamic memory allocation (`kmalloc`) for block I/O buffers to prevent race conditions.
- **Directory Iteration:** `readdir` maintains state within the file node context (`ext2_node_t`), ensuring safe iteration across multiple open handles.

## Constraints
- Directory entry logic is currently stubbed.
- Support for linked-list directory structure.
- No support for directory indexing (HTree).
