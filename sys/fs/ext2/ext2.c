#include <fs/ext2/ext2.h>
#include <vfs/vfs.h>
#include <vfs/buf.h>
#include <kern/console.h>
#include <kern/time.h>
#include <string.h>
#include <vm/vm_kmem.h>
#include <vm/uma.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/mount.h>

#ifdef HOST_TEST
/*
 * Host-test stubs.  The buffer cache lives in bio.c which doesn't get
 * linked into per-FS host tests; falling through to direct device I/O
 * preserves the existing test contracts while leaving real cache
 * coverage to host_test_bio.
 */
struct buf *bio_dev_get(void *d, int64_t b, size_t s)
    { (void)d; (void)b; (void)s; return NULL; }
void bio_dev_release(struct buf *bp)        { (void)bp; }
void bio_dev_invalidate(void *d, int64_t b) { (void)d; (void)b; }
void bio_dev_purge(void *d)                 { (void)d; }
#endif

// Cache for dynamically created nodes
#define EXT2_NODE_CACHE_SIZE 64
static ext2_node_t ext2_node_cache[EXT2_NODE_CACHE_SIZE];
static fs_node_t ext2_fs_node_cache[EXT2_NODE_CACHE_SIZE];
static int ext2_node_cache_idx = 0;

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
int ext2_unmount(fs_node_t *node);
size_t ext2_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
size_t ext2_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
struct dirent *ext2_readdir(fs_node_t *node, uint64_t index);
fs_node_t *ext2_finddir(fs_node_t *node, char *name);
int ext2_readlink(fs_node_t *node, char *buf, size_t size);
uint32_t ext2_alloc_block(ext2_fs_t *fs);
static int ext2_mknod(fs_node_t *dir, const char *name, uint16_t mode, uint32_t dev);
int ext2_mkdir(fs_node_t *dir, const char *name, uint16_t permission);
static int ext2_symlink(fs_node_t *dir, const char *target, const char *name);
int ext2_unlink(fs_node_t *dir, const char *name);
int ext2_rmdir(fs_node_t *dir, const char *name);
static int ext2_dir_is_empty(fs_node_t *node);
static int ext2_add_entry(fs_node_t *dir, const char *name, uint32_t inode, uint8_t file_type);
static int ext2_remove_entry(fs_node_t *dir, const char *name);
static int ext2_flush_super(ext2_fs_t *fs);
int ext2_statfs(fs_node_t *node, struct statfs *buf);
int ext2_link(fs_node_t *parent, fs_node_t *source, const char *name);
int ext2_rename(fs_node_t *old_parent, const char *old_name, fs_node_t *new_parent, const char *new_name);
static int ext2_flush_group_desc(ext2_fs_t *fs, uint32_t group);
static uint8_t ext2_dirent_type_from_mode(uint16_t mode);
static uint8_t ext2_file_type_to_dt(uint8_t ext2_type);
static int ext2_free_indirect_tree(ext2_fs_t *fs, uint32_t block_num, uint32_t depth);
static int ext2_free_inode_blocks(ext2_fs_t *fs, ext2_inode_t *inode);
static int ext2_chmod(fs_node_t *node, uint32_t mode);

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

// Read a block from the device, going through the unified buffer cache.
// On cache hit the data is served from RAM with no device I/O.  On miss
// we fill the cache buffer once, then serve subsequent reads of the same
// block from memory.  The cache is keyed by (device fs_node *, block_num,
// block_size), so two callers reading the same block share one entry.
uint32_t ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer) {
    if (!fs || !fs->device || !fs->device->read) return 0;

    struct buf *bp = bio_dev_get(fs->device, (int64_t)block_num, fs->block_size);
    if (bp) {
        if (!(bp->b_flags & B_CACHE)) {
            off_t offset = (off_t)block_num * fs->block_size;
            uint32_t got = fs->device->read(fs->device, offset, fs->block_size, bp->b_data);
            if (got != fs->block_size) {
                /* Don't cache short/failed reads. */
                bp->b_flags |= B_INVAL;
                bio_dev_release(bp);
                return got;
            }
            bp->b_flags |= B_CACHE;
        }
        memcpy(buffer, bp->b_data, fs->block_size);
        bio_dev_release(bp);
        return fs->block_size;
    }

    /* Cache exhausted (e.g. low memory) — fall back to direct device read. */
    off_t offset = (off_t)block_num * fs->block_size;
    return fs->device->read(fs->device, offset, fs->block_size, buffer);
}

