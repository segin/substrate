#include <fs/ext2/ext2.h>
#include <vfs/vfs.h>
#include <kern/console.h>
#include <string.h>
#include <vm/vm_kmem.h>

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

// Forward declarations
static fs_node_t *ext2_mount(const char *device, uint32_t flags, void *data);
static size_t ext2_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static size_t ext2_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
static struct dirent *ext2_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *ext2_finddir(fs_node_t *node, char *name);
static int ext2_readlink_fn(fs_node_t *node, char *buf, size_t size);
uint32_t ext2_alloc_block(ext2_fs_t *fs);

// Read a block from the device
uint32_t ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer) {
    if (!fs || !fs->device || !fs->device->read) return 0;
    off_t offset = (off_t)block_num * fs->block_size;
    return fs->device->read(fs->device, offset, fs->block_size, buffer);
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
    uint8_t *block_buf = kmalloc(4096);
    if (!block_buf) return -1;

    if (ext2_read_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        kfree(block_buf, 4096);
        return -1;
    }
    
    memcpy(inode, block_buf + inode_offset, sizeof(ext2_inode_t));
    kfree(block_buf, 4096);
    return 0;
}

// Write a block to the device
uint32_t ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buffer) {
    if (!fs || !fs->device || !fs->device->write) return 0;
    off_t offset = (off_t)block_num * fs->block_size;
    return fs->device->write(fs->device, offset, fs->block_size, (uint8_t *)buffer);
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
    uint8_t *block_buf = kmalloc(4096);
    if (!block_buf) return -1;

    if (ext2_read_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        kfree(block_buf, 4096);
        return -1;
    }
    
    // Update the inode data
    memcpy(block_buf + inode_offset, inode, sizeof(ext2_inode_t));
    
    // Write the block back to disk
    if (ext2_write_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        kfree(block_buf, 4096);
        return -1;
    }
    
    kfree(block_buf, 4096);
    return 0;
}

// Get block number for a given file block index (handles indirect blocks)
// Requires caller-provided scratch buffers (each size of block_size)
static uint32_t ext2_get_block_num(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx,
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
        // Need indirect buffer
        if (!indirect_buf) return 0;
        ext2_read_block(fs, inode->i_block[12], indirect_buf);
        return indirect_buf[block_idx];
    }
    block_idx -= ptrs_per_block;
    
    // Double indirect block (13)
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[13] == 0) return 0;
        if (!dindirect_buf || !indirect_buf) return 0;

        ext2_read_block(fs, inode->i_block[13], dindirect_buf);
        uint32_t indirect_idx = block_idx / ptrs_per_block;
        uint32_t direct_idx = block_idx % ptrs_per_block;
        if (dindirect_buf[indirect_idx] == 0) return 0;
        ext2_read_block(fs, dindirect_buf[indirect_idx], indirect_buf);
        return indirect_buf[direct_idx];
    }
    block_idx -= ptrs_per_block * ptrs_per_block;
    
    // Triple indirect block (14)
    if (block_idx < ptrs_per_block * ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[14] == 0) return 0;
        if (!tindirect_buf || !dindirect_buf || !indirect_buf) return 0;
        
        // Read triple indirect block
        ext2_read_block(fs, inode->i_block[14], tindirect_buf);
        
        // Calculate indices for triple indirection
        uint32_t tindirect_idx = block_idx / (ptrs_per_block * ptrs_per_block);
        uint32_t remaining = block_idx % (ptrs_per_block * ptrs_per_block);
        uint32_t dindirect_idx = remaining / ptrs_per_block;
        uint32_t indirect_idx = remaining % ptrs_per_block;
        
        // Read double indirect block
        if (tindirect_buf[tindirect_idx] == 0) return 0;
        ext2_read_block(fs, tindirect_buf[tindirect_idx], dindirect_buf);
        
        // Read indirect block
        if (dindirect_buf[dindirect_idx] == 0) return 0;
        ext2_read_block(fs, dindirect_buf[dindirect_idx], indirect_buf);
        
        return indirect_buf[indirect_idx];
    }
    
    // Beyond triple indirect
    return 0;
}

