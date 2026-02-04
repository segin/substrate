#include <fs/minix/minix.h>
#include <kern/console.h>
#include <vm/vm_kmem.h>
#include <vfs/vfs.h>
#include <sys/stat.h>
#include <kern/time.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define MINIX_BLOCK_SIZE 1024

/* Forward declarations */
static fs_node_t *minix_mount(const char *device, uint32_t flags, void *data);
static size_t minix_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static struct dirent *minix_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *minix_finddir(fs_node_t *node, char *name);
static size_t minix_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
static int minix_readlink(fs_node_t *node, char *buf, size_t size);
static int minix_symlink(fs_node_t *node, const char *name, const char *target);
static int minix_link(fs_node_t *dir, fs_node_t *node, const char *name);
static int minix_unlink(fs_node_t *dir, const char *name);
static int minix_mknod(fs_node_t *dir, const char *name, uint16_t mode, uint32_t dev);
static int minix_unmount(fs_node_t *root);

/* Inode reading helper */
static int minix_read_inode(minix_fs_t *fs, uint32_t inode_num, fs_node_t *node);
static int minix_write_inode(minix_fs_t *fs, fs_node_t *node);
static int minix_write_inode_raw(minix_fs_t *fs, uint32_t inode_num, struct minix_inode_v1 *inode);

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
static uint32_t minix_get_zone_v1(minix_fs_t *fs, struct minix_inode_v1 *inode, uint32_t zone_index) {
    // Direct zones (0-6)
    if (zone_index < 7) {
        return inode->i_zone[zone_index];
    }

    uint32_t indirect_index = zone_index - 7;
    uint32_t entries_per_block = MINIX_BLOCK_SIZE / sizeof(uint16_t); // 512

    // Indirect zone (7)
    if (indirect_index < entries_per_block) {
        uint16_t z = inode->i_zone[7];
        if (z == 0) return 0;

        uint16_t buf[MINIX_BLOCK_SIZE / 2];
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

        uint16_t buf[MINIX_BLOCK_SIZE / 2];
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

static uint32_t minix_get_zone_v2(minix_fs_t *fs, struct minix_inode_v2 *inode, uint32_t zone_index) {
    // Direct zones (0-6)
    if (zone_index < 7) {
        return inode->i_zone[zone_index];
    }

    uint32_t indirect_index = zone_index - 7;
    uint32_t entries_per_block = MINIX_BLOCK_SIZE / sizeof(uint32_t); // 256 for V2

    // Indirect zone (7)
    if (indirect_index < entries_per_block) {
        uint32_t z = inode->i_zone[7];
        if (z == 0) return 0;

        uint32_t buf[MINIX_BLOCK_SIZE / 4];
        if (read_fs(fs->block_device, z * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }
        return buf[indirect_index];
    }

    // Double indirect zone (8)
    uint32_t double_indirect_index = indirect_index - entries_per_block;
    if (double_indirect_index < entries_per_block * entries_per_block) {
        uint32_t z = inode->i_zone[8];
        if (z == 0) return 0;

        uint32_t buf[MINIX_BLOCK_SIZE / 4];
        if (read_fs(fs->block_device, z * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }

        uint32_t indirect_block = buf[double_indirect_index / entries_per_block];
        if (indirect_block == 0) return 0;

        if (read_fs(fs->block_device, indirect_block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t *)buf) != MINIX_BLOCK_SIZE) {
            return 0;
        }

        return buf[double_indirect_index % entries_per_block];
    }

    // Triple indirect zone (9) - Not supported/implemented yet for V2
    return 0;
}

static uint32_t minix_get_zone(minix_fs_t *fs, fs_node_t *node, uint32_t zone_index) {
    if (!node->ptr) return 0;

    if (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14) {
        return minix_get_zone_v2(fs, (struct minix_inode_v2 *)node->ptr, zone_index);
    } else {
        return minix_get_zone_v1(fs, (struct minix_inode_v1 *)node->ptr, zone_index);
    }
}

static uint32_t minix_alloc_zone(minix_fs_t *fs) {
    // Mock: simple counter allocator
    if (fs->last_zone_alloc == 0) fs->last_zone_alloc = fs->sb.s_firstdatazone;
    fs->last_zone_alloc++;
    uint32_t max_zones = (fs->sb.s_magic == MINIX_V1_Magic || fs->sb.s_magic == MINIX_V1_Magic_14) ? fs->sb.s_nzones : fs->sb.s_zones;
    if (fs->last_zone_alloc >= max_zones) return 0;
    return fs->last_zone_alloc;
}

static int minix_read_inode(minix_fs_t *fs, uint32_t inode_num, fs_node_t *node) {
    if (inode_num == 0 || inode_num > fs->sb.s_ninodes) return -1;

    // Calculate block and offset
    uint32_t inode_start_block = 2 + fs->sb.s_imap_blocks + fs->sb.s_zmap_blocks;

    bool v2 = (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14);
    uint32_t inode_size = v2 ? sizeof(struct minix_inode_v2) : sizeof(struct minix_inode_v1);
    uint32_t inodes_per_block = MINIX_BLOCK_SIZE / inode_size;

    uint32_t block = inode_start_block + (inode_num - 1) / inodes_per_block;
    uint32_t offset = ((inode_num - 1) % inodes_per_block) * inode_size;

    uint8_t buf[MINIX_BLOCK_SIZE];
    if (read_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
        return -1;
    }

    node->inode = inode_num;
    node->impl = (uintptr_t)fs;
    uint16_t mode;

    void *cache = kmalloc(inode_size);
    if (!cache) return -1;

    if (v2) {
        struct minix_inode_v2 *raw = (struct minix_inode_v2 *)(buf + offset);
        memcpy(cache, raw, inode_size);

        mode = raw->i_mode;
        node->length = raw->i_size;
        node->uid = raw->i_uid;
        node->gid = raw->i_gid;
        node->atime = raw->i_atime;
        node->mtime = raw->i_mtime;
        node->ctime = raw->i_ctime;
    } else {
        struct minix_inode_v1 *raw = (struct minix_inode_v1 *)(buf + offset);
        memcpy(cache, raw, inode_size);

        mode = raw->i_mode;
        node->length = raw->i_size;
        node->uid = raw->i_uid;
        node->gid = raw->i_gid;
        node->atime = raw->i_time;
        node->mtime = raw->i_time;
        node->ctime = raw->i_time;
    }
    
    node->ptr = (struct fs_node *)cache;

    // Parse mode
    if ((mode & 0xF000) == 0x4000) node->flags = FS_DIRECTORY;
    else if ((mode & 0xF000) == 0x8000) node->flags = FS_FILE;
    else if ((mode & 0xF000) == 0xA000) node->flags = FS_SYMLINK;
    else if ((mode & 0xF000) == 0x2000) {
        node->flags = FS_CHARDEVICE;
        if (v2) node->rdev = ((struct minix_inode_v2*)cache)->i_zone[0];
        else node->rdev = ((struct minix_inode_v1*)cache)->i_zone[0];
    } else if ((mode & 0xF000) == 0x6000) {
        node->flags = FS_BLOCKDEVICE;
        if (v2) node->rdev = ((struct minix_inode_v2*)cache)->i_zone[0];
        else node->rdev = ((struct minix_inode_v1*)cache)->i_zone[0];
    } else node->flags = 0;

    node->mask = mode & 0xFFF;

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
    if (node->flags & FS_DIRECTORY) {
         node->symlink = minix_symlink;
         node->link = minix_link;
         node->unlink = minix_unlink;
         node->mknod = minix_mknod;
    }

    return 0;
}

static void minix_free_block(minix_fs_t *fs, uint32_t zone) {
    if (zone == 0) return;

    uint32_t zmap_start_block = 2 + fs->sb.s_imap_blocks;
    uint32_t block_index = zone / (MINIX_BLOCK_SIZE * 8);
    uint32_t bit_offset = zone % (MINIX_BLOCK_SIZE * 8);

    if (block_index >= fs->sb.s_zmap_blocks) return;

    uint8_t buf[MINIX_BLOCK_SIZE];
    uint32_t block = zmap_start_block + block_index;

    if (read_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) return;

    buf[bit_offset / 8] &= ~(1 << (bit_offset % 8));

    write_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf);
}

static void minix_free_inode(minix_fs_t *fs, uint32_t inode_num) {
    if (inode_num == 0) return;

    uint32_t imap_start_block = 2;
    uint32_t block_index = inode_num / (MINIX_BLOCK_SIZE * 8);
    uint32_t bit_offset = inode_num % (MINIX_BLOCK_SIZE * 8);

    if (block_index >= fs->sb.s_imap_blocks) return;

    uint8_t buf[MINIX_BLOCK_SIZE];
    uint32_t block = imap_start_block + block_index;

    if (read_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) return;

    buf[bit_offset / 8] &= ~(1 << (bit_offset % 8));

    write_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf);
}

static void minix_free_all_zones(minix_fs_t *fs, fs_node_t *node) {
    if (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14) {
         struct minix_inode_v2 *inode = (struct minix_inode_v2 *)node->ptr;
         // Free V2 zones
         for (int i=0; i<7; i++) {
             if (inode->i_zone[i]) {
                 minix_free_block(fs, inode->i_zone[i]);
                 inode->i_zone[i] = 0;
             }
         }
         // Indirect
         if (inode->i_zone[7]) {
             uint32_t *buf = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
             if (buf) {
                 if (read_fs(fs->block_device, inode->i_zone[7] * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t*)buf) == MINIX_BLOCK_SIZE) {
                     for (int i=0; i<MINIX_BLOCK_SIZE/4; i++) {
                         if (buf[i]) minix_free_block(fs, buf[i]);
                     }
                 }
                 kfree(buf, MINIX_BLOCK_SIZE);
             }
             minix_free_block(fs, inode->i_zone[7]);
             inode->i_zone[7] = 0;
         }
         // Double Indirect
         if (inode->i_zone[8]) {
             uint32_t *buf = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
             if (buf) {
                 if (read_fs(fs->block_device, inode->i_zone[8] * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t*)buf) == MINIX_BLOCK_SIZE) {
                     for (int i=0; i<MINIX_BLOCK_SIZE/4; i++) {
                         if (buf[i]) {
                             uint32_t *buf2 = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
                             if (buf2) {
                                 if (read_fs(fs->block_device, buf[i] * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t*)buf2) == MINIX_BLOCK_SIZE) {
                                     for (int j=0; j<MINIX_BLOCK_SIZE/4; j++) {
                                         if (buf2[j]) minix_free_block(fs, buf2[j]);
                                     }
                                 }
                                 kfree(buf2, MINIX_BLOCK_SIZE);
                             }
                             minix_free_block(fs, buf[i]);
                         }
                     }
                 }
                 kfree(buf, MINIX_BLOCK_SIZE);
             }
             minix_free_block(fs, inode->i_zone[8]);
             inode->i_zone[8] = 0;
         }
         // Triple Indirect
         if (inode->i_zone[9]) {
             uint32_t *buf = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
             if (buf) {
                 if (read_fs(fs->block_device, inode->i_zone[9] * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t*)buf) == MINIX_BLOCK_SIZE) {
                     for (int i=0; i<MINIX_BLOCK_SIZE/4; i++) {
                         if (buf[i]) {
                             uint32_t *buf2 = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
                             if (buf2) {
                                 if (read_fs(fs->block_device, buf[i] * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t*)buf2) == MINIX_BLOCK_SIZE) {
                                     for (int j=0; j<MINIX_BLOCK_SIZE/4; j++) {
                                         if (buf2[j]) {
                                             uint32_t *buf3 = (uint32_t *)kmalloc(MINIX_BLOCK_SIZE);
                                             if (buf3) {
                                                 if (read_fs(fs->block_device, buf2[j] * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t*)buf3) == MINIX_BLOCK_SIZE) {
                                                     for (int k=0; k<MINIX_BLOCK_SIZE/4; k++) {
                                                         if (buf3[k]) minix_free_block(fs, buf3[k]);
                                                     }
                                                 }
                                                 kfree(buf3, MINIX_BLOCK_SIZE);
                                             }
                                             minix_free_block(fs, buf2[j]);
                                         }
                                     }
                                 }
                                 kfree(buf2, MINIX_BLOCK_SIZE);
                             }
                             minix_free_block(fs, buf[i]);
                         }
                     }
                 }
                 kfree(buf, MINIX_BLOCK_SIZE);
             }
             minix_free_block(fs, inode->i_zone[9]);
             inode->i_zone[9] = 0;
         }
    } else {
         struct minix_inode_v1 *inode = (struct minix_inode_v1 *)node->ptr;
         for (int i = 0; i < 7; i++) {
            if (inode->i_zone[i]) {
                minix_free_block(fs, inode->i_zone[i]);
                inode->i_zone[i] = 0;
            }
        }

        if (inode->i_zone[7]) {
            uint16_t *buf = (uint16_t *)kmalloc(MINIX_BLOCK_SIZE);
            if (buf) {
                if (read_fs(fs->block_device, inode->i_zone[7] * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t*)buf) == MINIX_BLOCK_SIZE) {
                    for (int i = 0; i < MINIX_BLOCK_SIZE / 2; i++) {
                        if (buf[i]) minix_free_block(fs, buf[i]);
                    }
                }
                kfree(buf, MINIX_BLOCK_SIZE);
            }
            minix_free_block(fs, inode->i_zone[7]);
            inode->i_zone[7] = 0;
        }

        if (inode->i_zone[8]) {
            uint16_t *buf = (uint16_t *)kmalloc(MINIX_BLOCK_SIZE);
            if (buf) {
                if (read_fs(fs->block_device, inode->i_zone[8] * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t*)buf) == MINIX_BLOCK_SIZE) {
                    for (int i = 0; i < MINIX_BLOCK_SIZE / 2; i++) {
                        if (buf[i]) {
                            uint16_t *buf2 = (uint16_t *)kmalloc(MINIX_BLOCK_SIZE);
                            if (buf2) {
                                if (read_fs(fs->block_device, buf[i] * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, (uint8_t*)buf2) == MINIX_BLOCK_SIZE) {
                                    for (int j = 0; j < MINIX_BLOCK_SIZE / 2; j++) {
                                        if (buf2[j]) minix_free_block(fs, buf2[j]);
                                    }
                                }
                                kfree(buf2, MINIX_BLOCK_SIZE);
                            }
                            minix_free_block(fs, buf[i]);
                        }
                    }
                }
                kfree(buf, MINIX_BLOCK_SIZE);
            }
            minix_free_block(fs, inode->i_zone[8]);
            inode->i_zone[8] = 0;
        }
    }
}