// Read multiple contiguous blocks.  This path stays out of the cache
// because (a) callers use it for big sequential extents where one large
// device transfer beats N cache lookups, and (b) the cache is keyed
// per-block so a single multi-block buffer wouldn't hash usefully.
// We still invalidate any per-block cache entries in the range so the
// next single-block read sees fresh data.
uint32_t ext2_read_blocks(ext2_fs_t *fs, uint32_t block_num, uint32_t count, void *buffer) {
    if (!fs || !fs->device || !fs->device->read) return 0;
    for (uint32_t i = 0; i < count; i++)
        bio_dev_invalidate(fs->device, (int64_t)(block_num + i));
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
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) return -1;

    if (ext2_read_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        kfree(block_buf, fs->block_size);
        return -1;
    }
    
    memcpy(inode, block_buf + inode_offset, sizeof(ext2_inode_t));
    kfree(block_buf, fs->block_size);
    return 0;
}

// Write a block to the device.  Write-through to keep the on-disk image
// consistent with the cache; refresh the cache entry if one exists so a
// subsequent read returns the just-written data instead of refetching.
uint32_t ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buffer) {
    if (!fs || !fs->device || !fs->device->write) return 0;
    off_t offset = (off_t)block_num * fs->block_size;
    uint32_t wrote = fs->device->write(fs->device, offset, fs->block_size, (uint8_t *)buffer);
    if (wrote == fs->block_size) {
        struct buf *bp = bio_dev_get(fs->device, (int64_t)block_num, fs->block_size);
        if (bp) {
            memcpy(bp->b_data, buffer, fs->block_size);
            bp->b_flags |= B_CACHE;
            bio_dev_release(bp);
        }
    } else {
        /* Short write — drop any cached entry to avoid serving stale data. */
        bio_dev_invalidate(fs->device, (int64_t)block_num);
    }
    return wrote;
}

static int is_sparse_backup(uint32_t group) {
    if (group <= 1) return 1;
    for (uint32_t p = 3; p <= 7; p += 2) {
        uint32_t val = p;
        while (val < group) val *= p;
        if (val == group) return 1;
    }
    return 0;
}

