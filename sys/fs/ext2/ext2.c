#include <fs/ext2/ext2.h>
#include <vfs/vfs.h>
#include <kern/console.h>
#include <kern/time.h>
#include <string.h>
#include <vm/vm_kmem.h>
#include <vm/uma.h>
#include <sys/errno.h>
#include <sys/stat.h>

// Static filesystem context (single mount for now)
static ext2_fs_t ext2_fs;
static ext2_group_desc_t ext2_bgd_table[64]; // Max 64 block groups
static ext2_node_t ext2_root_ctx;
static fs_node_t ext2_root;

// Cache for dynamically created nodes
#define EXT2_NODE_CACHE_SIZE 64
static ext2_node_t ext2_node_cache[EXT2_NODE_CACHE_SIZE];
static fs_node_t ext2_fs_node_cache[EXT2_NODE_CACHE_SIZE];
static int ext2_node_cache_idx = 0;

static uma_zone_t *ext2_block_cache;

static void ext2_node_open(fs_node_t *node) {
    if (!node) return;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx) return;
    ctx->pin_count++;
}

static void ext2_node_close(fs_node_t *node) {
    if (!node) return;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx) return;
    if (ctx->pin_count > 0) {
        ctx->pin_count--;
    }
}

// Forward declarations
fs_node_t *ext2_mount(const char *device, uint32_t flags, void *data);
size_t ext2_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
size_t ext2_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
struct dirent *ext2_readdir(fs_node_t *node, uint64_t index);
fs_node_t *ext2_finddir(fs_node_t *node, char *name);
int ext2_readlink(fs_node_t *node, char *buf, size_t size);
uint32_t ext2_alloc_block(ext2_fs_t *fs);
static int ext2_mknod(fs_node_t *dir, const char *name, uint16_t mode, uint32_t dev);
static int ext2_mkdir(fs_node_t *dir, const char *name, uint16_t permission);
static int ext2_symlink(fs_node_t *dir, const char *target, const char *name);
static int ext2_unlink(fs_node_t *dir, const char *name);
static int ext2_rmdir(fs_node_t *dir, const char *name);
static int ext2_dir_is_empty(fs_node_t *node);
static int ext2_add_entry(fs_node_t *dir, const char *name, uint32_t inode, uint8_t file_type);
static int ext2_remove_entry(fs_node_t *dir, const char *name);
static int ext2_flush_super(ext2_fs_t *fs);
static int ext2_flush_group_desc(ext2_fs_t *fs, uint32_t group);
static uint8_t ext2_dirent_type_from_mode(uint16_t mode);
static int ext2_free_indirect_tree(ext2_fs_t *fs, uint32_t block_num, uint32_t depth);
static int ext2_free_inode_blocks(ext2_fs_t *fs, ext2_inode_t *inode);

// Helper to find a zero bit in a bitmap range
int ext2_find_next_zero_bit(void *bitmap, uint32_t total_bits, uint32_t start, uint32_t end, uint32_t *found_idx) {
    uint32_t *bitmap32 = (uint32_t *)bitmap;
    uint32_t i = start;

    if (end > total_bits) end = total_bits;

    // Align to 32 bits
    while (i < end && (i & 31)) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;

        if (!(((uint8_t*)bitmap)[byte_idx] & (1 << bit_idx))) {
            *found_idx = i;
            return 1;
        }
        i++;
    }

    // Fast path: skip full words
    for (; i + 32 <= end; i += 32) {
        if (bitmap32[i / 32] != 0xFFFFFFFF) {
            break;
        }
    }

    // Slow path for remaining bits
    for (; i < end; i++) {
        uint32_t byte_idx = i / 8;
        uint32_t bit_idx = i % 8;

        if (!(((uint8_t*)bitmap)[byte_idx] & (1 << bit_idx))) {
            *found_idx = i;
            return 1;
        }
    }

    return 0;
}

// Read a block from the device
uint32_t ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer) {
    if (!fs || !fs->device || !fs->device->read) return 0;
    off_t offset = (off_t)block_num * fs->block_size;
    return fs->device->read(fs->device, offset, fs->block_size, buffer);
}

// Read multiple blocks from the device
uint32_t ext2_read_blocks(ext2_fs_t *fs, uint32_t block_num, uint32_t count, void *buffer) {
    if (!fs || !fs->device || !fs->device->read) return 0;
    off_t offset = (off_t)block_num * fs->block_size;
    return fs->device->read(fs->device, offset, fs->block_size * count, buffer);
}

// Read an inode
int ext2_read_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode) {
    if (!fs || inode_num == 0) return -1;
    
    // Calculate which block group the inode is in
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    if (group >= fs->group_count) return -1;
    
    // Get inode table location from block group descriptor
    uint32_t inode_table_block = fs->bgd[group].bg_inode_table;
    
    // Calculate which block and offset within the inode table
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t inode_offset = (index % inodes_per_block) * fs->inode_size;
    
    // Read the block containing the inode
    uint8_t *block_buf = uma_zalloc(ext2_block_cache, M_WAITOK);
    if (!block_buf) return -1;

    if (ext2_read_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        uma_zfree(ext2_block_cache, block_buf);
        return -1;
    }
    
    memcpy(inode, block_buf + inode_offset, sizeof(ext2_inode_t));
    uma_zfree(ext2_block_cache, block_buf);
    return 0;
}

// Write a block to the device
uint32_t ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buffer) {
    if (!fs || !fs->device || !fs->device->write) return 0;
    off_t offset = (off_t)block_num * fs->block_size;
    return fs->device->write(fs->device, offset, fs->block_size, (uint8_t *)buffer);
}

static int ext2_flush_super(ext2_fs_t *fs) {
    if (!fs || !fs->device || !fs->device->write) return -EIO;
    if (fs->device->write(fs->device, 1024, 1024, (uint8_t *)&fs->sb) != 1024) {
        return -EIO;
    }
    return 0;
}

static int ext2_flush_group_desc(ext2_fs_t *fs, uint32_t group) {
    uint32_t bgd_block;
    uint32_t desc_offset;
    uint32_t block_offset;
    uint8_t *block_buf;
    int ret = 0;

    if (!fs || group >= fs->group_count) return -EINVAL;

    bgd_block = (fs->block_size == 1024) ? 2 : 1;
    desc_offset = group * sizeof(ext2_group_desc_t);
    block_offset = desc_offset % fs->block_size;
    block_buf = uma_zalloc(ext2_block_cache, M_WAITOK);
    if (!block_buf) return -ENOMEM;

    if (ext2_read_block(fs, bgd_block + (desc_offset / fs->block_size), block_buf) != fs->block_size) {
        ret = -EIO;
        goto out;
    }

    memcpy(block_buf + block_offset, &fs->bgd[group], sizeof(ext2_group_desc_t));
    if (ext2_write_block(fs, bgd_block + (desc_offset / fs->block_size), block_buf) != fs->block_size) {
        ret = -EIO;
        goto out;
    }

out:
    uma_zfree(ext2_block_cache, block_buf);
    return ret;
}

// Write an inode back to disk
int ext2_write_inode(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode) {
    if (!fs || inode_num == 0) return -1;
    
    // Calculate which block group the inode is in
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    if (group >= fs->group_count) return -1;
    
    // Get inode table location from block group descriptor
    uint32_t inode_table_block = fs->bgd[group].bg_inode_table;
    
    // Calculate which block and offset within the inode table
    uint32_t inodes_per_block = fs->block_size / fs->inode_size;
    uint32_t block_offset = index / inodes_per_block;
    uint32_t inode_offset = (index % inodes_per_block) * fs->inode_size;
    
    // Read the block containing the inode
    uint8_t *block_buf = uma_zalloc(ext2_block_cache, M_WAITOK);
    if (!block_buf) return -1;

    if (ext2_read_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        uma_zfree(ext2_block_cache, block_buf);
        return -1;
    }

    // Update the inode data
    memcpy(block_buf + inode_offset, inode, sizeof(ext2_inode_t));
    
    // Write the block back to disk
    if (ext2_write_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        uma_zfree(ext2_block_cache, block_buf);
        return -1;
    }
    
    uma_zfree(ext2_block_cache, block_buf);
    return 0;
}