static size_t minix_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)node->impl;
    if (offset >= node->length) return 0;
    if (offset + (off_t)size > node->length) size = (size_t)(node->length - offset);

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
    if (offset + (off_t)written > node->length) {
        node->length = offset + written;
        minix_write_inode(fs, node);
    }
    return written;
}

static int minix_readlink(fs_node_t *node, char *buf, size_t size) {
    return (int)minix_read(node, 0, size, (uint8_t *)buf);
}

static struct dirent *minix_readdir(fs_node_t *node, uint64_t index) {
    
    // Minix V1 directory entries are 32 bytes (2 byte inode, 30 chars name)
    // 1024 bytes per block / 32 = 32 entries per block
    
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

    if (root->ptr) {
        uint32_t inode_size = sizeof(struct minix_inode_v1);
        if (fs && (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14)) {
            inode_size = sizeof(struct minix_inode_v2);
        }
        kfree(root->ptr, inode_size);
    }

    if (fs) {
        kfree(fs, sizeof(minix_fs_t));
    }

    kfree(root, sizeof(fs_node_t));
    return 0;
}

static int minix_alloc_inode(minix_fs_t *fs) {
    uint32_t total_inodes = fs->sb.s_ninodes;
    uint32_t imap_blocks = fs->sb.s_imap_blocks;
    uint32_t start_block = 2; // Boot + Super

    for (uint32_t b = 0; b < imap_blocks; b++) {
        uint8_t buf[MINIX_BLOCK_SIZE];
        if (read_fs(fs->block_device, (start_block + b) * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
            return -1;
        }

        for (uint32_t i = 0; i < MINIX_BLOCK_SIZE; i++) {
            if (buf[i] != 0xFF) {
                for (int j = 0; j < 8; j++) {
                    if (!((buf[i] >> j) & 1)) {
                        uint32_t inode = b * 8192 + i * 8 + j;
                        if (inode < 1 || inode > total_inodes) continue;

                        buf[i] |= (1 << j);
                        if (write_fs(fs->block_device, (start_block + b) * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
                            return -1;
                        }
                        return inode;
                    }
                }
            }
        }
    }
    return -1;
}


static int minix_dir_add(fs_node_t *dir, const char *name, uint32_t inode_num) {
    struct minix_dirent_v1 entry;
    memset(&entry, 0, sizeof(entry));
    entry.inode = (uint16_t)inode_num;
    strncpy(entry.name, name, 30);

    uint32_t offset = 0;
    struct minix_dirent_v1 tmp;

    while (offset < dir->length) {
        if (minix_read(dir, offset, sizeof(tmp), (uint8_t *)&tmp) != sizeof(tmp)) break;
        if (tmp.inode == 0) {
            // Found empty slot
            if (minix_write(dir, offset, sizeof(entry), (uint8_t *)&entry) != sizeof(entry)) return -1;
            return 0;
        }
        offset += sizeof(tmp);
    }

    // Append
    if (minix_write(dir, offset, sizeof(entry), (uint8_t *)&entry) != sizeof(entry)) {
        return -1;
    }

    // Update directory inode size on disk
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)dir->impl;
    struct minix_inode_v1 *cached = (struct minix_inode_v1 *)dir->ptr;
    if (cached) {
        cached->i_size = dir->length;
        minix_write_inode_raw(fs, dir->inode, cached);
    }

    return 0;
}

static int minix_mknod(fs_node_t *dir, const char *name, uint16_t mode, uint32_t dev) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)dir->impl;

    // 1. Allocate Inode
    int inode_num = minix_alloc_inode(fs);
    if (inode_num < 0) return -1;

    // 2. Prepare FS Node wrapper
    fs_node_t new_node;
    memset(&new_node, 0, sizeof(fs_node_t));
    new_node.inode = (uint64_t)inode_num;
    new_node.impl = (uintptr_t)fs;

    // Set type flags
    if (S_ISDIR(mode)) new_node.flags = FS_DIRECTORY;
    else if (S_ISCHR(mode)) new_node.flags = FS_CHARDEVICE;
    else if (S_ISBLK(mode)) new_node.flags = FS_BLOCKDEVICE;
    else if (S_ISREG(mode)) new_node.flags = FS_FILE;
    else if (S_ISFIFO(mode)) new_node.flags = FS_PIPE;
    else if (S_ISLNK(mode)) new_node.flags = FS_SYMLINK;
    else new_node.flags = 0;

    new_node.mask = mode & 0xFFF;
    new_node.uid = dir->uid;
    new_node.gid = dir->gid;
    new_node.length = 0;

    // 3. Allocate and Initialize Cached Raw Inode
    bool v2 = (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14);
    size_t inode_size = v2 ? sizeof(struct minix_inode_v2) : sizeof(struct minix_inode_v1);

    void *raw_inode = kmalloc(inode_size);
    if (!raw_inode) {
        // TODO: Free allocated inode number
        return -1;
    }
    memset(raw_inode, 0, inode_size);

    // Initialize specific fields in raw inode that minix_write_inode preserves
    if (v2) {
        struct minix_inode_v2 *v2_inode = (struct minix_inode_v2 *)raw_inode;
        v2_inode->i_nlinks = 1;
        v2_inode->i_zone[0] = dev;
    } else {
        struct minix_inode_v1 *v1_inode = (struct minix_inode_v1 *)raw_inode;
        v1_inode->i_nlinks = 1;
        v1_inode->i_zone[0] = (uint16_t)dev;
    }

    new_node.ptr = (struct fs_node *)raw_inode;

    // 4. Write Inode to Disk (using V1/V2 aware helper)
    if (minix_write_inode(fs, &new_node) != 0) {
        kfree(raw_inode, inode_size);
        return -1;
    }

    kfree(raw_inode, inode_size);

    // 5. Add to Directory
    if (minix_dir_add(dir, name, inode_num) != 0) {
        return -1;
    }

    return 0;
}