// Read data from an inode at a given offset
uint32_t ext2_inode_read(ext2_fs_t *fs, ext2_inode_t *inode, off_t offset, uint32_t size, void *buffer) {
    if (offset >= inode->i_size) return 0;
    if (offset + size > inode->i_size) size = inode->i_size - offset;
    
    // Allocate scratch buffers
    uint32_t block_size = fs->block_size;
    uint8_t *block_buf = kmalloc(block_size);
    uint32_t *indirect = kmalloc(block_size);
    uint32_t *dindirect = kmalloc(block_size);
    uint32_t *tindirect = kmalloc(block_size);

    if (!block_buf || !indirect || !dindirect || !tindirect) {
        if (block_buf) kfree(block_buf, block_size);
        if (indirect) kfree(indirect, block_size);
        if (dindirect) kfree(dindirect, block_size);
        if (tindirect) kfree(tindirect, block_size);
        return 0;
    }

    uint8_t *buf = (uint8_t *)buffer;
    uint32_t total_read = 0;
    
    while (size > 0) {
        // Use 64-bit division/modulo
        uint32_t block_idx = (uint32_t)(offset / fs->block_size);
        uint32_t block_offset = (uint32_t)(offset % fs->block_size);
        uint32_t block_num = ext2_get_block_num(fs, inode, block_idx, indirect, dindirect, tindirect);
        
        if (block_num == 0) {
            // Sparse file - return zeros
            uint32_t to_copy = fs->block_size - block_offset;
            if (to_copy > size) to_copy = size;
            memset(buf, 0, to_copy);
            buf += to_copy;
            total_read += to_copy;
            offset += to_copy;
            size -= to_copy;
            continue;
        }
        
        ext2_read_block(fs, block_num, block_buf);
        
        uint32_t to_copy = fs->block_size - block_offset;
        if (to_copy > size) to_copy = size;
        
        memcpy(buf, block_buf + block_offset, to_copy);
        buf += to_copy;
        total_read += to_copy;
        offset += to_copy;
        size -= to_copy;
    }
    
    kfree(block_buf, block_size);
    kfree(indirect, block_size);
    kfree(dindirect, block_size);
    kfree(tindirect, block_size);

    return total_read;
}

// Allocate and add a block to an inode
static int ext2_alloc_inode_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx, uint32_t *indirect_buf) {
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
            memset(indirect_buf, 0, fs->block_size);
            ext2_write_block(fs, new_indirect, indirect_buf);
        }
        
        // Allocate data block
        ext2_read_block(fs, inode->i_block[12], indirect_buf);
        uint32_t new_block = ext2_alloc_block(fs);
        if (new_block == 0) return -1;
        indirect_buf[block_idx] = new_block;
        ext2_write_block(fs, inode->i_block[12], indirect_buf);
        return 0;
    }
    
    // Double indirect and beyond not implemented
    return -1;
}

// Write data to an inode at a given offset
uint32_t ext2_inode_write(ext2_fs_t *fs, ext2_inode_t *inode, off_t offset, uint32_t size, const void *buffer) {
    uint32_t block_size = fs->block_size;
    uint8_t *block_buf = kmalloc(block_size);
    uint32_t *indirect = kmalloc(block_size);
    uint32_t *dindirect = kmalloc(block_size);
    uint32_t *tindirect = kmalloc(block_size);

    if (!block_buf || !indirect || !dindirect || !tindirect) {
        if (block_buf) kfree(block_buf, block_size);
        if (indirect) kfree(indirect, block_size);
        if (dindirect) kfree(dindirect, block_size);
        if (tindirect) kfree(tindirect, block_size);
        return 0;
    }

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
    
    // Update file size if we extended it
    if ((uint32_t)offset > inode->i_size) {
        inode->i_size = (uint32_t)offset;
    }
    
    // Update modification time
    extern int64_t get_time(void);
    inode->i_mtime = (uint32_t)get_time();
    
    kfree(block_buf, block_size);
    kfree(indirect, block_size);
    kfree(dindirect, block_size);
    kfree(tindirect, block_size);

    return total_written;
}