// Get block number for a given file block index (handles indirect blocks)
// Requires caller-provided scratch buffers (each size of block_size)
uint32_t ext2_get_block_num(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx,
                                   uint32_t *indirect_buf, uint32_t *dindirect_buf, uint32_t *tindirect_buf) {
    uint32_t ptrs_per_block = fs->block_size / 4;
    
    // Direct blocks (0-11)
    if (block_idx < 12) {
        return inode->i_block[block_idx];
    }
    block_idx -= 12;
    
    // Indirect block (12)
    if (block_idx < ptrs_per_block) {
        if (inode->i_block[12] == 0) return 0;
        ext2_read_block(fs, inode->i_block[12], (uint8_t *)indirect_buf);
        return indirect_buf[block_idx];
    }
    block_idx -= ptrs_per_block;
    
    // Double indirect block (13)
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[13] == 0) return 0;
        ext2_read_block(fs, inode->i_block[13], (uint8_t *)dindirect_buf);
        uint32_t indirect_idx = block_idx / ptrs_per_block;
        uint32_t direct_idx = block_idx % ptrs_per_block;
        uint32_t indirect_block = dindirect_buf[indirect_idx];

        if (indirect_block == 0) return 0;

        ext2_read_block(fs, indirect_block, (uint8_t *)indirect_buf);
        return indirect_buf[direct_idx];
    }
    block_idx -= ptrs_per_block * ptrs_per_block;
    
    // Triple indirect block (14)
    if (block_idx < ptrs_per_block * ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[14] == 0) return 0;
        ext2_read_block(fs, inode->i_block[14], (uint8_t *)tindirect_buf);
        
        uint32_t tindirect_idx = block_idx / (ptrs_per_block * ptrs_per_block);
        uint32_t remaining = block_idx % (ptrs_per_block * ptrs_per_block);
        uint32_t dindirect_idx = remaining / ptrs_per_block;
        uint32_t indirect_idx = remaining % ptrs_per_block;
        
        uint32_t dindirect_block = tindirect_buf[tindirect_idx];
        if (dindirect_block == 0) return 0;

        ext2_read_block(fs, dindirect_block, (uint8_t *)dindirect_buf);
        uint32_t indirect_block = dindirect_buf[dindirect_idx];
        if (indirect_block == 0) return 0;

        ext2_read_block(fs, indirect_block, (uint8_t *)indirect_buf);
        return indirect_buf[indirect_idx];
    }
    
    return 0;
}

// Get contiguous extent of blocks
void ext2_get_blocks_extent(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx, uint32_t max_count, 
                                   uint32_t *phys_block, uint32_t *count,
                                   uint32_t *indirect_buf, uint32_t *dindirect_buf, uint32_t *tindirect_buf) {
    *count = 0;
    *phys_block = 0;
    if (max_count == 0) return;

    *phys_block = ext2_get_block_num(fs, inode, block_idx, indirect_buf, dindirect_buf, tindirect_buf);
    *count = 1;
    
    if (*phys_block == 0) {
        // Find sparse run
        for (uint32_t i = 1; i < max_count; i++) {
            if (ext2_get_block_num(fs, inode, block_idx + i, indirect_buf, dindirect_buf, tindirect_buf) != 0) break;
            (*count)++;
        }
        return;
    }

    // Find contiguous run
    for (uint32_t i = 1; i < max_count; i++) {
        uint32_t next = ext2_get_block_num(fs, inode, block_idx + i, indirect_buf, dindirect_buf, tindirect_buf);
        if (next == *phys_block + i) {
            (*count)++;
        } else {
            break;
        }
    }
}

// Read data from an inode at a given offset
uint32_t ext2_inode_read(ext2_node_t *node, off_t offset, uint32_t size, void *buffer) {
    ext2_fs_t *fs = node->fs;
    ext2_inode_t *inode = &node->inode;

    if (offset >= inode->i_size) return 0;
    if (offset + size > inode->i_size) size = inode->i_size - offset;
    
    mutex_lock(&node->lock);

    // Lazy allocate scratch buffers
    uint32_t block_size = fs->block_size;
    if (!node->block_buf) node->block_buf = kmalloc(block_size);
    if (!node->indirect_buf) node->indirect_buf = kmalloc(block_size);
    if (!node->dindirect_buf) node->dindirect_buf = kmalloc(block_size);
    if (!node->tindirect_buf) node->tindirect_buf = kmalloc(block_size);

    if (!node->block_buf || !node->indirect_buf || !node->dindirect_buf || !node->tindirect_buf) {
        mutex_unlock(&node->lock);
        return 0;
    }

    uint8_t *block_buf = node->block_buf;
    uint32_t *indirect = node->indirect_buf;
    uint32_t *dindirect = node->dindirect_buf;
    uint32_t *tindirect = node->tindirect_buf;

    uint8_t *buf = (uint8_t *)buffer;
    uint32_t total_read = 0;
    
    while (size > 0) {
        uint32_t block_idx = (uint32_t)(offset / fs->block_size);
        uint32_t block_offset = (uint32_t)(offset % fs->block_size);
        
        uint32_t phys_block;
        uint32_t contiguous_blocks;
        uint32_t needed_blocks = (size + block_offset + fs->block_size - 1) / fs->block_size;
        if (needed_blocks > 1024) needed_blocks = 1024;
        
        ext2_get_blocks_extent(fs, inode, block_idx, needed_blocks, &phys_block, &contiguous_blocks, indirect, dindirect, tindirect);
        
        if (contiguous_blocks == 0) break; // Should not happen

        // 1. Handle Unaligned Head
        if (block_offset > 0) {
            if (phys_block == 0) memset(block_buf, 0, fs->block_size);
            else ext2_read_block(fs, phys_block, block_buf);

            uint32_t copy = fs->block_size - block_offset;
            if (copy > size) copy = size;
            memcpy(buf, block_buf + block_offset, copy);

            buf += copy; offset += copy; size -= copy;
            total_read += copy;

            if (phys_block != 0) phys_block++;
            contiguous_blocks--;
            block_offset = 0;
            if (size == 0 || contiguous_blocks == 0) continue;
        }

        // 2. Handle Aligned Body
        if (contiguous_blocks > 0 && size >= fs->block_size) {
            uint32_t full_blocks = size / fs->block_size;
            if (full_blocks > contiguous_blocks) full_blocks = contiguous_blocks;

            if (phys_block == 0) {
                memset(buf, 0, full_blocks * fs->block_size);
            } else {
                ext2_read_blocks(fs, phys_block, full_blocks, buf);
            }

            uint32_t bytes = full_blocks * fs->block_size;
            buf += bytes; offset += bytes; size -= bytes;
            total_read += bytes;

            if (phys_block != 0) phys_block += full_blocks;
            contiguous_blocks -= full_blocks;
        }

        // 3. Handle Tail
        if (contiguous_blocks > 0 && size > 0) {
            if (phys_block == 0) memset(block_buf, 0, fs->block_size);
            else ext2_read_block(fs, phys_block, block_buf);

            memcpy(buf, block_buf, size);
            buf += size; offset += size; 
            total_read += size;
            size = 0;
        }
    }
    
    mutex_unlock(&node->lock);
    return total_read;
}

// Allocate and add a block to an inode
int ext2_alloc_inode_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx, uint32_t *indirect_buf) {
    uint32_t ptrs_per_block = fs->block_size / 4;
    
    // Direct blocks (0-11)
    if (block_idx < 12) {
        uint32_t new_block = ext2_alloc_block(fs);
        if (new_block == 0) return -1;
        inode->i_block[block_idx] = new_block;
        return 0;
    }
    block_idx -= 12;
    
    // Indirect block (12)
    if (block_idx < ptrs_per_block) {
        // Allocate indirect block if not present
        if (inode->i_block[12] == 0) {
            uint32_t new_indirect = ext2_alloc_block(fs);
            if (new_indirect == 0) return -1;
            inode->i_block[12] = new_indirect;

            uint32_t *temp_indirect_buf = kmalloc(fs->block_size);
            if (!temp_indirect_buf) return -1;
            memset(temp_indirect_buf, 0, fs->block_size);
            ext2_write_block(fs, new_indirect, temp_indirect_buf);
            kfree(temp_indirect_buf, fs->block_size);
        }
        
        // Allocate data block
        ext2_read_block(fs, inode->i_block[12], indirect_buf);
        uint32_t new_block = ext2_alloc_block(fs);
        if (new_block == 0) {
            return -1;
        }
        indirect_buf[block_idx] = new_block;
        ext2_write_block(fs, inode->i_block[12], indirect_buf);
        return 0;
    }
    
    // Double indirect and beyond not implemented
    return -1;
}