static int minix_symlink(fs_node_t *dir, const char *name, const char *target) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)dir->impl;

    // 1. Allocate Inode
    int inode_num_int = minix_alloc_inode(fs);
    if (inode_num_int < 0) return -1;
    uint32_t inode_num = (uint32_t)inode_num_int;

    // 2. Initialize Inode
    fs_node_t new_node;
    memset(&new_node, 0, sizeof(fs_node_t));
    new_node.inode = inode_num;
    new_node.impl = (uintptr_t)fs;
    new_node.flags = FS_SYMLINK;
    new_node.mask = 0777;
    new_node.uid = dir->uid;
    new_node.gid = dir->gid;
    new_node.length = 0;

    bool v2 = (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14);
    uint32_t inode_size = v2 ? sizeof(struct minix_inode_v2) : sizeof(struct minix_inode_v1);

    new_node.ptr = kmalloc(inode_size);
    if (!new_node.ptr) return -1;
    memset(new_node.ptr, 0, inode_size);
    if (v2) ((struct minix_inode_v2 *)new_node.ptr)->i_nlinks = 1;
    else ((struct minix_inode_v1 *)new_node.ptr)->i_nlinks = 1;

    // 3. Allocate Zone and Write Target
    uint32_t zone = minix_alloc_zone(fs);
    if (zone == 0) {
        kfree(new_node.ptr, inode_size);
        return -1;
    }

    if (v2) ((struct minix_inode_v2 *)new_node.ptr)->i_zone[0] = zone;
    else ((struct minix_inode_v1 *)new_node.ptr)->i_zone[0] = zone;

    size_t target_len = strlen(target);
    // Note: minix_write handles updating new_node.length but we need to ensure it uses the correct zone we just set.
    // minix_write uses minix_get_zone which uses new_node.ptr. So it should work.

    if (minix_write(&new_node, 0, target_len, (uint8_t *)target) != target_len) {
        kfree(new_node.ptr, inode_size);
        return -1;
    }

    if (minix_write_inode(fs, &new_node) != 0) {
        kfree(new_node.ptr, inode_size);
        return -1;
    }

    kfree(new_node.ptr, inode_size);

    // 4. Add to directory
    if (minix_dir_add(dir, name, (uint16_t)inode_num) != 0) {
        return -1;
    }

    return 0;
}

