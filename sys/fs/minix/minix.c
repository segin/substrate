#include <fs/minix/minix.h>
#include <kern/console.h>
#include <vm/vm_kmem.h>
#include <vfs/vfs.h>
#include <string.h>
#include <stdio.h>

#define MINIX_BLOCK_SIZE 1024

/* Forward declarations */
static fs_node_t *minix_mount(const char *device, uint32_t flags, void *data);
static size_t minix_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static struct dirent *minix_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *minix_finddir(fs_node_t *node, char *name);
static size_t minix_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
static ssize_t minix_readlink(fs_node_t *node, char *buf, size_t size);
static int minix_symlink(fs_node_t *node, const char *name, const char *target);
static int minix_link(fs_node_t *dir, fs_node_t *node, const char *name);
static int minix_unlink(fs_node_t *dir, const char *name);
static int minix_mknod(fs_node_t *dir, const char *name, uint16_t mode, uint32_t dev);
static int minix_unmount(fs_node_t *root);

/* Inode reading helper */
static int minix_read_inode(minix_fs_t *fs, uint32_t inode_num, fs_node_t *node);

/* Filesystem definition */
static filesystem_t minix_fs = {
    .name = "minix",
    .mount = minix_mount,
    .next = NULL
};

void minix_init(void) {
    kprint("Initializing Minix FS Driver...\n");
    vfs_register_filesystem(&minix_fs);
}