// Write data to an inode at a given offset
uint32_t ext2_inode_write(ext2_node_t *node, off_t offset, uint32_t size, const void *buffer) {
    ext2_fs_t *fs = node->fs;
    ext2_inode_t *inode = &node->inode;

    mutex_lock(&node->lock);

    // Lazy allocate
    uint32_t block_size = fs->block_size;
    if (!node->block_buf) node->block_buf = kmalloc(block_size);
    if (!node->indirect_buf) node->indirect_buf = kmalloc(block_size);
    if (!node->dindirect_buf) node->dindirect_buf = kmalloc(block_size);
    if (!node->tindirect_buf) node->tindirect_buf = kmalloc(block_size);

    if (!node->block_buf || !node->indirect_buf || !node->dindirect_buf || !node->tindirect_buf) {
        mutex_unlock(&node->lock);
        return 0;
    }

    uint8_t *block_buf = node->block_buf;
    uint32_t *indirect = node->indirect_buf;
    uint32_t *dindirect = node->dindirect_buf;
    uint32_t *tindirect = node->tindirect_buf;

    const uint8_t *buf = (const uint8_t *)buffer;
    uint32_t total_written = 0;
    
    while (size > 0) {
        uint32_t block_idx = (uint32_t)(offset / fs->block_size);
        uint32_t block_offset = (uint32_t)(offset % fs->block_size);
        uint32_t block_num = ext2_get_block_num(fs, inode, block_idx, indirect, dindirect, tindirect);
        
        // Allocate block if it doesn't exist
        if (block_num == 0) {
            if (ext2_alloc_inode_block(fs, inode, block_idx, indirect) != 0) {
                // Out of space
                break;
            }
            block_num = ext2_get_block_num(fs, inode, block_idx, indirect, dindirect, tindirect);
            if (block_num == 0) break;
            
            // Zero the newly allocated block
            memset(block_buf, 0, fs->block_size);
            ext2_write_block(fs, block_num, block_buf);
        }
        
        // Read block if we're doing a partial write
        if (block_offset != 0 || size < fs->block_size) {
            ext2_read_block(fs, block_num, block_buf);
        }
        
        uint32_t to_copy = fs->block_size - block_offset;
        if (to_copy > size) to_copy = size;
        
        memcpy(block_buf + block_offset, buf, to_copy);
        ext2_write_block(fs, block_num, block_buf);
        
        buf += to_copy;
        total_written += to_copy;
        offset += to_copy;
        size -= to_copy;
    }

    if (offset > inode->i_size) {
        inode->i_size = offset;
    }

    /* Update modification and change times */
    time_t now = get_time();
    inode->i_mtime = (uint32_t)now;
    inode->i_ctime = (uint32_t)now;
    
    mutex_unlock(&node->lock);
    return total_written;
}

// Allocate a node from the cache
fs_node_t *ext2_alloc_node(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode) {
    int idx = -1;
    int start = ext2_node_cache_idx % EXT2_NODE_CACHE_SIZE;
    for (int i = 0; i < EXT2_NODE_CACHE_SIZE; i++) {
        int probe = (start + i) % EXT2_NODE_CACHE_SIZE;
        if (ext2_node_cache[probe].pin_count == 0) {
            idx = probe;
            ext2_node_cache_idx = (probe + 1) % EXT2_NODE_CACHE_SIZE;
            break;
        }
    }
    if (idx < 0) {
        return NULL;
    }
    
    ext2_node_t *ctx = &ext2_node_cache[idx];
    fs_node_t *node = &ext2_fs_node_cache[idx];
    
    ctx->fs = fs;
    ctx->inode_num = inode_num;
    memcpy(&ctx->inode, inode, sizeof(ext2_inode_t));
    
    // Initialize lock for scratch buffers
    mutex_init(&ctx->lock, "ext2_node");

    // Initialize readdir cache (use -1 to indicate uninitialized)
    ctx->last_readdir_idx = (uint64_t)-1;
    ctx->last_readdir_pos = 0;

    // Initialize dcache
    ctx->dcache_idx = 0;
    memset(ctx->dcache, 0, sizeof(ctx->dcache));

    uint32_t block_size = fs->block_size;

    if (ctx->block_buf) { kfree(ctx->block_buf, block_size); ctx->block_buf = NULL; }
    if (ctx->indirect_buf) { kfree(ctx->indirect_buf, block_size); ctx->indirect_buf = NULL; }
    if (ctx->dindirect_buf) { kfree(ctx->dindirect_buf, block_size); ctx->dindirect_buf = NULL; }
    if (ctx->tindirect_buf) { kfree(ctx->tindirect_buf, block_size); ctx->tindirect_buf = NULL; }

    memset(node, 0, sizeof(fs_node_t));
    node->inode = inode_num;
    node->length = inode->i_size;
    node->mask = inode->i_mode & 0xFFF;
    node->uid = inode->i_uid;
    node->gid = inode->i_gid;
    node->impl = (uintptr_t)ctx;
    node->open = ext2_node_open;
    node->close = ext2_node_close;
    ctx->cache_slot = (uint16_t)idx;
    ctx->pin_count = 0;
    
    // Set type and callbacks based on inode mode
    uint16_t type = inode->i_mode & 0xF000;
    if (type == EXT2_S_IFDIR) {
        node->flags = FS_DIRECTORY;
        node->readdir = ext2_readdir;
        node->finddir = ext2_finddir;
        node->mkdir = ext2_mkdir;
        node->mknod = ext2_mknod;
        node->unlink = ext2_unlink;
        node->rmdir = ext2_rmdir;
        node->symlink = ext2_symlink;
    } else if (type == EXT2_S_IFREG) {
        node->flags = FS_FILE;
        node->read = ext2_file_read;
        node->write = ext2_file_write;
        node->truncate = ext2_truncate;
    } else if (type == EXT2_S_IFLNK) {
        node->flags = FS_SYMLINK;
        node->readlink = ext2_readlink;
    } else if (type == EXT2_S_IFCHR) {
        node->flags = FS_CHARDEVICE;
        node->rdev = inode->i_block[0];
    } else if (type == EXT2_S_IFBLK) {
        node->flags = FS_BLOCKDEVICE;
        node->rdev = inode->i_block[0];
    }
    return node;
}

// Read symlink target
int ext2_readlink(fs_node_t *node, char *buf, size_t size) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_inode_t *inode = &ctx->inode;
    
    uint32_t link_size = inode->i_size;
    if (size < link_size + 1) link_size = size - 1;
    
    // Fast symlink: if size <= 60, target is stored in i_block[]
    if (inode->i_size <= 60) {
        memcpy(buf, (char *)inode->i_block, link_size);
    } else {
        // Slow symlink: target is in data blocks
        ext2_inode_read(ctx, 0, link_size, (uint8_t *)buf);
    }
    buf[link_size] = '\0';
    return link_size;
}

// File read operation
size_t ext2_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    return ext2_inode_read(ctx, offset, size, buffer);
}

// File write operation
size_t ext2_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_fs_t *fs = ctx->fs;
    
    uint32_t written = ext2_inode_write(ctx, offset, size, buffer);
    
    // Write updated inode back to disk
    if (written > 0) {
        ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
        node->length = ctx->inode.i_size; // Update VFS node size
    }
    
    return written;
}