static int minix_link(fs_node_t *dir, fs_node_t *node, const char *name) {
    if (!dir || !node || !name) return -1;
    if (strlen(name) > 30) return -1;

    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)dir->impl;

    struct minix_dirent_v1 {
        uint16_t inode;
        char name[30];
    } __attribute__((packed));

    // 1. Find a free directory entry
    uint32_t offset = 0;
    uint32_t dir_size = dir->length;
    uint32_t free_offset = 0;
    bool found = false;

    uint8_t buf[sizeof(struct minix_dirent_v1)];

    while (offset < dir_size) {
        if (minix_read(dir, offset, sizeof(struct minix_dirent_v1), buf) != sizeof(struct minix_dirent_v1)) {
            break;
        }
        struct minix_dirent_v1 *d = (struct minix_dirent_v1 *)buf;
        if (d->inode == 0) {
            free_offset = offset;
            found = true;
            break;
        }
        offset += sizeof(struct minix_dirent_v1);
    }

    if (!found) {
        // Append to directory
        free_offset = dir_size;
    }

    // 2. Increment link count
    struct minix_inode_v1 *inode = (struct minix_inode_v1 *)node->ptr;
    if (!inode) return -1;

    if (inode->i_nlinks == 255) return -1;

    inode->i_nlinks++;
    if (minix_write_inode(fs, node) != 0) {
        inode->i_nlinks--;
        return -1;
    }

    // 3. Write directory entry
    struct minix_dirent_v1 entry;
    entry.inode = node->inode;
    strncpy(entry.name, name, 30);

    if (minix_write(dir, free_offset, sizeof(struct minix_dirent_v1), (uint8_t *)&entry) != sizeof(struct minix_dirent_v1)) {
        // Rollback
        inode->i_nlinks--;
        minix_write_inode(fs, node);
        return -1;
    }

    // Update directory inode (size)
    struct minix_inode_v1 *dir_inode = (struct minix_inode_v1 *)dir->ptr;
    if (dir_inode) {
        dir_inode->i_size = dir->length;
    }
    
    // Update mtime/ctime
    extern int64_t get_time(void);
    dir->mtime = get_time();
    dir->ctime = dir->mtime;
    
    // Persist changes
    minix_write_inode(fs, dir);


    return 0;
}