static int ext2_flush_super(ext2_fs_t *fs) {
    if (!fs || !fs->device || !fs->device->write) return -EIO;
    
    // Write primary superblock
    if (fs->device->write(fs->device, 1024, 1024, (uint8_t *)&fs->sb) != 1024) {
        return -EIO;
    }

    // Write backups if necessary (EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER = 0x0001)
    int sparse = (fs->sb.s_feature_ro_compat & 0x0001);
    
    int backup_err = 0;
    for (uint32_t i = 1; i < fs->group_count; i++) {
        if (sparse && !is_sparse_backup(i)) continue;

        uint32_t block = i * fs->sb.s_blocks_per_group + fs->sb.s_first_data_block;
        // Superblock backups are at the start of the block
        uint8_t *tmp = kmalloc(fs->block_size);
        if (!tmp) {
            backup_err = -ENOMEM;
            continue;
        }
        memset(tmp, 0, fs->block_size);
        memcpy(tmp, &fs->sb, 1024);
        /* Don't drop write errors silently — a failed backup write
         * leaves the on-disk image inconsistent with the primary. */
        if (ext2_write_block(fs, block, tmp) != fs->block_size) {
            backup_err = -EIO;
        }
        kfree(tmp, fs->block_size);
    }

    return backup_err;
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
    block_buf = kmalloc(fs->block_size);
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
    kfree(block_buf, fs->block_size);
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
    uint8_t *block_buf = kmalloc(fs->block_size);
    if (!block_buf) return -1;

    if (ext2_read_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        kfree(block_buf, fs->block_size);
        return -1;
    }

    // Update the inode data
    memcpy(block_buf + inode_offset, inode, sizeof(ext2_inode_t));
    
    // Write the block back to disk
    if (ext2_write_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        kfree(block_buf, fs->block_size);
        return -1;
    }
    
    kfree(block_buf, fs->block_size);
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
int ext2_alloc_inode_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx, uint32_t *indirect, uint32_t *dindirect, uint32_t *tindirect) {
    uint32_t ptrs_per_block = fs->block_size / 4;
    uint32_t sectors_per_block = fs->block_size / 512;
    
    // Direct blocks (0-11)
    if (block_idx < 12) {
        uint32_t new_block = ext2_alloc_block(fs);
        if (new_block == 0) return -1;
        inode->i_block[block_idx] = new_block;
        inode->i_blocks += sectors_per_block;
        return 0;
    }
    block_idx -= 12;
    
    // Indirect block (12)
    if (block_idx < ptrs_per_block) {
        if (inode->i_block[12] == 0) {
            uint32_t new_indirect = ext2_alloc_block(fs);
            if (new_indirect == 0) return -1;
            inode->i_block[12] = new_indirect;
            inode->i_blocks += sectors_per_block;
            memset(indirect, 0, fs->block_size);
            ext2_write_block(fs, new_indirect, indirect);
        } else {
            ext2_read_block(fs, inode->i_block[12], (uint8_t *)indirect);
        }
        
        uint32_t new_block = ext2_alloc_block(fs);
        if (new_block == 0) return -1;
        indirect[block_idx] = new_block;
        inode->i_blocks += sectors_per_block;
        ext2_write_block(fs, inode->i_block[12], (uint8_t *)indirect);
        return 0;
    }
    block_idx -= ptrs_per_block;
    
    // Double indirect block (13)
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[13] == 0) {
            uint32_t new_dindirect = ext2_alloc_block(fs);
            if (new_dindirect == 0) return -1;
            inode->i_block[13] = new_dindirect;
            inode->i_blocks += sectors_per_block;
            memset(dindirect, 0, fs->block_size);
            ext2_write_block(fs, new_dindirect, (uint8_t *)dindirect);
        } else {
            ext2_read_block(fs, inode->i_block[13], (uint8_t *)dindirect);
        }
        
        uint32_t di_idx = block_idx / ptrs_per_block;
        uint32_t off = block_idx % ptrs_per_block;
        
        if (dindirect[di_idx] == 0) {
            uint32_t new_indirect = ext2_alloc_block(fs);
            if (new_indirect == 0) return -1;
            dindirect[di_idx] = new_indirect;
            inode->i_blocks += sectors_per_block;
            ext2_write_block(fs, inode->i_block[13], (uint8_t *)dindirect);
            memset(indirect, 0, fs->block_size);
            ext2_write_block(fs, new_indirect, (uint8_t *)indirect);
        } else {
            ext2_read_block(fs, dindirect[di_idx], (uint8_t *)indirect);
        }
        
        uint32_t new_block = ext2_alloc_block(fs);
        if (new_block == 0) return -1;
        indirect[off] = new_block;
        inode->i_blocks += sectors_per_block;
        ext2_write_block(fs, dindirect[di_idx], (uint8_t *)indirect);
        return 0;
    }
    block_idx -= ptrs_per_block * ptrs_per_block;
    
    // Triple indirect block (14)
    if (block_idx < ptrs_per_block * ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[14] == 0) {
            uint32_t new_tindirect = ext2_alloc_block(fs);
            if (new_tindirect == 0) return -1;
            inode->i_block[14] = new_tindirect;
            inode->i_blocks += sectors_per_block;
            memset(tindirect, 0, fs->block_size);
            ext2_write_block(fs, new_tindirect, (uint8_t *)tindirect);
        } else {
            ext2_read_block(fs, inode->i_block[14], (uint8_t *)tindirect);
        }
        
        uint32_t ddi_idx = block_idx / (ptrs_per_block * ptrs_per_block);
        uint32_t rem = block_idx % (ptrs_per_block * ptrs_per_block);
        
        if (tindirect[ddi_idx] == 0) {
            uint32_t new_dindirect = ext2_alloc_block(fs);
            if (new_dindirect == 0) return -1;
            tindirect[ddi_idx] = new_dindirect;
            inode->i_blocks += sectors_per_block;
            ext2_write_block(fs, inode->i_block[14], (uint8_t *)tindirect);
            memset(dindirect, 0, fs->block_size);
            ext2_write_block(fs, new_dindirect, (uint8_t *)dindirect);
        } else {
            ext2_read_block(fs, tindirect[ddi_idx], (uint8_t *)dindirect);
        }
        
        uint32_t di_idx = rem / ptrs_per_block;
        uint32_t off = rem % ptrs_per_block;
        
        if (dindirect[di_idx] == 0) {
            uint32_t new_indirect = ext2_alloc_block(fs);
            if (new_indirect == 0) return -1;
            dindirect[di_idx] = new_indirect;
            inode->i_blocks += sectors_per_block;
            ext2_write_block(fs, tindirect[ddi_idx], (uint8_t *)dindirect);
            memset(indirect, 0, fs->block_size);
            ext2_write_block(fs, new_indirect, (uint8_t *)indirect);
        } else {
            ext2_read_block(fs, dindirect[di_idx], (uint8_t *)indirect);
        }
        
        uint32_t new_block = ext2_alloc_block(fs);
        if (new_block == 0) return -1;
        indirect[off] = new_block;
        inode->i_blocks += sectors_per_block;
        ext2_write_block(fs, dindirect[di_idx], (uint8_t *)indirect);
        return 0;
    }
    
    return -EFBIG;
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
            if (ext2_alloc_inode_block(fs, inode, block_idx, indirect, dindirect, tindirect) != 0) {
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

    // 1. Search for existing node in cache
    for (int i = 0; i < EXT2_NODE_CACHE_SIZE; i++) {
        if (ext2_node_cache[i].fs == fs && ext2_node_cache[i].inode_num == inode_num) {
            return &ext2_fs_node_cache[i];
        }
    }

    // 2. Allocate a new slot from the cache
    int start = ext2_node_cache_idx % EXT2_NODE_CACHE_SIZE;
    for (int i = 0; i < EXT2_NODE_CACHE_SIZE; i++) {
        int probe = (start + i) % EXT2_NODE_CACHE_SIZE;
        if (ext2_node_cache[probe].pin_count == 0 && ext2_node_cache[probe].lock.locked == 0) {
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
    
    // Clean up old context
    if (ctx->fs) {
        uint32_t old_block_size = ctx->fs->block_size;
        if (ctx->block_buf) { kfree(ctx->block_buf, old_block_size); ctx->block_buf = NULL; }
        if (ctx->indirect_buf) { kfree(ctx->indirect_buf, old_block_size); ctx->indirect_buf = NULL; }
        if (ctx->dindirect_buf) { kfree(ctx->dindirect_buf, old_block_size); ctx->dindirect_buf = NULL; }
        if (ctx->tindirect_buf) { kfree(ctx->tindirect_buf, old_block_size); ctx->tindirect_buf = NULL; }
    }

    memset(ctx, 0, sizeof(ext2_node_t));
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

    memset(node, 0, sizeof(fs_node_t));
    node->inode = inode_num;
    node->length = inode->i_size;
    node->mask = inode->i_mode & 0xFFF;
    node->uid = inode->i_uid;
    node->gid = inode->i_gid;
    node->atime = inode->i_atime;
    node->mtime = inode->i_mtime;
    node->ctime = inode->i_ctime;
    node->impl = (uintptr_t)ctx;
    node->mp = fs->mp; // Correctly associate with mount point!
    node->open = ext2_node_open;
    node->close = ext2_node_close;
    node->chmod = ext2_chmod;
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
        node->link = ext2_link;
        node->rename = ext2_rename;
        node->statfs = ext2_statfs;
        node->unmount = ext2_unmount;
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

static int ext2_chmod(fs_node_t *node, uint32_t mode) {
    ext2_node_t *ctx;

    if (!node) return -EINVAL;

    ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx || !ctx->fs) return -EINVAL;

    ctx->inode.i_mode = (uint16_t)((ctx->inode.i_mode & 0xF000U) | (mode & 0x0FFFU));
    ctx->inode.i_ctime = (uint32_t)node->ctime;

    if (ext2_write_inode(ctx->fs, ctx->inode_num, &ctx->inode) != 0) {
        return -EIO;
    }

    return 0;
}

// Read symlink target
int ext2_readlink(fs_node_t *node, char *buf, size_t size) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_inode_t *inode = &ctx->inode;
    
    uint32_t link_size = inode->i_size;
    if (size < link_size + 1) link_size = size - 1;
    
    // Fast symlink: if size <= 60, target is stored in i_block[]
    if (inode->i_size <= 60) {
        /* Clamp to sizeof(i_block) to prevent overflow (finding #26) */
        if (link_size > 60) link_size = 60;
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

    // Lazily set mount pointer in filesystem context
    if (!fs->mp && node->mp) fs->mp = node->mp;
    
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
        while (block_off + 8 <= fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(ext2_dir_buf + block_off);
            
            if (de->rec_len < 8 || block_off + de->rec_len > fs->block_size) break;

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
                    ctx->current_dirent.d_namlen = (uint8_t)len;
                    ctx->current_dirent.d_type = ext2_file_type_to_dt(de->file_type);
                    ctx->current_dirent.d_reclen = (uint16_t)(((8 + len + 1 + 3) / 4) * 4);
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
    
    // Lazily set mount pointer in filesystem context
    if (!fs->mp && node->mp) fs->mp = node->mp;
    
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
                if (!result_node) goto cleanup;
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
        
        while (block_off + 8 <= fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(ext2_dir_buf + block_off);
            
            if (de->rec_len < 8 || block_off + de->rec_len > fs->block_size) break;

            if (de->inode != 0 && de->name_len > 0) {
                // Compare names
                if (de->name_len == name_len &&
                    memcmp(de->name, name, de->name_len) == 0) {
                    // Found it - read the inode and return a node
                    ext2_inode_t inode;
                    if (ext2_read_inode(fs, de->inode, &inode) == 0) {
                        result_node = ext2_alloc_node(fs, de->inode, &inode);
                        if (!result_node) goto cleanup;
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
    
    ext2_fs_t *fs = kmalloc(sizeof(ext2_fs_t));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(ext2_fs_t));

    // Read superblock (at offset 1024)
    uint8_t sb_buf[1024];
    uint32_t read = dev->read(dev, 1024, 1024, sb_buf);
    if (read != 1024) {
        kprint("EXT2: Failed to read superblock\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    
    memcpy(&fs->sb, sb_buf, sizeof(ext2_superblock_t));
    
    if (fs->sb.s_magic != EXT2_SUPER_MAGIC) {
        kprint("EXT2: Invalid magic number\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    
    fs->device = dev;

    // Validate s_log_block_size before shifting (max 64KB blocks, i.e. log=6)
    if (fs->sb.s_log_block_size > 6) {
        kprint("EXT2: Invalid s_log_block_size\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    fs->block_size = 1024 << fs->sb.s_log_block_size;

    fs->inodes_per_group = fs->sb.s_inodes_per_group;
    fs->blocks_per_group = fs->sb.s_blocks_per_group;
    if (fs->blocks_per_group == 0) {
        kprint("EXT2: Invalid blocks_per_group\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    fs->group_count = (fs->sb.s_blocks_count + fs->blocks_per_group - 1) / fs->blocks_per_group;
    
    fs->inode_size = (fs->sb.s_rev_level >= 1) ? fs->sb.s_inode_size : EXT2_GOOD_OLD_INODE_SIZE;

    // Validate inode_size is non-zero and fits within a block
    if (fs->inode_size == 0 || fs->inode_size > fs->block_size) {
        kprint("EXT2: Invalid inode_size\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }

    /* Sanity-cap the group descriptor table.  group_count is derived
     * from attacker-controllable s_blocks_count / s_blocks_per_group;
     * without a ceiling here a crafted superblock could ask for an
     * arbitrarily huge kmalloc.  16 MiB of BGDs covers >500k groups,
     * which is well past any realistic ext2/3/4 filesystem. */
    uint64_t raw_bgd_size64 = (uint64_t)fs->group_count * sizeof(ext2_group_desc_t);
    if (raw_bgd_size64 > (16ULL << 20)) {
        kprint("EXT2: group descriptor table too large; refusing mount\n");
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    uint32_t raw_bgd_size = (uint32_t)raw_bgd_size64;
    uint32_t bgd_blocks = (raw_bgd_size + fs->block_size - 1) / fs->block_size;
    uint32_t bgd_size = bgd_blocks * fs->block_size;

    fs->bgd = kmalloc(bgd_size);
    if (!fs->bgd) {
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    memset(fs->bgd, 0, bgd_size);

    fs->last_alloc_group = 0;
    fs->last_alloc_bit = 0;

    // Read block group descriptor table (starts at block 2 for 1K blocks, block 1 for larger)
    uint32_t bgd_block = (fs->block_size == 1024) ? 2 : 1;

    // Read enough blocks for the BGD table
    for (uint32_t i = 0; i < bgd_blocks; i++) {        ext2_read_block(fs, bgd_block + i, 
                       ((uint8_t *)fs->bgd) + i * fs->block_size);
    }
    
    // Read root inode (inode 2)
    ext2_inode_t root_inode;
    if (ext2_read_inode(fs, EXT2_ROOT_INO, &root_inode) != 0) {
        kprint("EXT2: Failed to read root inode\n");
        kfree(fs->bgd, bgd_size);
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }

    // Initialize active block bitmap cache
    fs->active_bg_group = (uint32_t)-1;
    fs->active_bg_bitmap = kmalloc(fs->block_size);
    if (!fs->active_bg_bitmap) {
        kfree(fs->bgd, bgd_size);
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    memset(fs->active_bg_bitmap, 0, fs->block_size);

    // Initialize active inode bitmap cache
    fs->active_inode_bg_group = (uint32_t)-1;
    fs->active_inode_bg_bitmap = kmalloc(fs->block_size);
    if (!fs->active_inode_bg_bitmap) {
        kfree(fs->active_bg_bitmap, fs->block_size);
        kfree(fs->bgd, bgd_size);
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }
    memset(fs->active_inode_bg_bitmap, 0, fs->block_size);

    // Setup root node
    fs_node_t *root_node = ext2_alloc_node(fs, EXT2_ROOT_INO, &root_inode);
    if (!root_node) {
        kprint("EXT2: Failed to allocate root node\n");
        kfree(fs->active_inode_bg_bitmap, fs->block_size);
        kfree(fs->active_bg_bitmap, fs->block_size);
        kfree(fs->bgd, bgd_size);
        kfree(fs, sizeof(ext2_fs_t));
        return NULL;
    }

    strncpy(root_node->name, "/", sizeof(root_node->name) - 1);
    root_node->name[sizeof(root_node->name) - 1] = '\0';
    
    ext2_node_t *root_ctx = (ext2_node_t *)(uintptr_t)root_node->impl;
    root_ctx->pin_count = 1; // Pin root node
    
    kprint("EXT2: Mounted successfully\n");
    return root_node;
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

static uint8_t ext2_file_type_to_dt(uint8_t ext2_type) {
    switch (ext2_type) {
        case EXT2_FT_REG_FILE: return DT_REG;
        case EXT2_FT_DIR:      return DT_DIR;
        case EXT2_FT_CHRDEV:   return DT_CHR;
        case EXT2_FT_BLKDEV:   return DT_BLK;
        case EXT2_FT_FIFO:     return DT_FIFO;
        case EXT2_FT_SOCK:     return DT_SOCK;
        case EXT2_FT_SYMLINK:  return DT_LNK;
        default:               return DT_UNKNOWN;
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
    block_buf = kmalloc(fs->block_size);
    if (!block_buf) return -ENOMEM;

    if (ext2_read_block(fs, block_num, block_buf) != fs->block_size) {
        kfree(block_buf, fs->block_size);
        return -EIO;
    }

    for (uint32_t i = 0; i < entries_per_block; i++) {
        if (block_buf[i] != 0) {
            int ret = ext2_free_indirect_tree(fs, block_buf[i], depth - 1);
            if (ret != 0) {
                kfree(block_buf, fs->block_size);
                return ret;
            }
        }
    }

    kfree(block_buf, fs->block_size);
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
        
        while (block_off + 8 <= fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + block_off);
            
            if (de->rec_len < 8 || block_off + de->rec_len > fs->block_size) break;
            
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
    if (ext2_alloc_inode_block(fs, &ctx->inode, new_block_idx, indirect, dindirect, tindirect) != 0) {
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
    ext2_write_inode(fs, ctx->inode_num, &ctx->inode);

cleanup:
    mutex_unlock(&ctx->lock);
    return result;
}

// Implement ext2_link
int ext2_link(fs_node_t *parent, fs_node_t *source, const char *name) {
    if (!parent || !source || !name || !name[0]) return -EINVAL;
    if ((parent->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
    if ((source->flags & 0x7) == FS_DIRECTORY) return -EPERM; // POSIX doesn't allow hard links to directories

    ext2_node_t *dir_ctx = (ext2_node_t *)(uintptr_t)parent->impl;
    ext2_node_t *source_ctx = (ext2_node_t *)(uintptr_t)source->impl;
    ext2_fs_t *fs = dir_ctx->fs;

    if (ext2_finddir(parent, (char *)name) != NULL) return -EEXIST;

    // Increment links_count
    source_ctx->inode.i_links_count++;
    source_ctx->inode.i_ctime = (uint32_t)get_time();
    if (ext2_write_inode(fs, source_ctx->inode_num, &source_ctx->inode) != 0) {
        source_ctx->inode.i_links_count--;
        return -EIO;
    }

    // Add entry to directory
    uint8_t file_type = EXT2_FT_REG_FILE;
    uint32_t s_flags = source->flags & 0x7;
    if (s_flags == FS_CHARDEVICE) file_type = EXT2_FT_CHRDEV;
    else if (s_flags == FS_BLOCKDEVICE) file_type = EXT2_FT_BLKDEV;
    else if (s_flags == FS_PIPE) file_type = EXT2_FT_FIFO;
    else if (s_flags == FS_SYMLINK) file_type = EXT2_FT_SYMLINK;

    if (ext2_add_entry(parent, name, source_ctx->inode_num, file_type) != 0) {
        source_ctx->inode.i_links_count--;
        ext2_write_inode(fs, source_ctx->inode_num, &source_ctx->inode);
        return -EIO;
    }

    return 0;
}

// Implement ext2_rename
int ext2_rename(fs_node_t *old_parent, const char *old_name, fs_node_t *new_parent, const char *new_name) {
    if (!old_parent || !old_name || !new_parent || !new_name) return -EINVAL;
    
    fs_node_t *old_node = ext2_finddir(old_parent, (char *)old_name);
    if (!old_node) return -ENOENT;

    // Check if new path exists and handle replacement
    fs_node_t *new_node = ext2_finddir(new_parent, (char *)new_name);
    if (new_node) {
        if ((old_node->flags & 0x7) == FS_DIRECTORY) {
            if ((new_node->flags & 0x7) != FS_DIRECTORY) return -ENOTDIR;
            if (!ext2_dir_is_empty(new_node)) return -ENOTEMPTY;
            ext2_rmdir(new_parent, new_name);
        } else {
            if ((new_node->flags & 0x7) == FS_DIRECTORY) return -EISDIR;
            ext2_unlink(new_parent, new_name);
        }
    }

    ext2_node_t *old_node_ctx = (ext2_node_t *)(uintptr_t)old_node->impl;
    uint8_t file_type = EXT2_FT_REG_FILE;
    uint32_t flags = old_node->flags & 0x7;
    if (flags == FS_DIRECTORY) file_type = EXT2_FT_DIR;
    else if (flags == FS_SYMLINK) file_type = EXT2_FT_SYMLINK;
    else if (flags == FS_CHARDEVICE) file_type = EXT2_FT_CHRDEV;
    else if (flags == FS_BLOCKDEVICE) file_type = EXT2_FT_BLKDEV;
    else if (flags == FS_PIPE) file_type = EXT2_FT_FIFO;

    // Add new entry
    if (ext2_add_entry(new_parent, new_name, old_node_ctx->inode_num, file_type) != 0) {
        return -EIO;
    }

    // Remove old entry
    if (ext2_remove_entry(old_parent, old_name) != 0) {
        ext2_remove_entry(new_parent, new_name); // Try to roll back
        return -EIO;
    }

    // Update parent link counts if it's a directory moving to a different parent
    if ((old_node->flags & 0x7) == FS_DIRECTORY && old_parent != new_parent) {
        ext2_node_t *old_p_ctx = (ext2_node_t *)(uintptr_t)old_parent->impl;
        ext2_node_t *new_p_ctx = (ext2_node_t *)(uintptr_t)new_parent->impl;
        ext2_fs_t *fs = old_node_ctx->fs;
        
        old_p_ctx->inode.i_links_count--;
        ext2_write_inode(fs, old_p_ctx->inode_num, &old_p_ctx->inode);
        
        new_p_ctx->inode.i_links_count++;
        ext2_write_inode(fs, new_p_ctx->inode_num, &new_p_ctx->inode);

        // Update ".." in the moved directory
        if (!old_node_ctx->indirect_buf) old_node_ctx->indirect_buf = kmalloc(fs->block_size);
        if (!old_node_ctx->dindirect_buf) old_node_ctx->dindirect_buf = kmalloc(fs->block_size);
        if (!old_node_ctx->tindirect_buf) old_node_ctx->tindirect_buf = kmalloc(fs->block_size);
        if (!old_node_ctx->block_buf) old_node_ctx->block_buf = kmalloc(fs->block_size);
        
        if (old_node_ctx->block_buf) {
            uint32_t dotdot_block = ext2_get_block_num(fs, &old_node_ctx->inode, 0, 
                                                       old_node_ctx->indirect_buf, 
                                                       old_node_ctx->dindirect_buf, 
                                                       old_node_ctx->tindirect_buf);
            if (dotdot_block != 0) {
                ext2_read_block(fs, dotdot_block, old_node_ctx->block_buf);
                ext2_dirent_t *dot = (ext2_dirent_t *)old_node_ctx->block_buf;
                ext2_dirent_t *dotdot = (ext2_dirent_t *)(old_node_ctx->block_buf + dot->rec_len);
                if (dotdot->name_len == 2 && dotdot->name[0] == '.' && dotdot->name[1] == '.') {
                    dotdot->inode = new_p_ctx->inode_num;
                    ext2_write_block(fs, dotdot_block, old_node_ctx->block_buf);
                }
            }
        }
    }

    return 0;
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
        
        while (block_off + 8 <= fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(block_buf + block_off);
            
            if (de->rec_len < 8 || block_off + de->rec_len > fs->block_size) break;
            
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
        ext2_node_close(lnode);
    }

    if (ext2_add_entry(dir, name, inode_num, EXT2_FT_SYMLINK) != 0) {
        ext2_free_inode(fs, inode_num, 0);
        return -EIO;
    }

    return 0;
}

int ext2_mkdir(fs_node_t *dir, const char *name, uint16_t permission) {
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

int ext2_unlink(fs_node_t *dir, const char *name) {
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

int ext2_rmdir(fs_node_t *dir, const char *name) {
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

int ext2_statfs(fs_node_t *node, struct statfs *buf) {
    if (!node || !buf) return -EINVAL;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx) return -EINVAL;
    ext2_fs_t *fs = ctx->fs;

    buf->f_type = EXT2_SUPER_MAGIC;
    buf->f_bsize = fs->block_size;
    buf->f_iosize = fs->block_size;
    buf->f_blocks = fs->sb.s_blocks_count;
    buf->f_bfree = fs->sb.s_free_blocks_count;
    buf->f_bavail = fs->sb.s_free_blocks_count;
    buf->f_files = fs->sb.s_inodes_count;
    buf->f_ffree = fs->sb.s_free_inodes_count;
    buf->f_fsid = 0;
    buf->f_owner = 0;
    buf->f_flags = 0;
    buf->f_syncwrites = 0;
    buf->f_asyncwrites = 0;
    
    strncpy(buf->f_fstypename, "ext2", sizeof(buf->f_fstypename));
    memset(buf->f_mntonname, 0, sizeof(buf->f_mntonname));
    memset(buf->f_mntfromname, 0, sizeof(buf->f_mntfromname));

    return 0;
}
int ext2_unmount(fs_node_t *node) {
    if (!node) return -EINVAL;
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    if (!ctx) return -EINVAL;
    ext2_fs_t *fs = ctx->fs;
    if (!fs) return -EINVAL;

    // Free all active cached nodes for this fs
    for (int i = 0; i < EXT2_NODE_CACHE_SIZE; i++) {
        if (ext2_node_cache[i].fs == fs) {
            uint32_t old_block_size = fs->block_size;
            if (ext2_node_cache[i].block_buf) kfree(ext2_node_cache[i].block_buf, old_block_size);
            if (ext2_node_cache[i].indirect_buf) kfree(ext2_node_cache[i].indirect_buf, old_block_size);
            if (ext2_node_cache[i].dindirect_buf) kfree(ext2_node_cache[i].dindirect_buf, old_block_size);
            if (ext2_node_cache[i].tindirect_buf) kfree(ext2_node_cache[i].tindirect_buf, old_block_size);
            memset(&ext2_node_cache[i], 0, sizeof(ext2_node_t));
            memset(&ext2_fs_node_cache[i], 0, sizeof(fs_node_t));
        }
    }

    if (fs->active_bg_bitmap) kfree(fs->active_bg_bitmap, fs->block_size);
    if (fs->active_inode_bg_bitmap) kfree(fs->active_inode_bg_bitmap, fs->block_size);
    if (fs->bgd) kfree(fs->bgd, fs->group_count * sizeof(ext2_group_desc_t));

    /* Drop every cached buffer for this device — the fs_node may be
     * reused for a different filesystem after unmount. */
    bio_dev_purge(fs->device);

    kfree(fs, sizeof(ext2_fs_t));
    return 0;
}