// Read directory entry at index
struct dirent *ext2_readdir(fs_node_t *node, uint64_t index) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_fs_t *fs = ctx->fs;
    
    // Read the directory data
    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = 0;
    uint32_t cur_idx = 0;
    
    mutex_lock(&ctx->lock);

    // Check cache for sequential access optimization
    if (index > 0 && index == ctx->last_readdir_idx + 1) {
        pos = ctx->last_readdir_pos;
        cur_idx = ctx->last_readdir_idx + 1;
    }

    // Lazy allocate
    uint32_t block_size = fs->block_size;
    if (!ctx->block_buf) ctx->block_buf = kmalloc(block_size);
    if (!ctx->indirect_buf) ctx->indirect_buf = kmalloc(block_size);
    if (!ctx->dindirect_buf) ctx->dindirect_buf = kmalloc(block_size);
    if (!ctx->tindirect_buf) ctx->tindirect_buf = kmalloc(block_size);

    if (!ctx->block_buf || !ctx->indirect_buf || !ctx->dindirect_buf || !ctx->tindirect_buf) {
        mutex_unlock(&ctx->lock);
        return NULL;
    }

    uint8_t *ext2_dir_buf = ctx->block_buf;
    uint32_t *indirect = ctx->indirect_buf;
    uint32_t *dindirect = ctx->dindirect_buf;
    uint32_t *tindirect = ctx->tindirect_buf;

    struct dirent *result = NULL;

    while (pos < dir_size) {
        // Read block containing current position
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t block_num = ext2_get_block_num(fs, &ctx->inode, block_idx, indirect, dindirect, tindirect);
        
        if (block_num == 0) break;
        ext2_read_block(fs, block_num, ext2_dir_buf);
        
        // Parse entries in this block
        while (block_off < fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(ext2_dir_buf + block_off);
            
            if (de->inode != 0 && de->name_len > 0) {
                if (cur_idx == index) {
                    // Found it - store in context specific dirent
                    ctx->current_dirent.d_ino = de->inode;
                    uint32_t len = de->name_len;
                    // Fix: Ensure name fits in buffer to prevent overflow
                    if (len >= sizeof(ctx->current_dirent.d_name)) {
                        len = sizeof(ctx->current_dirent.d_name) - 1;
                    }
                    memcpy(ctx->current_dirent.d_name, de->name, len);
                    ctx->current_dirent.d_name[len] = '\0';
                    result = &ctx->current_dirent;

                    // Update cache
                    ctx->last_readdir_idx = index;
                    ctx->last_readdir_pos = pos + de->rec_len;

                    goto cleanup;
                }
                cur_idx++;
            }
            
            if (de->rec_len == 0) break; // Prevent infinite loop
            block_off += de->rec_len;
            pos += de->rec_len;
        }
    }
    
cleanup:
    mutex_unlock(&ctx->lock);
    return result;
}

// Find entry by name in directory
fs_node_t *ext2_finddir(fs_node_t *node, char *name) {
    if (!node || !name) return NULL;

    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_fs_t *fs = ctx->fs;
    
    size_t name_len = strlen(name);
    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = 0;
    
    mutex_lock(&ctx->lock);

    fs_node_t *result_node = NULL;

    // 1. Check dcache
    for (int k = 0; k < EXT2_DCACHE_SIZE; k++) {
        if (ctx->dcache[k].inode_num != 0 &&
            ctx->dcache[k].name_len == name_len &&
            memcmp(ctx->dcache[k].name, name, name_len) == 0) {

            ext2_inode_t inode;
            if (ext2_read_inode(fs, ctx->dcache[k].inode_num, &inode) == 0) {
                result_node = ext2_alloc_node(fs, ctx->dcache[k].inode_num, &inode);
                // Copy name
                size_t len = name_len;
                if (len >= sizeof(result_node->name)) {
                    len = sizeof(result_node->name) - 1;
                }
                memcpy(result_node->name, name, len);
                result_node->name[len] = '\0';
                goto cleanup;
            }
        }
    }

    // Lazy allocate
    uint32_t block_size = fs->block_size;
    if (!ctx->block_buf) ctx->block_buf = kmalloc(block_size);
    if (!ctx->indirect_buf) ctx->indirect_buf = kmalloc(block_size);
    if (!ctx->dindirect_buf) ctx->dindirect_buf = kmalloc(block_size);
    if (!ctx->tindirect_buf) ctx->tindirect_buf = kmalloc(block_size);

    if (!ctx->block_buf || !ctx->indirect_buf || !ctx->dindirect_buf || !ctx->tindirect_buf) {
        mutex_unlock(&ctx->lock);
        return NULL;
    }

    uint8_t *ext2_dir_buf = ctx->block_buf;
    uint32_t *indirect = ctx->indirect_buf;
    uint32_t *dindirect = ctx->dindirect_buf;
    uint32_t *tindirect = ctx->tindirect_buf;

    while (pos < dir_size) {
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t block_num = ext2_get_block_num(fs, &ctx->inode, block_idx, indirect, dindirect, tindirect);
        
        if (block_num == 0) break;
        ext2_read_block(fs, block_num, ext2_dir_buf);
        
        while (block_off < fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(ext2_dir_buf + block_off);
            
            if (de->inode != 0 && de->name_len > 0) {
                // Compare names
                if (de->name_len == name_len &&
                    memcmp(de->name, name, de->name_len) == 0) {
                    // Found it - read the inode and return a node
                    ext2_inode_t inode;
                    if (ext2_read_inode(fs, de->inode, &inode) == 0) {
                        result_node = ext2_alloc_node(fs, de->inode, &inode);
                        // Copy name
                        uint32_t len = de->name_len;
                        if (len >= sizeof(result_node->name)) {
                            len = sizeof(result_node->name) - 1;
                        }
                        memcpy(result_node->name, de->name, len);
                        result_node->name[len] = '\0';

                        // Add to dcache only if name fits
                        if (len < sizeof(ctx->dcache[0].name)) {
                            uint32_t idx = ctx->dcache_idx++ % EXT2_DCACHE_SIZE;
                            ctx->dcache[idx].inode_num = de->inode;
                            ctx->dcache[idx].name_len = len;
                            memcpy(ctx->dcache[idx].name, de->name, len);
                            ctx->dcache[idx].name[len] = '\0';
                        }

                        goto cleanup;
                    }
                }
            }
            
            if (de->rec_len == 0) break;
            block_off += de->rec_len;
            pos += de->rec_len;
        }
    }
    
cleanup:
    mutex_unlock(&ctx->lock);
    return result_node;
}

// Mount ext2 filesystem
fs_node_t *ext2_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags;
    
    fs_node_t *dev = (fs_node_t *)data;
    if (!dev || !dev->read) {
        kprint("EXT2: No device or read function\n");
        return NULL;
    }
    
    // Read superblock (at offset 1024)
    uint8_t sb_buf[1024];
    uint32_t read = dev->read(dev, 1024, 1024, sb_buf);
    if (read != 1024) {
        kprint("EXT2: Failed to read superblock\n");
        return NULL;
    }
    
    memcpy(&ext2_fs.sb, sb_buf, sizeof(ext2_superblock_t));
    
    if (ext2_fs.sb.s_magic != EXT2_SUPER_MAGIC) {
        kprint("EXT2: Invalid magic number\n");
        return NULL;
    }
    
    ext2_fs.device = dev;
    ext2_fs.block_size = 1024 << ext2_fs.sb.s_log_block_size;
    ext2_fs.inodes_per_group = ext2_fs.sb.s_inodes_per_group;
    ext2_fs.blocks_per_group = ext2_fs.sb.s_blocks_per_group;
    ext2_fs.group_count = (ext2_fs.sb.s_blocks_count + ext2_fs.blocks_per_group - 1) / ext2_fs.blocks_per_group;
    ext2_fs.inode_size = (ext2_fs.sb.s_rev_level >= 1) ? ext2_fs.sb.s_inode_size : EXT2_GOOD_OLD_INODE_SIZE;
    ext2_fs.bgd = ext2_bgd_table;
    ext2_fs.last_alloc_group = 0;
    ext2_fs.last_alloc_bit = 0;
    
    // Read block group descriptor table (starts at block 2 for 1K blocks, block 1 for larger)
    uint32_t bgd_block = (ext2_fs.block_size == 1024) ? 2 : 1;
    uint32_t bgd_size = ext2_fs.group_count * sizeof(ext2_group_desc_t);
    
    // Read enough blocks for the BGD table
    uint32_t bgd_blocks = (bgd_size + ext2_fs.block_size - 1) / ext2_fs.block_size;
    for (uint32_t i = 0; i < bgd_blocks && i < 2; i++) {
        ext2_read_block(&ext2_fs, bgd_block + i, 
                       ((uint8_t *)ext2_bgd_table) + i * ext2_fs.block_size);
    }
    
    // Read root inode (inode 2)
    ext2_inode_t root_inode;
    if (ext2_read_inode(&ext2_fs, EXT2_ROOT_INO, &root_inode) != 0) {
        kprint("EXT2: Failed to read root inode\n");
        return NULL;
    }
    
    // Initialize optimization hints
    ext2_fs.last_alloc_group = 0;
    ext2_fs.last_alloc_bit = 0;

    // Initialize active block bitmap cache
    ext2_fs.active_bg_group = (uint32_t)-1;
    if (!ext2_fs.active_bg_bitmap) {
        ext2_fs.active_bg_bitmap = uma_zalloc(ext2_block_cache, M_WAITOK);
    }

    // Initialize active inode bitmap cache
    ext2_fs.active_inode_bg_group = (uint32_t)-1;
    if (!ext2_fs.active_inode_bg_bitmap) {
        ext2_fs.active_inode_bg_bitmap = uma_zalloc(ext2_block_cache, M_WAITOK);
    }

    // Setup root node
    ext2_root_ctx.fs = &ext2_fs;
    ext2_root_ctx.inode_num = EXT2_ROOT_INO;
    memcpy(&ext2_root_ctx.inode, &root_inode, sizeof(ext2_inode_t));
    ext2_root_ctx.cache_slot = 0xFFFF;
    ext2_root_ctx.pin_count = 0;
    
    memset(&ext2_root, 0, sizeof(fs_node_t));
    strncpy(ext2_root.name, "/", sizeof(ext2_root.name) - 1);
    ext2_root.name[sizeof(ext2_root.name) - 1] = '\0';
    ext2_root.flags = FS_DIRECTORY;
    ext2_root.inode = EXT2_ROOT_INO;
    ext2_root.length = root_inode.i_size;
    ext2_root.mask = root_inode.i_mode & 0x0FFF;
    ext2_root.uid = root_inode.i_uid;
    ext2_root.gid = root_inode.i_gid;
    ext2_root.impl = (uintptr_t)&ext2_root_ctx;
    ext2_root.readdir = ext2_readdir;
    ext2_root.finddir = ext2_finddir;
    ext2_root.mkdir = ext2_mkdir;
    ext2_root.mknod = ext2_mknod;
    ext2_root.unlink = ext2_unlink;
    ext2_root.rmdir = ext2_rmdir;
    ext2_root.symlink = ext2_symlink;
    ext2_root.open = ext2_node_open;
    ext2_root.close = ext2_node_close;
    
    kprint("EXT2: Mounted successfully\n");
    return &ext2_root;
}