static int minix_unlink(fs_node_t *dir, const char *name) {
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)dir->impl;
    struct minix_dirent_v1 {
        uint16_t inode;
        char name[30];
    } __attribute__((packed)) entry;

    uint32_t offset = 0;
    bool v2 = (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14);
    size_t inode_size = v2 ? sizeof(struct minix_inode_v2) : sizeof(struct minix_inode_v1);

    while (offset < dir->length) {
        if (minix_read(dir, offset, sizeof(entry), (uint8_t *)&entry) != sizeof(entry)) {
            return -1;
        }

        if (entry.inode != 0 && strncmp(entry.name, name, 30) == 0) {
            uint32_t target_inode_num = entry.inode;

            // Read target inode FIRST
            fs_node_t target;
            memset(&target, 0, sizeof(target));
            if (minix_read_inode(fs, target_inode_num, &target) != 0) {
                return -1;
            }

            if (!target.ptr) {
                return -1;
            }

            // Check if directory
            if (target.flags & FS_DIRECTORY) {
                 kfree(target.ptr, inode_size);
                 return -1;
            }

            // Remove directory entry
            entry.inode = 0;
            memset(entry.name, 0, 30);
            if (minix_write(dir, offset, sizeof(entry), (uint8_t *)&entry) != sizeof(entry)) {
                kfree(target.ptr, inode_size);
                return -1;
            }

            if (v2) {
                struct minix_inode_v2 *t_inode = (struct minix_inode_v2 *)target.ptr;
                if (t_inode->i_nlinks > 0) t_inode->i_nlinks--;

                if (t_inode->i_nlinks == 0) {
                    minix_free_all_zones(fs, &target);
                    minix_free_inode(fs, target_inode_num);
                } else {
                    extern int64_t get_time(void);
                    t_inode->i_ctime = get_time();
                    minix_write_inode(fs, &target);
                }
            } else {
                struct minix_inode_v1 *t_inode = (struct minix_inode_v1 *)target.ptr;
                if (t_inode->i_nlinks > 0) t_inode->i_nlinks--;

                if (t_inode->i_nlinks == 0) {
                    minix_free_all_zones(fs, &target);
                    minix_free_inode(fs, target_inode_num);
                } else {
                    extern int64_t get_time(void);
                    t_inode->i_time = (uint32_t)get_time();
                    minix_write_inode(fs, &target);
                }
            }

            kfree(target.ptr, inode_size);

            // Update parent directory timestamps
            extern int64_t get_time(void);
            dir->mtime = get_time();
            dir->ctime = dir->mtime;
            minix_write_inode(fs, dir);

            return 0;
        }
        offset += sizeof(entry);
    }

    return -1;
}