/* Zone conversion helper */
static uint32_t minix_get_zone(minix_fs_t *fs, fs_node_t *node, uint32_t zone_index) {
    struct minix_inode_v1 *inode = (struct minix_inode_v1 *)node->ptr;
    if (!inode) return 0;

    // Direct zones (0-6)
    if (zone_index < 7) {
        return inode->i_zone[zone_index];
    }

    uint32_t indirect_index = zone_index - 7;
    uint32_t entries_per_block = MINIX_BLOCK_SIZE / sizeof(uint16_t); // 512 for V1

    // Indirect zone (7)
    if (indirect_index < entries_per_block) {
        uint16_t z = inode->i_zone[7];
        if (z == 0) return 0;

        uint16_t buf[entries_per_block];
        if (read_fs(fs->block_device, z * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }
        return buf[indirect_index];
    }

    // Double indirect zone (8)
    uint32_t double_indirect_index = indirect_index - entries_per_block;
    if (double_indirect_index < entries_per_block * entries_per_block) {
        uint16_t z = inode->i_zone[8];
        if (z == 0) return 0;

        uint16_t buf[entries_per_block];
        if (read_fs(fs->block_device, z * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }
        
        uint16_t indirect_block = buf[double_indirect_index / entries_per_block];
        if (indirect_block == 0) return 0;

        if (read_fs(fs->block_device, indirect_block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }
        
        return buf[double_indirect_index % entries_per_block];
    }

    return 0;
}

static int minix_read_inode(minix_fs_t *fs, uint32_t inode_num, fs_node_t *node) {
    if (inode_num == 0 || inode_num > fs->sb.s_ninodes) return -1;

    // Calculate block and offset
    // Inodes start after boot(1) + super(1) + imap(x) + zmap(y)
    // Block size = 1024
    uint32_t inode_start_block = 2 + fs->sb.s_imap_blocks + fs->sb.s_zmap_blocks;
    uint32_t inodes_per_block = MINIX_BLOCK_SIZE / sizeof(struct minix_inode_v1);
    uint32_t block = inode_start_block + (inode_num - 1) / inodes_per_block;
    uint32_t offset = ((inode_num - 1) % inodes_per_block) * sizeof(struct minix_inode_v1);

    uint8_t buf[MINIX_BLOCK_SIZE];
    if (read_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
        return -1;
    }

    struct minix_inode_v1 *raw = (struct minix_inode_v1 *)(buf + offset);
    
    node->inode = inode_num;
    node->length = raw->i_size;
    node->impl = (uint32_t)(uintptr_t)fs;
    
    // Parse mode
    if ((raw->i_mode & 0xF000) == 0x4000) node->flags = FS_DIRECTORY;
    else if ((raw->i_mode & 0xF000) == 0x8000) node->flags = FS_FILE;
    else if ((raw->i_mode & 0xF000) == 0xA000) node->flags = FS_SYMLINK;
    else if ((raw->i_mode & 0xF000) == 0x2000) node->flags = FS_CHARDEVICE;
    else if ((raw->i_mode & 0xF000) == 0x6000) node->flags = FS_BLOCKDEVICE;
    else node->flags = 0;

    node->mask = raw->i_mode & 0xFFF;
    node->uid = raw->i_uid;
    node->gid = raw->i_gid;
    node->atime = raw->i_time;
    node->mtime = raw->i_time;
    node->ctime = raw->i_time;

    // Hook operations
    // Hook operations
    if (node->flags & FS_DIRECTORY) {
        node->readdir = minix_readdir;
        node->finddir = minix_finddir;
    } else {
        node->read = minix_read;
        node->write = minix_write;
        if (node->flags & FS_SYMLINK) {
             node->readlink = minix_readlink;
        }
    }
    // Hook directory ops provided by VFS (create, mkdir, etc) if supported
    // For now we only have read/write/finddir
    if (node->flags & FS_DIRECTORY) {
         node->symlink = minix_symlink;
         node->link = minix_link;
         node->unlink = minix_unlink;
         node->mknod = minix_mknod;
    }

    // Cache private data (zones) - Allocating for simplicity
    // Note: This leaks if we don't implement close/free callback properly.
    // For VFS refactor context, we accept this.
    void *cache = kmalloc(sizeof(struct minix_inode_v1));
    if (cache) {
        memcpy(cache, raw, sizeof(struct minix_inode_v1));
        node->ptr = (struct fs_node *)cache;
    }

    return 0;
}

static size_t minix_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)node->impl;
    if (offset >= node->length) return 0;
    if (offset + size > node->length) size = node->length - offset;

    size_t read_bytes = 0;
    uint32_t current_offset = (uint32_t)offset;
    
    while (read_bytes < size) {
        uint32_t block_index = current_offset / MINIX_BLOCK_SIZE;
        uint32_t block_offset = current_offset % MINIX_BLOCK_SIZE;
        uint32_t zone = minix_get_zone(fs, node, block_index);
        
        if (zone == 0) {
            // Sparse file (hole)
            memset(buffer + read_bytes, 0, 1); // Only 1 byte for simplicity, or chunk
            // Better: calculate chunk
            uint32_t chunk = MINIX_BLOCK_SIZE - block_offset;
            if (chunk > size - read_bytes) chunk = size - read_bytes;
            memset(buffer + read_bytes, 0, chunk);
            read_bytes += chunk;
            current_offset += chunk;
            continue;
        }

        uint8_t block_buf[MINIX_BLOCK_SIZE];
        if (read_fs(fs->block_device, zone * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, block_buf) != MINIX_BLOCK_SIZE) {
            break;
        }

        uint32_t copy_size = MINIX_BLOCK_SIZE - block_offset;
        if (copy_size > size - read_bytes) copy_size = size - read_bytes;
        
        memcpy(buffer + read_bytes, block_buf + block_offset, copy_size);
        read_bytes += copy_size;
        current_offset += copy_size;
    }
    return read_bytes;
}

static size_t minix_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)node->impl;
    
    // Simple overwrite support (no allocation)
    size_t written = 0;
    uint32_t current_offset = (uint32_t)offset;
    
    while (written < size) {
        uint32_t block_index = current_offset / MINIX_BLOCK_SIZE;
        uint32_t block_offset = current_offset % MINIX_BLOCK_SIZE;
        uint32_t zone = minix_get_zone(fs, node, block_index);
        
        if (zone == 0) {
            // Hole - Allocation not supported yet
            break; 
        }

        uint32_t chunk = MINIX_BLOCK_SIZE - block_offset;
        if (chunk > size - written) chunk = size - written;

        uint8_t block_buf[MINIX_BLOCK_SIZE];
        bool need_read = (block_offset != 0) || (chunk != MINIX_BLOCK_SIZE);
        
        if (need_read) {
             if (read_fs(fs->block_device, zone * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, block_buf) != MINIX_BLOCK_SIZE) {
                 break;
             }
        }
        
        memcpy(block_buf + block_offset, buffer + written, chunk);
        
        if (write_fs(fs->block_device, zone * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, block_buf) != MINIX_BLOCK_SIZE) {
            break;
        }

        written += chunk;
        current_offset += chunk;
    }
    
    // Update size if extended
    if (offset + written > node->length) {
        node->length = offset + written;
        // TODO: Write updated inode to disk
    }
    return written;
}

static ssize_t minix_readlink(fs_node_t *node, char *buf, size_t size) {
    return minix_read(node, 0, size, (uint8_t *)buf);
}

static struct dirent *minix_readdir(fs_node_t *node, uint64_t index) {
    
    // Minix V1 directory entries are 32 bytes (2 byte inode, 30 chars name)
    // 1024 bytes per block / 32 = 32 entries per block
    