static filesystem_t ext2_filesystem = {
    .name = "ext2",
    .mount = ext2_mount,
};

void ext2_init(void) {
    kprint("Initializing EXT2 Driver...\n");
    ext2_block_cache = uma_zcreate("ext2-block", 4096, NULL, NULL, NULL, NULL, 0, 0);
    vfs_register_filesystem(&ext2_filesystem);
}

// Allocate a block from the filesystem
uint32_t ext2_alloc_block(ext2_fs_t *fs) {
    if (!fs) return 0;
    
    // Search each block group for a free block, starting from last allocation
    uint32_t start_group = fs->last_alloc_group;
    uint32_t group = start_group;

    do {
        if (fs->bgd[group].bg_free_blocks_count == 0) {
            group = (group + 1) % fs->group_count;
            continue;
        }
        
        // Read the block bitmap if it's not cached
        if (fs->active_bg_group != group) {
            fs->active_bg_group = (uint32_t)-1;
            if (ext2_read_block(fs, fs->bgd[group].bg_block_bitmap, fs->active_bg_bitmap) != fs->block_size) {
                group = (group + 1) % fs->group_count;
                continue;
            }
            fs->active_bg_group = group;
        }
        
        uint8_t *bitmap_buf = fs->active_bg_bitmap;
        uint32_t bits_in_group = fs->blocks_per_group;
        uint32_t start_bit = (group == fs->last_alloc_group) ? fs->last_alloc_bit : 0;
        uint32_t found_idx = 0;
        int found = 0;

        // Pass 1: Search from start_bit to end
        if (ext2_find_next_zero_bit(bitmap_buf, bits_in_group, start_bit, bits_in_group, &found_idx)) {
            found = 1;
        }
        // Pass 2: Search from 0 to start_bit (wrap around within group)
        else if (start_bit > 0 && ext2_find_next_zero_bit(bitmap_buf, bits_in_group, 0, start_bit, &found_idx)) {
            found = 1;
        }

        if (found) {
            uint32_t byte_idx = found_idx / 8;
            uint32_t bit_idx = found_idx % 8;
            
            // Mark as used
            bitmap_buf[byte_idx] |= (1 << bit_idx);

            // Write bitmap back
            ext2_write_block(fs, fs->bgd[group].bg_block_bitmap, bitmap_buf);

            // Update block group descriptor
            fs->bgd[group].bg_free_blocks_count--;

            // Calculate absolute block number
            uint32_t block_num = group * fs->blocks_per_group + found_idx + fs->sb.s_first_data_block;

            // Update superblock free blocks count
            fs->sb.s_free_blocks_count--;
            ext2_flush_group_desc(fs, group);
            ext2_flush_super(fs);

            // Update hints
            fs->last_alloc_group = group;
            fs->last_alloc_bit = (found_idx + 1) % bits_in_group;

            return block_num;
        }

        group = (group + 1) % fs->group_count;
    } while (group != start_group);
    
    return 0; // No free blocks
}

// Free a block
void ext2_free_block(ext2_fs_t *fs, uint32_t block_num) {
    if (!fs || block_num == 0) return;
    
    // Calculate which group this block belongs to
    uint32_t group = (block_num - fs->sb.s_first_data_block) / fs->blocks_per_group;
    uint32_t index = (block_num - fs->sb.s_first_data_block) % fs->blocks_per_group;
    
    if (group >= fs->group_count) return;
    
    // Read the block bitmap if it's not cached
    if (fs->active_bg_group != group) {
        fs->active_bg_group = (uint32_t)-1;
        if (ext2_read_block(fs, fs->bgd[group].bg_block_bitmap, fs->active_bg_bitmap) != fs->block_size) {
            return;
        }
        fs->active_bg_group = group;
    }
    
    uint8_t *bitmap_buf = fs->active_bg_bitmap;

    uint32_t byte_idx = index / 8;
    uint32_t bit_idx = index % 8;
    
    // Clear the bit
    bitmap_buf[byte_idx] &= ~(1 << bit_idx);
    
    // Write bitmap back
    ext2_write_block(fs, fs->bgd[group].bg_block_bitmap, bitmap_buf);
    
    // Update block group descriptor
    fs->bgd[group].bg_free_blocks_count++;
    
    // Update superblock
    fs->sb.s_free_blocks_count++;
    ext2_flush_group_desc(fs, group);
    ext2_flush_super(fs);
}

// Allocate an inode
uint32_t ext2_alloc_inode(ext2_fs_t *fs, int is_dir) {
    if (!fs) return 0;
    
    // Search each block group for a free inode
    for (uint32_t group = 0; group < fs->group_count; group++) {
        if (fs->bgd[group].bg_free_inodes_count == 0) continue;
        
        // Read the inode bitmap if it's not cached
        if (fs->active_inode_bg_group != group) {
            fs->active_inode_bg_group = (uint32_t)-1;
            if (ext2_read_block(fs, fs->bgd[group].bg_inode_bitmap, fs->active_inode_bg_bitmap) != fs->block_size) {
                continue;
            }
            fs->active_inode_bg_group = group;
        }
        
        uint8_t *bitmap_buf = fs->active_inode_bg_bitmap;

        // Find the first free bit (skip reserved inodes in first group)
        uint32_t start = (group == 0) ? fs->sb.s_first_ino : 0;
        uint32_t bits_in_group = fs->inodes_per_group;
        
        for (uint32_t i = start; i < bits_in_group; i++) {
            // Optimization: Skip full 32-bit words
            if ((i % 32 == 0) && (i + 32 <= bits_in_group)) {
                if (*(uint32_t *)&bitmap_buf[i / 8] == 0xFFFFFFFF) {
                    i += 31;
                    continue;
                }
            }
            // Optimization: Skip full bytes
            else if ((i % 8 == 0) && (i + 8 <= bits_in_group)) {
                if (bitmap_buf[i / 8] == 0xFF) {
                    i += 7;
                    continue;
                }
            }

            uint32_t byte_idx = i / 8;
            uint32_t bit_idx = i % 8;
            
            if (!(bitmap_buf[byte_idx] & (1 << bit_idx))) {
                // Found free inode - allocate it
                bitmap_buf[byte_idx] |= (1 << bit_idx);
                
                // Write bitmap back
                ext2_write_block(fs, fs->bgd[group].bg_inode_bitmap, bitmap_buf);
                
                // Update block group descriptor
                fs->bgd[group].bg_free_inodes_count--;
                if (is_dir) {
                    fs->bgd[group].bg_used_dirs_count++;
                }
                
                // Calculate absolute inode number
                uint32_t inode_num = group * fs->inodes_per_group + i + 1;
                
                // Update superblock
                fs->sb.s_free_inodes_count--;
                ext2_flush_group_desc(fs, group);
                ext2_flush_super(fs);
                
                // Initialize the inode
                ext2_inode_t inode;
                memset(&inode, 0, sizeof(ext2_inode_t));
                uint32_t now = (uint32_t)get_time();
                inode.i_ctime = now;
                inode.i_mtime = now;
                inode.i_atime = now;
                
                ext2_write_inode(fs, inode_num, &inode);
                
                return inode_num;
            }
        }
    }
    
    return 0; // No free inodes
}

