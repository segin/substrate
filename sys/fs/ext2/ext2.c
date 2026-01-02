#include "ext2.h"
#include "../../drivers/video/vga.h"
#include "../../vfs/vfs.h"
#include "../../kern/console.h"
#include <string.h>

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
static uint32_t ext2_file_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
static struct dirent *ext2_readdir(fs_node_t *node, uint32_t index);
static fs_node_t *ext2_finddir(fs_node_t *node, char *name);
static int ext2_readlink_fn(fs_node_t *node, char *buf, size_t size);

// Read a block from the device
uint32_t ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buffer) {
    if (!fs || !fs->device || !fs->device->read) return 0;
    uint32_t offset = block_num * fs->block_size;
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
    static uint8_t block_buf[4096]; // Max block size
    if (ext2_read_block(fs, inode_table_block + block_offset, block_buf) != fs->block_size) {
        return -1;
    }
    
    memcpy(inode, block_buf + inode_offset, sizeof(ext2_inode_t));
    return 0;
}

// Get block number for a given file block index (handles indirect blocks)
static uint32_t ext2_get_block_num(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t block_idx) {
    uint32_t ptrs_per_block = fs->block_size / 4;
    static uint32_t indirect_buf[1024];
    static uint32_t dindirect_buf[1024];
    
    // Direct blocks (0-11)
    if (block_idx < 12) {
        return inode->i_block[block_idx];
    }
    block_idx -= 12;
    
    // Indirect block (12)
    if (block_idx < ptrs_per_block) {
        if (inode->i_block[12] == 0) return 0;
        ext2_read_block(fs, inode->i_block[12], indirect_buf);
        return indirect_buf[block_idx];
    }
    block_idx -= ptrs_per_block;
    
    // Double indirect block (13)
    if (block_idx < ptrs_per_block * ptrs_per_block) {
        if (inode->i_block[13] == 0) return 0;
        ext2_read_block(fs, inode->i_block[13], dindirect_buf);
        uint32_t indirect_idx = block_idx / ptrs_per_block;
        uint32_t direct_idx = block_idx % ptrs_per_block;
        if (dindirect_buf[indirect_idx] == 0) return 0;
        ext2_read_block(fs, dindirect_buf[indirect_idx], indirect_buf);
        return indirect_buf[direct_idx];
    }
    
    // Triple indirect not implemented (files > 4GB+)
    return 0;
}

// Read data from an inode at a given offset
uint32_t ext2_inode_read(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t offset, uint32_t size, void *buffer) {
    if (offset >= inode->i_size) return 0;
    if (offset + size > inode->i_size) size = inode->i_size - offset;
    
    static uint8_t block_buf[4096];
    uint8_t *buf = (uint8_t *)buffer;
    uint32_t total_read = 0;
    
    while (size > 0) {
        uint32_t block_idx = offset / fs->block_size;
        uint32_t block_offset = offset % fs->block_size;
        uint32_t block_num = ext2_get_block_num(fs, inode, block_idx);
        
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
    
    return total_read;
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
    } else if (type == EXT2_S_IFLNK) {
        node->flags = FS_SYMLINK;
        node->readlink = ext2_readlink_fn;
    } else if (type == EXT2_S_IFCHR) {
        node->flags = FS_CHARDEVICE;
    } else if (type == EXT2_S_IFBLK) {
        node->flags = FS_BLOCKDEVICE;
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
static uint32_t ext2_file_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    return ext2_inode_read(ctx->fs, &ctx->inode, offset, size, buffer);
}

// Directory iteration state
static struct dirent ext2_dirent;
static uint8_t ext2_dir_buf[4096];

// Read directory entry at index
static struct dirent *ext2_readdir(fs_node_t *node, uint32_t index) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_fs_t *fs = ctx->fs;
    
    // Read the directory data
    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = 0;
    uint32_t cur_idx = 0;
    
    while (pos < dir_size) {
        // Read block containing current position
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t block_num = ext2_get_block_num(fs, &ctx->inode, block_idx);
        
        if (block_num == 0) break;
        ext2_read_block(fs, block_num, ext2_dir_buf);
        
        // Parse entries in this block
        while (block_off < fs->block_size && pos < dir_size) {
            ext2_dirent_t *de = (ext2_dirent_t *)(ext2_dir_buf + block_off);
            
            if (de->inode != 0 && de->name_len > 0) {
                if (cur_idx == index) {
                    // Found it
                    ext2_dirent.ino = de->inode;
                    memcpy(ext2_dirent.name, de->name, de->name_len);
                    ext2_dirent.name[de->name_len] = '\0';
                    return &ext2_dirent;
                }
                cur_idx++;
            }
            
            if (de->rec_len == 0) break; // Prevent infinite loop
            block_off += de->rec_len;
            pos += de->rec_len;
        }
    }
    
    return NULL;
}

// Find entry by name in directory
static fs_node_t *ext2_finddir(fs_node_t *node, char *name) {
    ext2_node_t *ctx = (ext2_node_t *)(uintptr_t)node->impl;
    ext2_fs_t *fs = ctx->fs;
    
    uint32_t dir_size = ctx->inode.i_size;
    uint32_t pos = 0;
    
    while (pos < dir_size) {
        uint32_t block_idx = pos / fs->block_size;
        uint32_t block_off = pos % fs->block_size;
        uint32_t block_num = ext2_get_block_num(fs, &ctx->inode, block_idx);
        
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
                        fs_node_t *result = ext2_alloc_node(fs, de->inode, &inode);
                        // Copy name
                        memcpy(result->name, de->name, de->name_len);
                        result->name[de->name_len] = '\0';
                        return result;
                    }
                }
            }
            
            if (de->rec_len == 0) break;
            block_off += de->rec_len;
            pos += de->rec_len;
        }
    }
    
    return NULL;
}

// Mount ext2 filesystem
static fs_node_t *ext2_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags;
    
    fs_node_t *dev = (fs_node_t *)data;
    if (!dev || !dev->read) {
        vga_write("EXT2: No device or read function\n", 33);
        return NULL;
    }
    
    // Read superblock (at offset 1024)
    uint8_t sb_buf[1024];
    uint32_t read = dev->read(dev, 1024, 1024, sb_buf);
    if (read != 1024) {
        vga_write("EXT2: Failed to read superblock\n", 32);
        return NULL;
    }
    
    memcpy(&ext2_fs.sb, sb_buf, sizeof(ext2_superblock_t));
    
    if (ext2_fs.sb.s_magic != EXT2_SUPER_MAGIC) {
        vga_write("EXT2: Invalid magic number\n", 27);
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
        vga_write("EXT2: Failed to read root inode\n", 32);
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
    
    vga_write("EXT2: Mounted successfully\n", 27);
    return &ext2_root;
}

static filesystem_t ext2_filesystem = {
    .name = "ext2",
    .mount = ext2_mount,
};

void ext2_init(void) {
    vga_write("Initializing EXT2 Driver...\n", 28);
    vfs_register_filesystem(&ext2_filesystem);
}

// Stubs for allocation (not implemented yet)
uint32_t ext2_alloc_block(void) { return 0; }
void ext2_free_block(uint32_t block) { (void)block; }
uint32_t ext2_alloc_inode(void) { return 0; }
void ext2_free_inode(uint32_t inode) { (void)inode; }
int ext2_add_entry(fs_node_t *dir, const char *name, uint32_t inode) { 
    (void)dir; (void)name; (void)inode; return -1; 
}
int ext2_remove_entry(fs_node_t *dir, const char *name) { 
    (void)dir; (void)name; return -1; 
}