// Allocate a node from the cache
static fs_node_t *ext2_alloc_node(ext2_fs_t *fs, uint32_t inode_num, ext2_inode_t *inode) {
    int idx = ext2_node_cache_idx++ % EXT2_NODE_CACHE_SIZE;
    
    ext2_node_t *ctx = &ext2_node_cache[idx];
    fs_node_t *node = &ext2_fs_node_cache[idx];
    
    ctx->fs = fs;
    ctx->inode_num = inode_num;
    memcpy(&ctx->inode, inode, sizeof(ext2_inode_t));
    
    memset(node, 0, sizeof(fs_node_t));
    node->inode = inode_num;
    node->length = inode->i_size;
    node->mask = inode->i_mode & 0xFFF;
    node->uid = inode->i_uid;
    node->gid = inode->i_gid;
    node->impl = (uint32_t)(uintptr_t)ctx;
    
    // Set type and callbacks based on inode mode
    uint16_t type = inode->i_mode & 0xF000;
    if (type == EXT2_S_IFDIR) {
        node->flags = FS_DIRECTORY;
        node->readdir = ext2_readdir;
        node->finddir = ext2_finddir;
    } else if (type == EXT2_S_IFREG) {
        node->flags = FS_FILE;
        node->read = ext2_file_read;
        node->write = ext2_file_write;
    } else if (type == EXT2_S_IFLNK) {
        node->flags = FS_SYMLINK;
        node->readlink = ext2_readlink_fn;
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
static int ext2_readlink_fn(fs_node_t *node, char *buf, size_t size) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_inode_t *inode = &ctx->inode;
    
    uint32_t link_size = inode->i_size;
    if (size < link_size + 1) link_size = size - 1;
    
    // Fast symlink: if size <= 60, target is stored in i_block[]
    if (inode->i_size <= 60) {
        memcpy(buf, (char *)inode->i_block, link_size);
    } else {
        // Slow symlink: target is in data blocks
        ext2_inode_read(ctx->fs, inode, 0, link_size, (uint8_t *)buf);
    }
    buf[link_size] = '\0';
    return link_size;
}

// File read operation
static size_t ext2_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    return ext2_inode_read(ctx->fs, &ctx->inode, offset, size, buffer);
}

// File write operation
static size_t ext2_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_fs_t *fs = ctx->fs;
    
    uint32_t written = ext2_inode_write(fs, &ctx->inode, offset, size, buffer);
    
    // Write updated inode back to disk
    if (written > 0) {
        ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
        node->length = ctx->inode.i_size; // Update VFS node size
    }
    
    return written;
}

// Read directory entry at index
static struct dirent *ext2_readdir(fs_node_t *node, uint64_t index) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_fs_t *fs = ctx->fs;
    
    // Read the directory data
    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = 0;
    uint32_t cur_idx = 0;
    
    uint32_t block_size = fs->block_size;
    uint8_t *ext2_dir_buf = kmalloc(block_size);
    uint32_t *indirect = kmalloc(block_size);
    uint32_t *dindirect = kmalloc(block_size);
    uint32_t *tindirect = kmalloc(block_size);

    if (!ext2_dir_buf || !indirect || !dindirect || !tindirect) {
        if (ext2_dir_buf) kfree(ext2_dir_buf, block_size);
        if (indirect) kfree(indirect, block_size);
        if (dindirect) kfree(dindirect, block_size);
        if (tindirect) kfree(tindirect, block_size);
        return NULL;
    }

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
                    ctx->dirent.ino = de->inode;
                    memcpy(ctx->dirent.name, de->name, de->name_len);
                    ctx->dirent.name[de->name_len] = '\0';
                    result = &ctx->dirent;
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
    kfree(ext2_dir_buf, block_size);
    kfree(indirect, block_size);
    kfree(dindirect, block_size);
    kfree(tindirect, block_size);
    return result;
}