// Free an inode
void ext2_free_inode(ext2_fs_t *fs, uint32_t inode_num, int was_dir) {
    if (!fs || inode_num == 0) return;
    
    // Calculate which group this inode belongs to
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    if (group >= fs->group_count) return;
    
    // Read the inode bitmap if it's not cached
    if (fs->active_inode_bg_group != group) {
        fs->active_inode_bg_group = (uint32_t)-1;
        if (ext2_read_block(fs, fs->bgd[group].bg_inode_bitmap, fs->active_inode_bg_bitmap) != fs->block_size) {
            return;
        }
        fs->active_inode_bg_group = group;
    }
    
    uint8_t *bitmap_buf = fs->active_inode_bg_bitmap;

    uint32_t byte_idx = index / 8;
    uint32_t bit_idx = index % 8;
    
    // Clear the bit
    bitmap_buf[byte_idx] &= ~(1 << bit_idx);
    
    // Write bitmap back
    ext2_write_block(fs, fs->bgd[group].bg_inode_bitmap, bitmap_buf);
    
    // Update block group descriptor
    fs->bgd[group].bg_free_inodes_count++;
    if (was_dir) {
        fs->bgd[group].bg_used_dirs_count--;
    }
    
    // Update superblock
    fs->sb.s_free_inodes_count++;
    ext2_flush_group_desc(fs, group);
    ext2_flush_super(fs);
}

static uint8_t ext2_dirent_type_from_mode(uint16_t mode) {
    switch (mode & S_IFMT) {
        case S_IFREG: return EXT2_FT_REG_FILE;
        case S_IFDIR: return EXT2_FT_DIR;
        case S_IFCHR: return EXT2_FT_CHRDEV;
        case S_IFBLK: return EXT2_FT_BLKDEV;
        case S_IFIFO: return EXT2_FT_FIFO;
        case S_IFLNK: return EXT2_FT_SYMLINK;
        default: return EXT2_FT_UNKNOWN;
    }
}

static int ext2_free_indirect_tree(ext2_fs_t *fs, uint32_t block_num, uint32_t depth) {
    uint32_t entries_per_block;
    uint32_t *block_buf;

    if (!fs || block_num == 0) return 0;
    if (depth == 0) {
        ext2_free_block(fs, block_num);
        return 0;
    }

    entries_per_block = fs->block_size / sizeof(uint32_t);
    block_buf = uma_zalloc(ext2_block_cache, M_WAITOK);
    if (!block_buf) return -ENOMEM;

    if (ext2_read_block(fs, block_num, block_buf) != fs->block_size) {
        uma_zfree(ext2_block_cache, block_buf);
        return -EIO;
    }

    for (uint32_t i = 0; i < entries_per_block; i++) {
        if (block_buf[i] != 0) {
            int ret = ext2_free_indirect_tree(fs, block_buf[i], depth - 1);
            if (ret != 0) {
                uma_zfree(ext2_block_cache, block_buf);
                return ret;
            }
        }
    }

    uma_zfree(ext2_block_cache, block_buf);
    ext2_free_block(fs, block_num);
    return 0;
}

static int ext2_free_inode_blocks(ext2_fs_t *fs, ext2_inode_t *inode) {
    if (!fs || !inode) return -EINVAL;

    for (uint32_t i = 0; i < 12; i++) {
        if (inode->i_block[i] != 0) {
            ext2_free_block(fs, inode->i_block[i]);
            inode->i_block[i] = 0;
        }
    }
    if (inode->i_block[12] != 0) {
        int ret = ext2_free_indirect_tree(fs, inode->i_block[12], 1);
        if (ret != 0) return ret;
        inode->i_block[12] = 0;
    }
    if (inode->i_block[13] != 0) {
        int ret = ext2_free_indirect_tree(fs, inode->i_block[13], 2);
        if (ret != 0) return ret;
        inode->i_block[13] = 0;
    }
    if (inode->i_block[14] != 0) {
        int ret = ext2_free_indirect_tree(fs, inode->i_block[14], 3);
        if (ret != 0) return ret;
        inode->i_block[14] = 0;
    }

    inode->i_blocks = 0;
    inode->i_size = 0;
    return 0;
}

int ext2_truncate(fs_node_t *node, off_t length) {
    ext2_node_t *ctx;
    ext2_fs_t *fs;

    if (!node) return -EINVAL;
    if (length < 0) return -EINVAL;
    ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx) return -EINVAL;
    fs = ctx->fs;

    if (length == (off_t)ctx->inode.i_size) {
        return 0;
    }
    if (length != 0) {
        return -EOPNOTSUPP;
    }

    mutex_lock(&ctx->lock);
    int ret = ext2_free_inode_blocks(fs, &ctx->inode);
    if (ret == 0) {
        uint32_t now = (uint32_t)get_time();
        ctx->inode.i_mtime = now;
        ctx->inode.i_ctime = now;
        node->length = 0;
        ret = ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
        if (ret != 0) {
            ret = -EIO;
        }
    }
    mutex_unlock(&ctx->lock);
    return ret;
}