static int minix_write_inode(minix_fs_t *fs, fs_node_t *node) {
    if (!fs || !node || !node->ptr) return -1;

    if (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14) {
        // V2
        uint32_t inode_start = 2 + fs->sb.s_imap_blocks + fs->sb.s_zmap_blocks;
        uint32_t inodes_per_block = MINIX_BLOCK_SIZE / sizeof(struct minix_inode_v2);
        uint32_t block = inode_start + (node->inode - 1) / inodes_per_block;
        uint32_t offset = ((node->inode - 1) % inodes_per_block) * sizeof(struct minix_inode_v2);
        
        uint8_t buf[MINIX_BLOCK_SIZE];
        if (read_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) return -1;
        
        memcpy(buf + offset, node->ptr, sizeof(struct minix_inode_v2));
        
        if (write_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) return -1;
        return 0;
    } else {
        // V1
        return minix_write_inode_raw(fs, node->inode, (struct minix_inode_v1 *)node->ptr);
    }
}

static int minix_write_inode_raw(minix_fs_t *fs, uint32_t inode_num, struct minix_inode_v1 *inode) {
    uint32_t inode_start_block = 2 + fs->sb.s_imap_blocks + fs->sb.s_zmap_blocks;
    uint32_t inodes_per_block = MINIX_BLOCK_SIZE / sizeof(struct minix_inode_v1);
    uint32_t block = inode_start_block + (inode_num - 1) / inodes_per_block;
    uint32_t offset = ((inode_num - 1) % inodes_per_block) * sizeof(struct minix_inode_v1);

    uint8_t buf[MINIX_BLOCK_SIZE];
    if (read_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
        return -1;
    }

    memcpy(buf + offset, inode, sizeof(struct minix_inode_v1));

    if (write_fs(fs->block_device, block * MINIX_BLOCK_SIZE, MINIX_BLOCK_SIZE, buf) != MINIX_BLOCK_SIZE) {
        return -1;
    }
    return 0;
}