// Find entry by name in directory
static fs_node_t *ext2_finddir(fs_node_t *node, char *name) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_fs_t *fs = ctx->fs;
    
    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = 0;
    
    uint32_t block_size = fs->block_size;
    uint8_t *ext2_dir_buf = kmalloc(block_size);
    uint32_t *indirect = kmalloc(block_size);
    uint32_t *dindirect = kmalloc(block_size);
    uint32_t *tindirect = kmalloc(block_size);

    if (!ext2_dir_buf || !indirect || !dindirect || !tindirect) {
        if (ext2_dir_buf) kfree(ext2_dir_buf, block_size);
        if (indirect) kfree(indirect, block_size);
        if (dindirect) kfree(dindirect, block_size);
        if (tindirect) kfree(tindirect, block_size);
        return NULL;
    }

    fs_node_t *result_node = NULL;

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
                if (de->name_len == strlen(name) && 
                    strncmp(de->name, name, de->name_len) == 0) {
                    // Found it - read the inode and return a node
                    ext2_inode_t inode;
                    if (ext2_read_inode(fs, de->inode, &inode) == 0) {
                        result_node = ext2_alloc_node(fs, de->inode, &inode);
                        // Copy name
                        memcpy(result_node->name, de->name, de->name_len);
                        result_node->name[de->name_len] = '\0';
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
    kfree(ext2_dir_buf, block_size);
    kfree(indirect, block_size);
    kfree(dindirect, block_size);
    kfree(tindirect, block_size);
    return result_node;
}

// Mount ext2 filesystem
static fs_node_t *ext2_mount(const char *device, uint32_t flags, void *data) {
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
    
    // Setup root node
    ext2_root_ctx.fs = &ext2_fs;
    ext2_root_ctx.inode_num = EXT2_ROOT_INO;
    memcpy(&ext2_root_ctx.inode, &root_inode, sizeof(ext2_inode_t));
    
    memset(&ext2_root, 0, sizeof(fs_node_t));
    strcpy(ext2_root.name, "/");
    ext2_root.flags = FS_DIRECTORY;
    ext2_root.inode = EXT2_ROOT_INO;
    ext2_root.length = root_inode.i_size;
    ext2_root.impl = (uint32_t)(uintptr_t)&ext2_root_ctx;
    ext2_root.readdir = ext2_readdir;
    ext2_root.finddir = ext2_finddir;
    
    kprint("EXT2: Mounted successfully\n");
    return &ext2_root;
}

static filesystem_t ext2_filesystem = {
    .name = "ext2",
    .mount = ext2_mount,
};

void ext2_init(void) {
    kprint("Initializing EXT2 Driver...\n");
    vfs_register_filesystem(&ext2_filesystem);
}

// Allocate a block from the filesystem
uint32_t ext2_alloc_block(ext2_fs_t *fs) {
    if (!fs) return 0;
    
    uint8_t *bitmap_buf = kmalloc(4096);
    if (!bitmap_buf) return 0;
    
    // Search each block group for a free block
    for (uint32_t group = 0; group < fs->group_count; group++) {
        if (fs->bgd[group].bg_free_blocks_count == 0) continue;
        
        // Read the block bitmap
        if (ext2_read_block(fs, fs->bgd[group].bg_block_bitmap, bitmap_buf) != fs->block_size) {
            continue;
        }
        
        // Find the first free bit
        uint32_t bits_in_group = fs->blocks_per_group;
        uint32_t i = 0;
        uint32_t *bitmap32 = (uint32_t *)bitmap_buf;

        // Fast path: skip full words
        // We can safely skip only if the word is all ones (0xFFFFFFFF).
        // This is endian-safe because 0xFFFFFFFF is all ones regardless of endianness.
        for (; i + 32 <= bits_in_group; i += 32) {
            if (bitmap32[i / 32] != 0xFFFFFFFF) {
                break; // Found a word with free bits, fall through to slow path
            }
        }

        // Slow path: check remaining bits (and the word we broke on)
        for (; i < bits_in_group; i++) {
            uint32_t byte_idx = i / 8;
            uint32_t bit_idx = i % 8;
            
            if (!(bitmap_buf[byte_idx] & (1 << bit_idx))) {
                // Found free block - allocate it
                bitmap_buf[byte_idx] |= (1 << bit_idx);
                
                // Write bitmap back
                ext2_write_block(fs, fs->bgd[group].bg_block_bitmap, bitmap_buf);
                
                // Update block group descriptor
                fs->bgd[group].bg_free_blocks_count--;
                
                // Calculate absolute block number
                uint32_t block_num = group * fs->blocks_per_group + i + fs->sb.s_first_data_block;
                
                // Update superblock free blocks count
                fs->sb.s_free_blocks_count--;
                
                kfree(bitmap_buf, 4096);
                return block_num;
            }
        }
    }
    
    kfree(bitmap_buf, 4096);
    return 0; // No free blocks
}

// Free a block
void ext2_free_block(ext2_fs_t *fs, uint32_t block_num) {
    if (!fs || block_num == 0) return;
    
    // Calculate which group this block belongs to
    uint32_t group = (block_num - fs->sb.s_first_data_block) / fs->blocks_per_group;
    uint32_t index = (block_num - fs->sb.s_first_data_block) % fs->blocks_per_group;
    
    if (group >= fs->group_count) return;
    
    uint8_t *bitmap_buf = kmalloc(4096);
    if (!bitmap_buf) return;
    
    // Read the block bitmap
    if (ext2_read_block(fs, fs->bgd[group].bg_block_bitmap, bitmap_buf) != fs->block_size) {
        kfree(bitmap_buf, 4096);
        return;
    }
    
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

    kfree(bitmap_buf, 4096);
}

// Allocate an inode
uint32_t ext2_alloc_inode(ext2_fs_t *fs, int is_dir) {
    if (!fs) return 0;
    
    uint8_t *bitmap_buf = kmalloc(4096);
    if (!bitmap_buf) return 0;
    
    // Search each block group for a free inode
    for (uint32_t group = 0; group < fs->group_count; group++) {
        if (fs->bgd[group].bg_free_inodes_count == 0) continue;
        
        // Read the inode bitmap
        if (ext2_read_block(fs, fs->bgd[group].bg_inode_bitmap, bitmap_buf) != fs->block_size) {
            continue;
        }
        
        // Find the first free bit (skip reserved inodes in first group)
        uint32_t start = (group == 0) ? fs->sb.s_first_ino : 0;
        uint32_t bits_in_group = fs->inodes_per_group;
        
        for (uint32_t i = start; i < bits_in_group; i++) {
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
                
                // Initialize the inode
                ext2_inode_t inode;
                memset(&inode, 0, sizeof(ext2_inode_t));
                extern int64_t get_time(void);
                uint32_t now = (uint32_t)get_time();
                inode.i_ctime = now;
                inode.i_mtime = now;
                inode.i_atime = now;
                
                ext2_write_inode(fs, inode_num, &inode);
                
                kfree(bitmap_buf, 4096);
                return inode_num;
            }
        }
    }
    
    kfree(bitmap_buf, 4096);
    return 0; // No free inodes
}