// Add directory entry
static int ext2_add_entry(fs_node_t *dir, const char *name, uint32_t inode, uint8_t file_type) {
    if (!dir || !name || inode == 0) return -1;
    
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    ext2_fs_t *fs = ctx->fs;
    
    uint32_t name_len = strlen(name);
    if (name_len > 255) return -1;
    
    // Calculate required entry size (aligned to 4 bytes)
    uint32_t required_size = ((8 + name_len + 3) / 4) * 4;
    
    mutex_lock(&ctx->lock);

    // Invalidate dcache entry if it matches
    for (int k = 0; k < EXT2_DCACHE_SIZE; k++) {
        if (ctx->dcache[k].inode_num != 0 &&
            ctx->dcache[k].name_len == name_len &&
            memcmp(ctx->dcache[k].name, name, name_len) == 0) {
            ctx->dcache[k].inode_num = 0;
            // No break, clear all possible duplicates just in case
        }
    }

    // Lazy allocate
    uint32_t block_size = fs->block_size;
    if (!ctx->block_buf) ctx->block_buf = kmalloc(block_size);
    if (!ctx->indirect_buf) ctx->indirect_buf = kmalloc(block_size);
    if (!ctx->dindirect_buf) ctx->dindirect_buf = kmalloc(block_size);
    if (!ctx->tindirect_buf) ctx->tindirect_buf = kmalloc(block_size);

    if (!ctx->block_buf || !ctx->indirect_buf || !ctx->dindirect_buf || !ctx->tindirect_buf) {
        mutex_unlock(&ctx->lock);
        return -1;
    }

    uint8_t *block_buf = ctx->block_buf;
    uint32_t *indirect = ctx->indirect_buf;
    uint32_t *dindirect = ctx->dindirect_buf;
    uint32_t *tindirect = ctx->tindirect_buf;

    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = 0;
    int result = -1;
    
    // Search for space in existing blocks
    while (pos < dir_size) {
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t block_num = ext2_get_block_num(fs, &ctx->inode, block_idx, indirect, dindirect, tindirect);
        
        if (block_num == 0) break;
        ext2_read_block(fs, block_num, block_buf);
        
        while (block_off < fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + block_off);
            
            if (de->rec_len == 0) break;
            
            // Calculate actual size needed by this entry
            uint32_t actual_size = ((8 + de->name_len + 3) / 4) * 4;
            uint32_t slack = de->rec_len - actual_size;
            
            // Can we fit the new entry in the slack space?
            if (slack >= required_size && de->inode != 0) {
                // Split this entry
                de->rec_len = actual_size;
                
                ext2_dirent_t *new_de = (ext2_dirent_t *)(block_buf + block_off + actual_size);
                new_de->inode = inode;
                new_de->rec_len = slack;
                new_de->name_len = name_len;
                new_de->file_type = file_type;
                memcpy(new_de->name, name, name_len);
                
                ext2_write_block(fs, block_num, block_buf);
                result = 0;
                goto done;
            }
            
            // Can we reuse a deleted entry?
            if (de->inode == 0 && de->rec_len >= required_size) {
                de->inode = inode;
                de->name_len = name_len;
                de->file_type = file_type;
                memcpy(de->name, name, name_len);
                
                ext2_write_block(fs, block_num, block_buf);
                result = 0;
                goto done;
            }
            
            block_off += de->rec_len;
            pos += de->rec_len;
        }
    }
    
    // No space found - need to allocate a new block
    uint32_t new_block_idx = dir_size / fs->block_size;

    // Use ext2_alloc_inode_block to allocate and attach the block
    if (ext2_alloc_inode_block(fs, &ctx->inode, new_block_idx, indirect) != 0) {
        result = -1; // Out of space
        goto cleanup;
    }

    // Get the block number of the newly allocated block
    uint32_t new_block = ext2_get_block_num(fs, &ctx->inode, new_block_idx, indirect, dindirect, tindirect);
    if (new_block == 0) {
        result = -1;
        goto cleanup;
    }

    // Zero the new block
    memset(block_buf, 0, fs->block_size);
    
    // Create the new entry
    ext2_dirent_t *de = (ext2_dirent_t *)block_buf;
    de->inode = inode;
    de->rec_len = fs->block_size; // Entry spans entire block
    de->name_len = name_len;
    de->file_type = file_type;
    memcpy(de->name, name, name_len);
    
    ext2_write_block(fs, new_block, block_buf);
    
    // Update directory size and timestamps
    ctx->inode.i_size += fs->block_size;
    ctx->inode.i_blocks += (fs->block_size / 512);
    
    uint32_t now = (uint32_t)get_time();
    ctx->inode.i_mtime = now;
    ctx->inode.i_ctime = now;
    
    ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
    result = 0;
    
    // Invalidate readdir cache
    ctx->last_readdir_idx = (uint64_t)-1;
    ctx->last_readdir_pos = 0;
    goto cleanup;

done:
    /* Update parent dir timestamps for slot reuse/split paths */
    {
        uint32_t now = (uint32_t)get_time();
        ctx->inode.i_mtime = now;
        ctx->inode.i_ctime = now;
        ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
    }

cleanup:
    mutex_unlock(&ctx->lock);
    return result;
}

// Remove directory entry
static int ext2_remove_entry(fs_node_t *dir, const char *name) {
    if (!dir || !name) return -1;
    
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    ext2_fs_t *fs = ctx->fs;
    // Optimization: Calculate strlen once
    size_t name_len = strlen(name);

    mutex_lock(&ctx->lock);

    // Invalidate dcache entry if it matches
    for (int k = 0; k < EXT2_DCACHE_SIZE; k++) {
        if (ctx->dcache[k].inode_num != 0 &&
            ctx->dcache[k].name_len == name_len &&
            memcmp(ctx->dcache[k].name, name, name_len) == 0) {
            ctx->dcache[k].inode_num = 0;
            // No break, clear all possible duplicates just in case
        }
    }

    // Lazy allocate
    uint32_t block_size = fs->block_size;
    if (!ctx->block_buf) ctx->block_buf = kmalloc(block_size);
    if (!ctx->indirect_buf) ctx->indirect_buf = kmalloc(block_size);
    if (!ctx->dindirect_buf) ctx->dindirect_buf = kmalloc(block_size);
    if (!ctx->tindirect_buf) ctx->tindirect_buf = kmalloc(block_size);

    if (!ctx->block_buf || !ctx->indirect_buf || !ctx->dindirect_buf || !ctx->tindirect_buf) {
        mutex_unlock(&ctx->lock);
        return -1;
    }

    uint8_t *block_buf = ctx->block_buf;
    uint32_t *indirect = ctx->indirect_buf;
    uint32_t *dindirect = ctx->dindirect_buf;
    uint32_t *tindirect = ctx->tindirect_buf;

    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = 0;
    int result = -1;
    
    while (pos < dir_size) {
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t block_num = ext2_get_block_num(fs, &ctx->inode, block_idx, indirect, dindirect, tindirect);
        
        if (block_num == 0) break;
        ext2_read_block(fs, block_num, block_buf);
        
        ext2_dirent_t *prev_de = NULL;
        
        while (block_off < fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + block_off);
            
            if (de->rec_len == 0) break;
            
            // Is this the entry to remove?
            if (de->inode != 0 && de->name_len == name_len &&
                memcmp(de->name, name, de->name_len) == 0) {
                
                // Merge with previous entry if possible
                if (prev_de) {
                    prev_de->rec_len += de->rec_len;
                } else {
                    // First entry in block - just mark as deleted
                    de->inode = 0;
                }
                
                ext2_write_block(fs, block_num, block_buf);
                
                // Update directory mtime and ctime
                uint32_t now = (uint32_t)get_time();
                ctx->inode.i_mtime = now;
                ctx->inode.i_ctime = now;
                ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
                
                result = 0;

                // Invalidate readdir cache
                ctx->last_readdir_idx = (uint64_t)-1;
                ctx->last_readdir_pos = 0;

                goto cleanup;
            }
            
            prev_de = de;
            block_off += de->rec_len;
            pos += de->rec_len;
        }
    }
    
cleanup:
    mutex_unlock(&ctx->lock);
    return result;
}

static int ext2_mknod(fs_node_t *dir, const char *name, uint16_t mode, uint32_t dev) {
    ext2_node_t *dir_ctx;
    ext2_fs_t *fs;
    ext2_inode_t inode;
    uint32_t inode_num;
    uint16_t type;
    int is_dir = 0;

    if (!dir || !name || !name[0]) return -EINVAL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
    if (ext2_finddir(dir, (char *)name) != NULL) return -EEXIST;

    type = mode & S_IFMT;
    if (type == 0) {
        type = S_IFREG;
        mode |= S_IFREG;
    }
    if (type == S_IFDIR) return -EISDIR;

    dir_ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    fs = dir_ctx->fs;
    inode_num = ext2_alloc_inode(fs, is_dir);
    if (inode_num == 0) return -ENOSPC;

    memset(&inode, 0, sizeof(inode));
    inode.i_mode = mode;
    inode.i_uid = dir->uid;
    inode.i_gid = dir->gid;
    inode.i_links_count = 1;
    if (type == S_IFCHR || type == S_IFBLK) {
        inode.i_block[0] = dev;
    }
    uint32_t now = (uint32_t)get_time();
    inode.i_atime = now;
    inode.i_mtime = now;
    inode.i_ctime = now;

    if (ext2_write_inode(fs, inode_num, &inode) != 0) {
        ext2_free_inode(fs, inode_num, is_dir);
        return -EIO;
    }

    if (ext2_add_entry(dir, name, inode_num, ext2_dirent_type_from_mode(mode)) != 0) {
        ext2_free_inode(fs, inode_num, is_dir);
        return -EIO;
    }

    return 0;
}