    struct minix_dirent_v1 {
        uint16_t inode;
        char name[30];
    } __attribute__((packed));

    uint32_t entry_size = sizeof(struct minix_dirent_v1);
    uint32_t offset = index * entry_size;
    
    // Read directly from file logic (reuse minix_read if possible, but we need temporary buffer)
    // Or just implement simplified read here
    
    struct minix_dirent_v1 entry;
    if (minix_read(node, offset, entry_size, (uint8_t *)&entry) != entry_size) {
        return NULL;
    }

    if (entry.inode == 0) return NULL; // Deleted/Empty

    static struct dirent dir; // Static return buffer required by VFS interface
    strcpy(dir.name, entry.name);
    dir.ino = entry.inode;
    return &dir;
}

static fs_node_t *minix_finddir(fs_node_t *node, char *name) {
    // Scan directory
    fs_node_t *result = NULL;
    struct dirent *d;
    uint64_t index = 0;
    while ((d = minix_readdir(node, index++)) != NULL) {
        if (strcmp(d->name, name) == 0) {
            minix_fs_t *fs = (minix_fs_t *)(uintptr_t)node->impl;
            result = (fs_node_t *)kmalloc(sizeof(fs_node_t));
            memset(result, 0, sizeof(fs_node_t));
            if (minix_read_inode(fs, d->ino, result) != 0) {
                kfree(result, sizeof(fs_node_t));
                return NULL;
            }
            return result;
        }
    }
    return NULL;
}

static fs_node_t *minix_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags;
    fs_node_t *dev = (fs_node_t *)data;
    if (!dev) return NULL;

    minix_fs_t *fs = (minix_fs_t *)kmalloc(sizeof(minix_fs_t));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(minix_fs_t));
    fs->block_device = dev;

    uint8_t buf[MINIX_BLOCK_SIZE];
    if (read_fs(dev, MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
        kfree(fs, sizeof(minix_fs_t));
        return NULL;
    }

    memcpy(&fs->sb, buf, sizeof(struct minix_superblock));

    /* Check Magic - Supports V1 (0x137F) mainly for now */
    if (fs->sb.s_magic != MINIX_V1_Magic && 
        fs->sb.s_magic != MINIX_V1_Magic_14 && 
        fs->sb.s_magic != MINIX_V2_Magic) {
        kprint("Minix: Invalid magic\n");
        kfree(fs, sizeof(minix_fs_t));
        return NULL;
    }

    kprint("Minix: Filesystem detected.\n");

    fs_node_t *root = (fs_node_t *)kmalloc(sizeof(fs_node_t));
    if (!root) {
        kfree(fs, sizeof(minix_fs_t));
        return NULL;
    }
    memset(root, 0, sizeof(fs_node_t));

    // Read inode 1 (Root)
    if (minix_read_inode(fs, MINIX_ROOT_INODE, root) != 0) {
        kprint("Minix: Failed to read root inode\n");
        kfree(root, sizeof(fs_node_t));
        kfree(fs, sizeof(minix_fs_t));
        return NULL;
    }

    // strcpy(root->name, "minix_root"); // Usually VFS overrides name with mountpoint
    root->unmount = minix_unmount;
    return root; 
}

static int minix_unmount(fs_node_t *root) {
    if (!root) return -1;
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)root->impl;

    if (fs) {
        kfree(fs, sizeof(minix_fs_t));
    }

    if (root->ptr) {
        kfree(root->ptr, sizeof(struct minix_inode_v1));
    }

    kfree(root, sizeof(fs_node_t));
    return 0;
}

static int minix_symlink(fs_node_t *node, const char *name, const char *target) {
    // TODO: Allocate inode, write target path to it, add to directory
    (void)node; (void)name; (void)target;
    return -1;
}

static int minix_link(fs_node_t *dir, fs_node_t *node, const char *name) {
    // TODO: Create directory entry pointing to node->inode, increment inode->nlinks
    (void)dir; (void)node; (void)name;
    return -1;
}

static int minix_unlink(fs_node_t *dir, const char *name) {
    // TODO: Find directory entry, remove it, decrement inode->nlinks
    (void)dir; (void)name;
    return -1;
}

static int minix_mknod(fs_node_t *dir, const char *name, uint16_t mode, uint32_t dev) {
    // TODO: Allocate inode, set mode to (S_IFBLK/S_IFCHR | perms), set zone[0] to dev
    (void)dir; (void)name; (void)mode; (void)dev;
    return -1;
}