// Free an inode
void ext2_free_inode(ext2_fs_t *fs, uint32_t inode_num, int was_dir) {
    if (!fs || inode_num == 0) return;
    
    // Calculate which group this inode belongs to
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    if (group >= fs->group_count) return;
    
    uint8_t *bitmap_buf = kmalloc(4096);
    if (!bitmap_buf) return;
    
    // Read the inode bitmap
    if (ext2_read_block(fs, fs->bgd[group].bg_inode_bitmap, bitmap_buf) != fs->block_size) {
        kfree(bitmap_buf, 4096);
        return;
    }
    
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

    kfree(bitmap_buf, 4096);
}

// Add directory entry
int ext2_add_entry(fs_node_t *dir, const char *name, uint32_t inode) {
    if (!dir || !name || inode == 0) return -1;
    
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    ext2_fs_t *fs = ctx->fs;
    
    uint32_t name_len = strlen(name);
    if (name_len > 255) return -1;
    
    // Calculate required entry size (aligned to 4 bytes)
    uint32_t required_size = ((8 + name_len + 3) / 4) * 4;
    
    uint32_t block_size = fs->block_size;
    uint8_t *block_buf = kmalloc(block_size);
    uint32_t *indirect = kmalloc(block_size);
    uint32_t *dindirect = kmalloc(block_size);
    uint32_t *tindirect = kmalloc(block_size);

    if (!block_buf || !indirect || !dindirect || !tindirect) {
        if (block_buf) kfree(block_buf, block_size);
        if (indirect) kfree(indirect, block_size);
        if (dindirect) kfree(dindirect, block_size);
        if (tindirect) kfree(tindirect, block_size);
        return -1;
    }

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
                new_de->file_type = 0; // Unknown for now
                memcpy(new_de->name, name, name_len);
                
                ext2_write_block(fs, block_num, block_buf);
                result = 0;
                goto cleanup;
            }
            
            // Can we reuse a deleted entry?
            if (de->inode == 0 && de->rec_len >= required_size) {
                de->inode = inode;
                de->name_len = name_len;
                de->file_type = 0;
                memcpy(de->name, name, name_len);
                
                ext2_write_block(fs, block_num, block_buf);
                result = 0;
                goto cleanup;
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
    de->file_type = 0;
    memcpy(de->name, name, name_len);
    
    ext2_write_block(fs, new_block, block_buf);
    
    // Update directory size
    ctx->inode.i_size += fs->block_size;
    ctx->inode.i_blocks += (fs->block_size / 512);
    
    extern int64_t get_time(void);
    ctx->inode.i_mtime = (uint32_t)get_time();
    
    ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
    result = 0;
    
cleanup:
    kfree(block_buf, block_size);
    kfree(indirect, block_size);
    kfree(dindirect, block_size);
    kfree(tindirect, block_size);
    return result;
}