static int ext2_symlink(fs_node_t *dir, const char *target, const char *name) {
    if (!dir || !target || !name || !name[0]) return -EINVAL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
    if (ext2_finddir(dir, (char *)name) != NULL) return -EEXIST;

    ext2_node_t *dir_ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    ext2_fs_t *fs = dir_ctx->fs;
    uint32_t target_len = strlen(target);

    uint32_t inode_num = ext2_alloc_inode(fs, 0);
    if (inode_num == 0) return -ENOSPC;

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = EXT2_S_IFLNK | 0777;
    inode.i_uid = 0;
    inode.i_gid = 0;
    inode.i_links_count = 1;
    inode.i_size = target_len;
    uint32_t now = (uint32_t)get_time();
    inode.i_atime = now;
    inode.i_mtime = now;
    inode.i_ctime = now;

    if (target_len <= 60) {
        /* Fast symlink: target stored inline in i_block[] */
        memcpy(inode.i_block, target, target_len);
        inode.i_blocks = 0;
    }

    if (ext2_write_inode(fs, inode_num, &inode) != 0) {
        ext2_free_inode(fs, inode_num, 0);
        return -EIO;
    }

    if (target_len > 60) {
        /* Slow symlink: allocate a data block and write target */
        fs_node_t *lnode = ext2_alloc_node(fs, inode_num, &inode);
        if (!lnode) {
            ext2_free_inode(fs, inode_num, 0);
            return -EIO;
        }
        ext2_node_t *lctx = (ext2_node_t *)(uintptr_t)lnode->impl;
        uint32_t written = ext2_inode_write(lctx, 0, target_len, target);
        lctx->inode.i_size = target_len;
        ext2_write_inode(fs, inode_num, &lctx->inode);
        if (written < target_len) {
            ext2_free_inode(fs, inode_num, 0);
            return -EIO;
        }
        close_fs(lnode);
    }

    if (ext2_add_entry(dir, name, inode_num, EXT2_FT_SYMLINK) != 0) {
        ext2_free_inode(fs, inode_num, 0);
        return -EIO;
    }

    return 0;
}

static int ext2_mkdir(fs_node_t *dir, const char *name, uint16_t permission) {
    ext2_node_t *dir_ctx;
    ext2_fs_t *fs;
    ext2_inode_t inode;
    uint32_t inode_num;
    uint32_t block_num;
    uint8_t *block_buf = NULL;
    uint32_t now;
    uint16_t dot_len;

    if (!dir || !name || !name[0]) return -EINVAL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
    if (ext2_finddir(dir, (char *)name) != NULL) return -EEXIST;

    dir_ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    fs = dir_ctx->fs;
    inode_num = ext2_alloc_inode(fs, 1);
    if (inode_num == 0) return -ENOSPC;

    block_num = ext2_alloc_block(fs);
    if (block_num == 0) {
        ext2_free_inode(fs, inode_num, 1);
        return -ENOSPC;
    }

    block_buf = kmalloc(fs->block_size);
    if (!block_buf) {
        ext2_free_block(fs, block_num);
        ext2_free_inode(fs, inode_num, 1);
        return -ENOMEM;
    }

    memset(&inode, 0, sizeof(inode));
    memset(block_buf, 0, fs->block_size);

    now = (uint32_t)get_time();

    inode.i_mode = (uint16_t)(S_IFDIR | (permission & 0777));
    inode.i_uid = dir->uid;
    inode.i_gid = dir->gid;
    inode.i_size = fs->block_size;
    inode.i_links_count = 2;
    inode.i_blocks = fs->block_size / 512;
    inode.i_block[0] = block_num;
    inode.i_atime = now;
    inode.i_mtime = now;
    inode.i_ctime = now;

    dot_len = (uint16_t)(((8 + 1 + 3) / 4) * 4);
    {
        ext2_dirent_t *dot = (ext2_dirent_t *)block_buf;
        ext2_dirent_t *dotdot = (ext2_dirent_t *)(block_buf + dot_len);

        dot->inode = inode_num;
        dot->rec_len = dot_len;
        dot->name_len = 1;
        dot->file_type = EXT2_FT_DIR;
        dot->name[0] = '.';

        dotdot->inode = dir_ctx->inode_num;
        dotdot->rec_len = (uint16_t)(fs->block_size - dot_len);
        dotdot->name_len = 2;
        dotdot->file_type = EXT2_FT_DIR;
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';
    }

    if (ext2_write_block(fs, block_num, block_buf) != fs->block_size) {
        kfree(block_buf, fs->block_size);
        ext2_free_block(fs, block_num);
        ext2_free_inode(fs, inode_num, 1);
        return -EIO;
    }
    kfree(block_buf, fs->block_size);

    if (ext2_write_inode(fs, inode_num, &inode) != 0) {
        ext2_free_block(fs, block_num);
        ext2_free_inode(fs, inode_num, 1);
        return -EIO;
    }

    if (ext2_add_entry(dir, name, inode_num, EXT2_FT_DIR) != 0) {
        ext2_free_inode_blocks(fs, &inode);
        ext2_free_inode(fs, inode_num, 1);
        return -EIO;
    }

    dir_ctx->inode.i_links_count++;
    dir_ctx->inode.i_mtime = now;
    dir_ctx->inode.i_ctime = now;
    if (ext2_write_inode(fs, dir_ctx->inode_num, &dir_ctx->inode) != 0) {
        ext2_remove_entry(dir, name);
        ext2_free_inode_blocks(fs, &inode);
        ext2_free_inode(fs, inode_num, 1);
        dir_ctx->inode.i_links_count--;
        return -EIO;
    }

    return 0;
}

static int ext2_unlink(fs_node_t *dir, const char *name) {
    fs_node_t *victim;
    ext2_node_t *victim_ctx;
    ext2_fs_t *fs;
    int ret;

    if (!dir || !name || !name[0]) return -EINVAL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;

    victim = ext2_finddir(dir, (char *)name);
    if (!victim) return -ENOENT;
    if ((victim->flags & 0x7) == FS_DIRECTORY) return -EISDIR;

    victim_ctx = (ext2_node_t *)(uintptr_t)victim->impl;
    fs = victim_ctx->fs;

    ret = ext2_remove_entry(dir, name);
    if (ret != 0) return -EIO;

    if (victim_ctx->inode.i_links_count > 0) {
        victim_ctx->inode.i_links_count--;
    }

    if (victim_ctx->inode.i_links_count == 0) {
        victim_ctx->inode.i_dtime = (uint32_t)get_time();
        ret = ext2_free_inode_blocks(fs, &victim_ctx->inode);
        if (ret != 0) return ret;
        if (ext2_write_inode(fs, victim_ctx->inode_num, &victim_ctx->inode) != 0) {
            return -EIO;
        }
        ext2_free_inode(fs, victim_ctx->inode_num, 0);
        memset(&victim_ctx->inode, 0, sizeof(victim_ctx->inode));
        victim->length = 0;
    } else {
        /* Update ctime: link count changed */
        victim_ctx->inode.i_ctime = (uint32_t)get_time();
        if (ext2_write_inode(fs, victim_ctx->inode_num, &victim_ctx->inode) != 0) {
            return -EIO;
        }
    }

    return 0;
}

static int ext2_dir_is_empty(fs_node_t *node) {
    uint64_t idx = 0;
    struct dirent *de;

    if (!node || (node->flags & 0x7) != FS_DIRECTORY) return 0;

    while ((de = ext2_readdir(node, idx++)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        return 0;
    }
    return 1;
}

static int ext2_rmdir(fs_node_t *dir, const char *name) {
    fs_node_t *victim;
    ext2_node_t *dir_ctx;
    ext2_node_t *victim_ctx;
    ext2_fs_t *fs;
    int ret;

    if (!dir || !name || !name[0]) return -EINVAL;
    if ((dir->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;

    victim = ext2_finddir(dir, (char *)name);
    if (!victim) return -ENOENT;
    if ((victim->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if (!ext2_dir_is_empty(victim)) return -ENOTEMPTY;

    dir_ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    victim_ctx = (ext2_node_t *)(uintptr_t)victim->impl;
    fs = victim_ctx->fs;

    ret = ext2_remove_entry(dir, name);
    if (ret != 0) return -EIO;

    if (dir_ctx->inode.i_links_count > 0) {
        dir_ctx->inode.i_links_count--;
    }

    dir_ctx->inode.i_mtime = (uint32_t)get_time();
    dir_ctx->inode.i_ctime = dir_ctx->inode.i_mtime;
    if (ext2_write_inode(fs, dir_ctx->inode_num, &dir_ctx->inode) != 0) {
        return -EIO;
    }

    victim_ctx->inode.i_links_count = 0;
    victim_ctx->inode.i_dtime = (uint32_t)get_time();
    ret = ext2_free_inode_blocks(fs, &victim_ctx->inode);
    if (ret != 0) return ret;
    if (ext2_write_inode(fs, victim_ctx->inode_num, &victim_ctx->inode) != 0) {
        return -EIO;
    }
    ext2_free_inode(fs, victim_ctx->inode_num, 1);
    memset(&victim_ctx->inode, 0, sizeof(victim_ctx->inode));
    victim->length = 0;

    return 0;
}