// Remove directory entry
int ext2_remove_entry(fs_node_t *dir, const char *name) {
    if (!dir || !name) return -1;
    
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)dir->impl;
    ext2_fs_t *fs = ctx->fs;
    
    uint32_t block_size = fs->block_size;
    uint8_t *block_buf = kmalloc(block_size);
    uint32_t *indirect = kmalloc(block_size);
    uint32_t *dindirect = kmalloc(block_size);
    uint32_t *tindirect = kmalloc(block_size);

    if (!block_buf || !indirect || !dindirect || !tindirect) {
        if (block_buf) kfree(block_buf, block_size);
        if (indirect) kfree(indirect, block_size);
        if (dindirect) kfree(dindirect, block_size);
        if (tindirect) kfree(tindirect, block_size);
        return -1;
    }

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
            if (de->inode != 0 && de->name_len == strlen(name) &&
                strncmp(de->name, name, de->name_len) == 0) {
                
                // Merge with previous entry if possible
                if (prev_de) {
                    prev_de->rec_len += de->rec_len;
                } else {
                    // First entry in block - just mark as deleted
                    de->inode = 0;
                }
                
                ext2_write_block(fs, block_num, block_buf);
                
                // Update directory mtime
                extern int64_t get_time(void);
                ctx->inode.i_mtime = (uint32_t)get_time();
                ext2_write_inode(fs, ctx->inode_num, &ctx->inode);
                
                result = 0;
                goto cleanup;
            }
            
            prev_de = de;
            block_off += de->rec_len;
            pos += de->rec_len;
        }
    }
    
cleanup:
    kfree(block_buf, block_size);
    kfree(indirect, block_size);
    kfree(dindirect, block_size);
    kfree(tindirect, block_size);
    return result;
}
