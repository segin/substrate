#include <fs/fat/fat.h>
#include <kern/console.h>
#include <string.h>
#include <vm/vm_kmem.h>

// Global filesystem list is handled by VFS

#define FAT_ROOT_INO 1ULL
#define FAT_SYNTH_INO_BASE 0x8000000000000000ULL


// Node cache for dynamically allocated nodes
static fat_node_t fat_node_cache[FAT_NODE_CACHE_SIZE];
static fs_node_t fat_fs_node_cache[FAT_NODE_CACHE_SIZE];
static int fat_node_cache_idx = 0;

static uint32_t fat_cluster_to_sector(fat_fs_t *fs, uint32_t cluster);

/* Write support forward declarations */
static size_t fat_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer);
static int fat_truncate(fs_node_t *node, off_t new_size);
static int fat_mkdir_vfs(fs_node_t *parent, const char *name, uint16_t permission);
static int fat_unlink(fs_node_t *parent, const char *name);
static int fat_rmdir_vfs(fs_node_t *parent, const char *name);
static int fat_rename(fs_node_t *old_parent, const char *old_name,
                      fs_node_t *new_parent, const char *new_name);

static uint8_t fat_ascii_tolower(uint8_t c) {
    if (c >= 'A' && c <= 'Z') {
        return (uint8_t)(c - 'A' + 'a');
    }
    return c;
}

static int fat_name_matches(const char *entry_name, const char *lookup_name) {
    if (!entry_name || !lookup_name) {
        return 0;
    }

    while (*entry_name && *lookup_name) {
        if (fat_ascii_tolower((uint8_t)*entry_name) != fat_ascii_tolower((uint8_t)*lookup_name)) {
            return 0;
        }
        entry_name++;
        lookup_name++;
    }

    return *entry_name == '\0' && *lookup_name == '\0';
}

static uint64_t fat_make_synth_inode(fat_fs_t *fs, uint32_t dir_cluster,
                                     uint32_t sector_index,
                                     uint32_t entry_offset,
                                     uint32_t first_cluster) {
    if (first_cluster != 0) {
        return (uint64_t)first_cluster;
    }

    if (dir_cluster == 0 && fs->fat_type != 32) {
        uint64_t slot = ((uint64_t)(fs->root_dir_first_sector + sector_index) << 16) |
                        (uint64_t)(entry_offset / sizeof(fat_dirent_t));
        return FAT_SYNTH_INO_BASE | slot;
    }

    {
        uint64_t slot = ((uint64_t)(fat_cluster_to_sector(fs, dir_cluster) + sector_index) << 16) |
                        (uint64_t)(entry_offset / sizeof(fat_dirent_t));
        return FAT_SYNTH_INO_BASE | slot;
    }
}

static uint32_t fat_default_mask(uint8_t attr) {
    uint32_t mask;
    if (attr & FAT_ATTR_DIRECTORY) {
        mask = 0755;
    } else {
        mask = 0644;
    }

    if (attr & FAT_ATTR_READ_ONLY) {
        mask &= ~0222;
    }

    return mask;
}

// Read sectors from device
static int fat_read_sectors(fat_fs_t *fs, uint32_t sector, uint32_t count, void *buffer) {
    if (!fs->device || !fs->device->read) return -1;
    uint32_t bytes_per_sector = fs->bpb.bytes_per_sector ? fs->bpb.bytes_per_sector : 512;
    off_t offset = (off_t)sector * bytes_per_sector;
    size_t size = count * bytes_per_sector;
    size_t read = fs->device->read(fs->device, offset, size, (uint8_t *)buffer);
    return (read == size) ? 0 : -1;
}

static int fat_read_fat_bytes(fat_fs_t *fs, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (!fs || !buffer || !size) return -1;
    if (!fs->device || !fs->device->read) return -1;

    uint64_t byte_off = (uint64_t)fs->fat_start_sector * fs->bpb.bytes_per_sector + offset;
    size_t read = fs->device->read(fs->device, (off_t)byte_off, size, buffer);
    return (read == size) ? 0 : -1;
}

static int fat_read_root_sector(fat_fs_t *fs, uint32_t index, uint8_t *buffer, size_t buffer_size) {
    if (!fs || !buffer) return -1;
    if (fs->fat_type == 32) return -1;
    if (index >= fs->root_dir_sectors) return -1;
    if (fs->bpb.bytes_per_sector == 0 || fs->bpb.bytes_per_sector > buffer_size) return -1;

    return fat_read_sectors(fs, fs->root_dir_first_sector + index, 1, buffer);
}

// Get next cluster from FAT table
uint32_t fat_get_next_cluster(fat_fs_t *fs, uint32_t cluster) {
    if (!fs) return 0x0FFFFFFF;
    if (cluster < 2 || cluster >= (fs->total_clusters + 2)) return 0x0FFFFFFF;
    
    if (fs->fat_type == 32) {
        uint32_t entry = 0;
        uint64_t off = (uint64_t)cluster * 4;

        if (fs->fat_table && off + 4 <= fs->fat_table_size) {
            uint32_t *fat32 = (uint32_t *)fs->fat_table;
            entry = fat32[cluster];
        } else {
            uint8_t raw[4];
            if (fat_read_fat_bytes(fs, (uint32_t)off, sizeof(raw), raw) != 0) return 0x0FFFFFFF;
            entry = (uint32_t)raw[0] |
                    ((uint32_t)raw[1] << 8) |
                    ((uint32_t)raw[2] << 16) |
                    ((uint32_t)raw[3] << 24);
        }

        uint32_t next = entry & 0x0FFFFFFF;
        if (next < 2) return 0x0FFFFFFF;
        if (next >= 0x0FFFFFF8) return 0x0FFFFFFF; // EOC
        return next;
    } else if (fs->fat_type == 16) {
        uint16_t next = 0;
        uint64_t off = (uint64_t)cluster * 2;

        if (fs->fat_table && off + 2 <= fs->fat_table_size) {
            uint16_t *fat16 = (uint16_t *)fs->fat_table;
            next = fat16[cluster];
        } else {
            uint8_t raw[2];
            if (fat_read_fat_bytes(fs, (uint32_t)off, sizeof(raw), raw) != 0) return 0x0FFFFFFF;
            next = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
        }

        if (next < 2) return 0x0FFFFFFF;
        if (next >= 0xFFF8) return 0x0FFFFFFF; // EOC
        return next;
    } else if (fs->fat_type == 12) {
        uint32_t offset = cluster + (cluster / 2);
        uint16_t value = 0;

        if (fs->fat_table && (uint64_t)offset + 2 <= fs->fat_table_size) {
            value = *(uint16_t *)(fs->fat_table + offset);
        } else {
            uint8_t raw[2];
            if (fat_read_fat_bytes(fs, offset, sizeof(raw), raw) != 0) return 0x0FFFFFFF;
            value = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
        }

        if (cluster & 1) {
            value >>= 4;
        } else {
            value &= 0x0FFF;
        }
        if (value < 2) return 0x0FFFFFFF;
        if (value >= 0x0FF8) return 0x0FFFFFFF; // EOC
        return value;
    }
    
    return 0x0FFFFFFF;
}

// Convert cluster to sector
static uint32_t fat_cluster_to_sector(fat_fs_t *fs, uint32_t cluster) {
    return fs->first_data_sector + (cluster - 2) * fs->bpb.sectors_per_cluster;
}

// Parse short filename (8.3 format)
static void fat_parse_short_name(const char *fat_name, char *output) {
    int i, j = 0;
    
    // Copy name part (8 chars)
    for (i = 0; i < 8 && fat_name[i] != ' '; i++) {
        output[j++] = fat_name[i];
    }
    
    // Add extension if present
    if (fat_name[8] != ' ') {
        output[j++] = '.';
        for (i = 8; i < 11 && fat_name[i] != ' '; i++) {
            output[j++] = fat_name[i];
        }
    }
    
    output[j] = '\0';
}

// Parse long filename entry
int fat_parse_lfn(fat_lfn_t *lfn, char *buffer, int max_len) {
    // Security: Validate index to prevent buffer overflow
    int idx = ((lfn->order & 0x3F) - 1) * 13;  // Each LFN holds 13 chars

    if (idx < 0 || idx >= max_len) return -1;
    
    // Extract name1 (5 chars)
    for (int i = 0; i < 5; i++) {
        uint16_t c = lfn->name1[i];
        if (c == 0 || c == 0xFFFF) return idx;
        if (idx >= max_len) return -1;
        buffer[idx++] = (c < 256) ? (char)c : '?';  // Simple UTF-16 to ASCII
    }
    
    // Extract name2 (6 chars)
    for (int i = 0; i < 6; i++) {
        uint16_t c = lfn->name2[i];
        if (c == 0 || c == 0xFFFF) return idx;
        if (idx >= max_len) return -1;
        buffer[idx++] = (c < 256) ? (char)c : '?';
    }
    
    // Extract name3 (2 chars)
    for (int i = 0; i < 2; i++) {
        uint16_t c = lfn->name3[i];
        if (c == 0 || c == 0xFFFF) return idx;
        if (idx >= max_len) return -1;
        buffer[idx++] = (c < 256) ? (char)c : '?';
    }
    
    return idx;
}

// Read file data
size_t fat_file_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    fat_node_t *ctx = (fat_node_t *)(uintptr_t)node->impl;
    fat_fs_t *fs = ctx->fs;
    
    if ((uint32_t)offset >= ctx->size) return 0;
    if (offset + size > ctx->size) size = ctx->size - offset;
    
    uint32_t cluster = ctx->first_cluster;
    uint32_t cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;
    uint32_t total_read = 0;
    
    // Skip to starting cluster
    uint32_t skip_clusters = offset / cluster_size;
    uint32_t cluster_offset = offset % cluster_size;
    uint32_t visited = 0;
    
    for (uint32_t i = 0; i < skip_clusters; i++) {
        cluster = fat_get_next_cluster(fs, cluster);
        if (cluster >= 0x0FFFFFFF) return 0;
        if (++visited > fs->total_clusters) return 0; // Cycle detection
    }
    
    // Read data
    uint8_t *cluster_buf = kmalloc(fs->cluster_size);
    if (!cluster_buf) return 0;

    while (size > 0 && cluster < 0x0FFFFFFF) {
        if (++visited > fs->total_clusters) break; // Cycle detection
        uint32_t sector = fat_cluster_to_sector(fs, cluster);
        
        if (fat_read_sectors(fs, sector, fs->bpb.sectors_per_cluster, cluster_buf) != 0) {
            break;
        }
        
        uint32_t to_copy = cluster_size - cluster_offset;
        if (to_copy > size) to_copy = size;
        
        memcpy(buffer + total_read, cluster_buf + cluster_offset, to_copy);
        total_read += to_copy;
        size -= to_copy;
        cluster_offset = 0;
        
        cluster = fat_get_next_cluster(fs, cluster);
    }
    
    kfree(cluster_buf, fs->cluster_size);
    return total_read;
}

// Read directory entries
struct dirent *fat_readdir(fs_node_t *node, uint64_t index) {
    fat_node_t *ctx = (fat_node_t *)(uintptr_t)node->impl;
    fat_fs_t *fs = ctx->fs;
    
    struct dirent *dirent = &ctx->current_dirent;
    uint8_t *dir_buf = kmalloc(fs->cluster_size);
    if (!dir_buf) return NULL;
    char lfn_buffer[256];
    uint32_t bytes_per_sector = fs->bpb.bytes_per_sector;
    uint32_t cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;

    if (ctx->first_cluster == 0 && fs->fat_type != 32) {
        uint64_t current_idx = 0;
        int lfn_len = 0;
        uint8_t *root_sector_buf = kmalloc(fs->bpb.bytes_per_sector);
        if (!root_sector_buf) {
            kfree(dir_buf, fs->cluster_size);
            return NULL;
        }

        for (uint32_t sector_i = 0; sector_i < fs->root_dir_sectors; sector_i++) {
            if (fat_read_root_sector(fs, sector_i, root_sector_buf, fs->bpb.bytes_per_sector) != 0) {
                kfree(root_sector_buf, fs->bpb.bytes_per_sector);
                kfree(dir_buf, fs->cluster_size);
                return NULL;
            }

            for (uint32_t i = 0; i + sizeof(fat_dirent_t) <= bytes_per_sector; i += sizeof(fat_dirent_t)) {
                fat_dirent_t *entry = (fat_dirent_t *)(root_sector_buf + i);

                if (entry->name[0] == 0x00) {
                    kfree(root_sector_buf, fs->bpb.bytes_per_sector);
                    kfree(dir_buf, fs->cluster_size);
                    return NULL;
                }
                if ((uint8_t)entry->name[0] == 0xE5) continue;

                if (entry->attr == FAT_ATTR_LFN) {
                    fat_lfn_t *lfn = (fat_lfn_t *)entry;
                    if (lfn->order & 0x40) {
                        memset(lfn_buffer, 0, sizeof(lfn_buffer));
                        lfn_len = 0;
                    }

                    int ret = fat_parse_lfn(lfn, lfn_buffer, sizeof(lfn_buffer) - 1);
                    if (ret > lfn_len) lfn_len = ret;
                    continue;
                }

                if (entry->attr & FAT_ATTR_VOLUME_ID) continue;

                if (current_idx == index) {
                    if (lfn_len > 0) {
                        lfn_buffer[lfn_len] = '\0';
                        strncpy(dirent->d_name, lfn_buffer, 127);
                        dirent->d_name[127] = '\0';
                        lfn_len = 0;
                    } else {
                        fat_parse_short_name(entry->name, dirent->d_name);
                    }

                    uint32_t cluster_num = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
                    dirent->d_ino = fat_make_synth_inode(fs, 0, sector_i, i, cluster_num);
                    kfree(root_sector_buf, fs->bpb.bytes_per_sector);
                    kfree(dir_buf, fs->cluster_size);
                    return dirent;
                }

                current_idx++;
                lfn_len = 0;
            }
        }
        kfree(root_sector_buf, fs->bpb.bytes_per_sector);
        kfree(dir_buf, fs->cluster_size);
        return NULL;
    }
    
    uint32_t cluster = ctx->first_cluster;
    uint64_t current_idx = 0;
    int lfn_len = 0;
    uint32_t visited = 0;
    
    while (cluster < 0x0FFFFFFF) {
        if (++visited > fs->total_clusters) return NULL; // Cycle detection
        uint32_t sector = fat_cluster_to_sector(fs, cluster);
        if (fat_read_sectors(fs, sector, fs->bpb.sectors_per_cluster, dir_buf) != 0) {
            kfree(dir_buf, fs->cluster_size);
            return NULL;
        }
        
        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dirent_t *entry = (fat_dirent_t *)(dir_buf + i);
            
            if (entry->name[0] == 0x00) {
                kfree(dir_buf, fs->cluster_size);
                return NULL;  // End of directory
            }
            if ((uint8_t)entry->name[0] == 0xE5) continue;      // Deleted entry
            
            // Check for LFN entry
            if (entry->attr == FAT_ATTR_LFN) {
                fat_lfn_t *lfn = (fat_lfn_t *)entry;

                // If this is the last entry (masked with 0x40), it marks the start of a new LFN sequence
                if (lfn->order & 0x40) {
                    memset(lfn_buffer, 0, sizeof(lfn_buffer));
                    lfn_len = 0;
                }

                int ret = fat_parse_lfn(lfn, lfn_buffer, sizeof(lfn_buffer) - 1);
                if (ret > lfn_len) {
                    lfn_len = ret;
                }
                continue;
            }
            
            // Skip volume labels
            if (entry->attr & FAT_ATTR_VOLUME_ID) continue;
            
            // Found a valid entry
            if (current_idx == index) {
                if (lfn_len > 0) {
                    lfn_buffer[lfn_len] = '\0';
                    strncpy(dirent->d_name, lfn_buffer, 127);
                    dirent->d_name[127] = '\0';
                    lfn_len = 0;
                } else {
                    fat_parse_short_name(entry->name, dirent->d_name);
                }
                
                uint32_t cluster_num = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
                dirent->d_ino = fat_make_synth_inode(fs, ctx->first_cluster, 0, i, cluster_num);
                kfree(dir_buf, fs->cluster_size);
                return dirent;
            }
            
            current_idx++;
            lfn_len = 0;
        }
        
        cluster = fat_get_next_cluster(fs, cluster);
    }
    
    return NULL;
}

// Allocate a new node from cache
static fs_node_t *fat_alloc_node(fat_fs_t *fs, const char *name, uint64_t inode,
                                 uint32_t first_cluster, uint32_t size, uint8_t attr) {
    int idx = fat_node_cache_idx++ % FAT_NODE_CACHE_SIZE;
    
    fat_node_t *ctx = &fat_node_cache[idx];
    fs_node_t *node = &fat_fs_node_cache[idx];

    // If node was previously used by a different filesystem, or for a different inode,
    // we should probably clear it.
    memset(node, 0, sizeof(fs_node_t));
    memset(ctx, 0, sizeof(fat_node_t));
    
    ctx->fs = fs;
    ctx->first_cluster = first_cluster;
    ctx->size = size;
    ctx->attr = attr;
    
    memset(node, 0, sizeof(fs_node_t));
    strncpy(node->name, name, 127);
    node->name[127] = '\0';
    node->impl = (uint32_t)(uintptr_t)ctx;
    node->inode = inode;
    node->length = size;
    node->mask = fat_default_mask(attr);
    node->uid = 0;
    node->gid = 0;
    
    if (attr & FAT_ATTR_DIRECTORY) {
        node->flags = FS_DIRECTORY;
        node->readdir = fat_readdir;
        node->finddir = fat_finddir;
        node->mkdir = fat_mkdir_vfs;
        node->unlink = fat_unlink;
        node->rmdir = fat_rmdir_vfs;
        node->rename = fat_rename;
    } else {
        node->flags = FS_FILE;
        node->read = fat_file_read;
        node->write = fat_file_write;
        node->truncate = fat_truncate;
    }
    
    return node;
}

// Find directory entry by name
fs_node_t *fat_finddir(fs_node_t *node, char *name) {
    fat_node_t *ctx = (fat_node_t *)(uintptr_t)node->impl;
    fat_fs_t *fs = ctx->fs;
    uint32_t bytes_per_sector = fs->bpb.bytes_per_sector;
    uint8_t *root_sector_buf = NULL;
    
    if (ctx->first_cluster == 0 && fs->fat_type != 32) {
        root_sector_buf = kmalloc(bytes_per_sector);
        if (!root_sector_buf) return NULL;
        char lfn_buffer[256];
        int lfn_len = 0;

        for (uint32_t sector_i = 0; sector_i < fs->root_dir_sectors; sector_i++) {
            if (fat_read_root_sector(fs, sector_i, root_sector_buf, bytes_per_sector) != 0) {
                kfree(root_sector_buf, bytes_per_sector);
                return NULL;
            }

            for (uint32_t i = 0; i + sizeof(fat_dirent_t) <= bytes_per_sector; i += sizeof(fat_dirent_t)) {
                fat_dirent_t *entry = (fat_dirent_t *)(root_sector_buf + i);

                if (entry->name[0] == 0x00) {
                    kfree(root_sector_buf, bytes_per_sector);
                    return NULL;
                }
                if ((uint8_t)entry->name[0] == 0xE5) continue;

                if (entry->attr == FAT_ATTR_LFN) {
                    fat_lfn_t *lfn = (fat_lfn_t *)entry;
                    if (lfn->order & 0x40) {
                        memset(lfn_buffer, 0, sizeof(lfn_buffer));
                        lfn_len = 0;
                    }

                    int ret = fat_parse_lfn(lfn, lfn_buffer, sizeof(lfn_buffer) - 1);
                    if (ret > lfn_len) lfn_len = ret;
                    continue;
                }

                if (entry->attr & FAT_ATTR_VOLUME_ID) continue;

                char entry_name[128];
                if (lfn_len > 0) {
                    lfn_buffer[lfn_len] = '\0';
                    strncpy(entry_name, lfn_buffer, 127);
                    entry_name[127] = '\0';
                } else {
                    fat_parse_short_name(entry->name, entry_name);
                }

                if (fat_name_matches(entry_name, name)) {
                    uint32_t cluster_num = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
                    uint64_t inode = fat_make_synth_inode(fs, 0, sector_i, i, cluster_num);
                    fs_node_t *ret = fat_alloc_node(fs, entry_name, inode, cluster_num, entry->file_size, entry->attr);
                    kfree(root_sector_buf, bytes_per_sector);
                    return ret;
                }

                lfn_len = 0;
            }
        }

        kfree(root_sector_buf, bytes_per_sector);
        return NULL;
    }
    
    uint32_t cluster = ctx->first_cluster;
    uint32_t cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;
    
    uint8_t *dir_buf = kmalloc(cluster_size);
    if (!dir_buf) return NULL;
    char lfn_buffer[256];
    int lfn_len = 0;
    uint32_t visited = 0;
    
    while (cluster < 0x0FFFFFFF) {
        if (++visited > fs->total_clusters) {
            kfree(dir_buf, cluster_size);
            return NULL; // Cycle detection
        }
        uint32_t sector = fat_cluster_to_sector(fs, cluster);
        if (fat_read_sectors(fs, sector, fs->bpb.sectors_per_cluster, dir_buf) != 0) {
            kfree(dir_buf, cluster_size);
            return NULL;
        }
        
        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dirent_t *entry = (fat_dirent_t *)(dir_buf + i);
            
            if (entry->name[0] == 0x00) {
                kfree(dir_buf, cluster_size);
                return NULL;  // End of directory
            }
            if ((uint8_t)entry->name[0] == 0xE5) continue;      // Deleted entry
            
            // Check for LFN entry
            if (entry->attr == FAT_ATTR_LFN) {
                fat_lfn_t *lfn = (fat_lfn_t *)entry;

                // If this is the last entry (masked with 0x40), it marks the start of a new LFN sequence
                if (lfn->order & 0x40) {
                    memset(lfn_buffer, 0, sizeof(lfn_buffer));
                    lfn_len = 0;
                }

                int ret = fat_parse_lfn(lfn, lfn_buffer, sizeof(lfn_buffer) - 1);
                if (ret > lfn_len) {
                    lfn_len = ret;
                }
                continue;
            }
            
            // Skip volume labels
            if (entry->attr & FAT_ATTR_VOLUME_ID) continue;
            
            // Build name and compare
            char entry_name[128];
            if (lfn_len > 0) {
                lfn_buffer[lfn_len] = '\0';
                strncpy(entry_name, lfn_buffer, 127);
                entry_name[127] = '\0';
            } else {
                fat_parse_short_name(entry->name, entry_name);
            }
            
            if (fat_name_matches(entry_name, name)) {
                // Found it - create and return node
                uint32_t cluster_num = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
                uint64_t inode = fat_make_synth_inode(fs, ctx->first_cluster, 0, i, cluster_num);
                fs_node_t *ret = fat_alloc_node(fs, entry_name, inode, cluster_num, entry->file_size, entry->attr);
                kfree(dir_buf, fs->cluster_size);
                return ret;
            }
            
            lfn_len = 0;
        }
        
        cluster = fat_get_next_cluster(fs, cluster);
    }
    
    kfree(dir_buf, fs->cluster_size);
    return NULL;
}

static int fat_unmount(fs_node_t *root) {
    if (!root) return -1;
    fat_node_t *ctx = (fat_node_t *)(uintptr_t)root->impl;
    if (!ctx) return -1;
    fat_fs_t *fs = ctx->fs;
    if (!fs) return -1;

    // Invalidate node cache for this filesystem
    for (int i = 0; i < FAT_NODE_CACHE_SIZE; i++) {
        if (fat_node_cache[i].fs == fs) {
            memset(&fat_node_cache[i], 0, sizeof(fat_node_t));
            memset(&fat_fs_node_cache[i], 0, sizeof(fs_node_t));
        }
    }

    if (fs->fat_table) {
        kfree(fs->fat_table, fs->fat_table_size);
    }

    kfree(fs, sizeof(fat_fs_t));
    // Root node and its context are in the cache (usually), 
    // but the VFS might have a separate copy of the root node.
    // Actually, fat_mount returns a pointer to the cache.
    return 0;
}

// Mount FAT filesystem
fs_node_t *fat_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags;
    
    fs_node_t *dev = (fs_node_t *)data;
    if (!dev || !dev->read) {
        kprint("FAT: No device or read function\n");
        return NULL;
    }
    
    fat_fs_t *fs = kmalloc(sizeof(fat_fs_t));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(*fs));
    fs->device = dev;
    
    // Read boot sector
    if (fat_read_sectors(fs, 0, 1, &fs->bpb) != 0) {
        kprint("FAT: Failed to read boot sector\n");
        kfree(fs, sizeof(fat_fs_t));
        return NULL;
    }

    if (fs->bpb.bytes_per_sector == 0 || fs->bpb.sectors_per_cluster == 0 ||
        fs->bpb.fat_count == 0 || fs->bpb.reserved_sectors == 0) {
        kprint("FAT: Invalid BPB\n");
        kfree(fs, sizeof(fat_fs_t));
        return NULL;
    }

    if ((fs->bpb.bytes_per_sector & (fs->bpb.bytes_per_sector - 1)) != 0 ||
        fs->bpb.bytes_per_sector < 512 || fs->bpb.bytes_per_sector > 4096) {
        kprint("FAT: Unsupported sector size\n");
        kfree(fs, sizeof(fat_fs_t));
        return NULL;
    }

    // Validate sectors_per_cluster: must be power of 2, cluster size <= 32KB
    if ((fs->bpb.sectors_per_cluster & (fs->bpb.sectors_per_cluster - 1)) != 0) {
        kprint("FAT: Invalid sectors_per_cluster (not power of 2)\n");
        kfree(fs, sizeof(fat_fs_t));
        return NULL;
    }
    if ((uint32_t)fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster > 32768) {
        kprint("FAT: Cluster size exceeds 32KB limit\n");
        kfree(fs, sizeof(fat_fs_t));
        return NULL;
    }
    
    // Determine FAT type
    uint32_t root_dir_sectors = ((fs->bpb.root_entries * 32) + (fs->bpb.bytes_per_sector - 1)) / fs->bpb.bytes_per_sector;
    uint32_t fat_size = (fs->bpb.fat_size_16 != 0) ? fs->bpb.fat_size_16 : 0;
    
    // Read FAT32 extended BPB if needed
    if (fat_size == 0) {
        off_t ext_offset = sizeof(fat_bpb_t);
        if (dev->read(dev, ext_offset, sizeof(fat32_ext_bpb_t), (uint8_t *)&fs->ext_bpb) != sizeof(fat32_ext_bpb_t)) {
            kfree(fs, sizeof(fat_fs_t));
            return NULL;
        }
        fat_size = fs->ext_bpb.fat_size_32;
    }
    
    uint32_t total_sectors = (fs->bpb.total_sectors_16 != 0) ? fs->bpb.total_sectors_16 : fs->bpb.total_sectors_32;
    if (fat_size == 0 || total_sectors == 0) {
        kprint("FAT: Invalid FAT geometry\n");
        kfree(fs, sizeof(fat_fs_t));
        return NULL;
    }

    uint32_t overhead_sectors = fs->bpb.reserved_sectors + (fs->bpb.fat_count * fat_size) + root_dir_sectors;
    if (total_sectors <= overhead_sectors) {
        kprint("FAT: Invalid sector layout\n");
        kfree(fs, sizeof(fat_fs_t));
        return NULL;
    }

    uint32_t data_sectors = total_sectors - overhead_sectors;
    uint32_t total_clusters = data_sectors / fs->bpb.sectors_per_cluster;
    
    // Determine FAT type
    if (total_clusters < 4085) {
        fs->fat_type = 12;
    } else if (total_clusters < 65525) {
        fs->fat_type = 16;
    } else {
        fs->fat_type = 32;
    }
    
    fs->fat_start_sector = fs->bpb.reserved_sectors;
    fs->fat_sectors = fat_size;
    fs->root_dir_first_sector = fs->bpb.reserved_sectors + (fs->bpb.fat_count * fat_size);
    fs->root_dir_sectors = root_dir_sectors;
    fs->first_data_sector = fs->root_dir_first_sector + root_dir_sectors;
    fs->total_clusters = total_clusters;
    fs->cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;
    
    // Cache FAT table for better performance
    uint64_t fat_table_size_64 = (uint64_t)fat_size * fs->bpb.bytes_per_sector;
    uint32_t fat_table_size = 0;

    if (fat_table_size_64 > 0xFFFFFFFF) {
        kprint("FAT: FAT table too large for cache\n");
        fs->fat_table = NULL;
        fs->fat_table_size = 0;
    } else {
        fat_table_size = (uint32_t)fat_table_size_64;
        fs->fat_table = (uint8_t *)kmalloc(fat_table_size);
        fs->fat_table_size = fat_table_size;
    }

    if (fs->fat_table) {
        if (fat_read_sectors(fs, fs->fat_start_sector, fat_size, fs->fat_table) != 0) {
            kprint("FAT: Warning - failed to cache FAT table\n");
            fs->fat_table = NULL;
            fs->fat_table_size = 0;
        }
    } else if (fat_table_size_64 <= 0xFFFFFFFF) {
        kprint("FAT: Warning - could not allocate FAT table cache\n");
        fs->fat_table_size = 0;
    }
    
    kprint(")\n");
    
    uint32_t root_cluster = (fs->fat_type == 32) ? fs->ext_bpb.root_cluster : 0;
    fs_node_t *root_node = fat_alloc_node(fs, "/", FAT_ROOT_INO, root_cluster, 0, FAT_ATTR_DIRECTORY);
    if (root_node) {
        root_node->unmount = fat_unmount;
        fs->root_node = root_node;
    }
    return root_node;
}

/* ============================================================
 * FAT Write Support
 * ============================================================ */

/* Write sectors to device */
static int fat_write_sectors(fat_fs_t *fs, uint32_t sector, uint32_t count, const void *buffer) {
    if (!fs->device || !fs->device->write) return -1;
    uint32_t bytes_per_sector = fs->bpb.bytes_per_sector ? fs->bpb.bytes_per_sector : 512;
    off_t offset = (off_t)sector * bytes_per_sector;
    size_t size = count * bytes_per_sector;
    size_t written = fs->device->write(fs->device, offset, size, (const uint8_t *)buffer);
    return (written == size) ? 0 : -1;
}

/* Write FAT entry for a cluster, updating all FAT copies */
static int fat_set_fat_entry(fat_fs_t *fs, uint32_t cluster, uint32_t value) {
    if (!fs || cluster < 2 || cluster >= (fs->total_clusters + 2)) return -1;

    for (uint32_t fat_copy = 0; fat_copy < fs->bpb.fat_count; fat_copy++) {
        uint32_t fat_sector_base = fs->fat_start_sector + fat_copy * fs->fat_sectors;

        if (fs->fat_type == 32) {
            uint32_t fat_offset = cluster * 4;
            uint32_t sector = fat_sector_base + fat_offset / fs->bpb.bytes_per_sector;
            uint32_t byte_off = fat_offset % fs->bpb.bytes_per_sector;
            uint8_t sector_buf[4096];
            if (fat_read_sectors(fs, sector, 1, sector_buf) != 0) return -1;
            uint32_t existing;
            __builtin_memcpy(&existing, sector_buf + byte_off, 4);
            existing = (existing & 0xF0000000) | (value & 0x0FFFFFFF);
            __builtin_memcpy(sector_buf + byte_off, &existing, 4);
            if (fat_write_sectors(fs, sector, 1, sector_buf) != 0) return -1;
            /* Update cache */
            if (fs->fat_table && (fat_offset + 4) <= fs->fat_table_size) {
                uint32_t *fat32 = (uint32_t *)fs->fat_table;
                fat32[cluster] = existing;
            }
        } else if (fs->fat_type == 16) {
            uint32_t fat_offset = cluster * 2;
            uint32_t sector = fat_sector_base + fat_offset / fs->bpb.bytes_per_sector;
            uint32_t byte_off = fat_offset % fs->bpb.bytes_per_sector;
            uint8_t sector_buf[4096];
            if (fat_read_sectors(fs, sector, 1, sector_buf) != 0) return -1;
            uint16_t v16 = (uint16_t)(value & 0xFFFF);
            __builtin_memcpy(sector_buf + byte_off, &v16, 2);
            if (fat_write_sectors(fs, sector, 1, sector_buf) != 0) return -1;
            if (fs->fat_table && (fat_offset + 2) <= fs->fat_table_size) {
                uint16_t *fat16 = (uint16_t *)fs->fat_table;
                fat16[cluster] = v16;
            }
        } else { /* FAT12 */
            uint32_t fat_offset = cluster + (cluster / 2);
            uint32_t sector = fat_sector_base + fat_offset / fs->bpb.bytes_per_sector;
            uint32_t byte_off = fat_offset % fs->bpb.bytes_per_sector;
            uint8_t sector_buf[4096];
            if (fat_read_sectors(fs, sector, 1, sector_buf) != 0) return -1;
            /* Handle boundary across two sectors */
            uint8_t lo = sector_buf[byte_off];
            uint8_t hi = (byte_off + 1 < fs->bpb.bytes_per_sector)
                         ? sector_buf[byte_off + 1]
                         : 0;
            if (cluster & 1) {
                lo = (lo & 0x0F) | (uint8_t)((value & 0x0F) << 4);
                hi = (uint8_t)((value >> 4) & 0xFF);
            } else {
                lo = (uint8_t)(value & 0xFF);
                hi = (hi & 0xF0) | (uint8_t)((value >> 8) & 0x0F);
            }
            sector_buf[byte_off] = lo;
            if (byte_off + 1 < fs->bpb.bytes_per_sector) {
                sector_buf[byte_off + 1] = hi;
                if (fat_write_sectors(fs, sector, 1, sector_buf) != 0) return -1;
            } else {
                /* Split across sector boundary */
                if (fat_write_sectors(fs, sector, 1, sector_buf) != 0) return -1;
                uint8_t next_sector_buf[4096];
                if (fat_read_sectors(fs, sector + 1, 1, next_sector_buf) != 0) return -1;
                next_sector_buf[0] = hi;
                if (fat_write_sectors(fs, sector + 1, 1, next_sector_buf) != 0) return -1;
            }
            if (fs->fat_table && (fat_offset + 2) <= fs->fat_table_size) {
                fs->fat_table[fat_offset] = lo;
                if (fat_offset + 1 < fs->fat_table_size)
                    fs->fat_table[fat_offset + 1] = hi;
            }
        }
    }
    return 0;
}

/* EOC marker for each FAT type */
static uint32_t fat_eoc(fat_fs_t *fs) {
    if (fs->fat_type == 12) return 0x0FFF;
    if (fs->fat_type == 16) return 0xFFFF;
    return 0x0FFFFFFF;
}

/* Allocate a free cluster; returns cluster number or 0 on failure */
static uint32_t fat_alloc_cluster(fat_fs_t *fs) {
    for (uint32_t c = 2; c < fs->total_clusters + 2; c++) {
        uint32_t next = fat_get_next_cluster(fs, c);
        /* A free cluster has FAT entry == 0 */
        uint32_t entry_val;
        if (fs->fat_type == 32) {
            if (fs->fat_table && (c * 4 + 4) <= fs->fat_table_size) {
                uint32_t *fat32 = (uint32_t *)fs->fat_table;
                entry_val = fat32[c] & 0x0FFFFFFF;
            } else {
                entry_val = next; /* fat_get_next_cluster already returns 0x0FFFFFFF for non-free */
                /* Reread raw */
                uint8_t raw[4];
                uint32_t off = c * 4;
                if (fat_read_fat_bytes(fs, off, 4, raw) != 0) continue;
                entry_val = ((uint32_t)raw[0] | ((uint32_t)raw[1]<<8) | ((uint32_t)raw[2]<<16) | ((uint32_t)raw[3]<<24)) & 0x0FFFFFFF;
            }
        } else if (fs->fat_type == 16) {
            if (fs->fat_table && (c * 2 + 2) <= fs->fat_table_size) {
                uint16_t *fat16 = (uint16_t *)fs->fat_table;
                entry_val = fat16[c];
            } else {
                uint8_t raw[2];
                uint32_t off = c * 2;
                if (fat_read_fat_bytes(fs, off, 2, raw) != 0) continue;
                entry_val = (uint32_t)raw[0] | ((uint32_t)raw[1] << 8);
            }
        } else { /* FAT12 */
            uint32_t offset = c + (c / 2);
            uint8_t raw[2] = {0, 0};
            if (fs->fat_table && (offset + 2) <= fs->fat_table_size) {
                raw[0] = fs->fat_table[offset];
                raw[1] = fs->fat_table[offset + 1];
            } else {
                fat_read_fat_bytes(fs, offset, 2, raw);
            }
            uint16_t val = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
            entry_val = (c & 1) ? (val >> 4) : (val & 0x0FFF);
        }

        if (entry_val == 0) {
            /* Mark as EOC */
            if (fat_set_fat_entry(fs, c, fat_eoc(fs)) == 0)
                return c;
        }
        (void)next;
    }
    return 0; /* No free cluster */
}

/* Free a cluster chain starting at cluster */
static int fat_free_chain(fat_fs_t *fs, uint32_t cluster) {
    uint32_t visited = 0;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        if (++visited > fs->total_clusters) break;
        uint32_t next = fat_get_next_cluster(fs, cluster);
        fat_set_fat_entry(fs, cluster, 0);
        cluster = next;
    }
    return 0;
}

/* Write file data */
static size_t fat_file_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    fat_node_t *ctx = (fat_node_t *)(uintptr_t)node->impl;
    fat_fs_t *fs = ctx->fs;
    uint32_t cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;

    if (size == 0) return 0;

    /* Extend cluster chain if needed */
    uint32_t needed_end = (uint32_t)(offset + size);
    uint32_t needed_clusters = (needed_end + cluster_size - 1) / cluster_size;

    /* If no clusters yet, allocate first */
    if (ctx->first_cluster < 2) {
        uint32_t nc = fat_alloc_cluster(fs);
        if (nc == 0) return 0;
        ctx->first_cluster = nc;
        node->inode = nc;
        /* TODO: update directory entry cluster fields */
    }

    /* Walk chain, extending as needed */
    uint32_t cluster = ctx->first_cluster;
    uint32_t chain_len = 1;
    uint32_t visited = 0;
    while (chain_len < needed_clusters) {
        if (++visited > fs->total_clusters) return 0;
        uint32_t next = fat_get_next_cluster(fs, cluster);
        if (next >= 0x0FFFFFF8) {
            /* Extend */
            uint32_t nc = fat_alloc_cluster(fs);
            if (nc == 0) break;
            fat_set_fat_entry(fs, cluster, nc);
            fat_set_fat_entry(fs, nc, fat_eoc(fs));
            cluster = nc;
        } else {
            cluster = next;
        }
        chain_len++;
    }

    /* Now write data cluster by cluster */
    cluster = ctx->first_cluster;
    uint32_t skip = (uint32_t)offset / cluster_size;
    uint32_t cluster_offset = (uint32_t)offset % cluster_size;
    visited = 0;
    for (uint32_t i = 0; i < skip; i++) {
        if (++visited > fs->total_clusters) return 0;
        cluster = fat_get_next_cluster(fs, cluster);
        if (cluster >= 0x0FFFFFF8) return 0;
    }

    size_t total_written = 0;
    static uint8_t cluster_buf[32768];
    visited = 0;
    while (size > 0 && cluster < 0x0FFFFFF8) {
        if (++visited > fs->total_clusters) break;
        uint32_t sector = fat_cluster_to_sector(fs, cluster);

        /* Read-modify-write if partial cluster */
        if (cluster_offset > 0 || size < cluster_size) {
            if (fat_read_sectors(fs, sector, fs->bpb.sectors_per_cluster, cluster_buf) != 0) break;
        }

        uint32_t to_copy = cluster_size - cluster_offset;
        if (to_copy > size) to_copy = (uint32_t)size;

        __builtin_memcpy(cluster_buf + cluster_offset, buffer + total_written, to_copy);

        if (fat_write_sectors(fs, sector, fs->bpb.sectors_per_cluster, cluster_buf) != 0) break;

        total_written += to_copy;
        size -= to_copy;
        cluster_offset = 0;
        cluster = fat_get_next_cluster(fs, cluster);
    }

    /* Update file size in node */
    if ((uint32_t)(offset + total_written) > ctx->size) {
        ctx->size = (uint32_t)(offset + total_written);
        node->length = ctx->size;
        /* TODO: flush directory entry */
    }

    return total_written;
}

/* Truncate file */
static int fat_truncate(fs_node_t *node, off_t new_size) {
    fat_node_t *ctx = (fat_node_t *)(uintptr_t)node->impl;
    fat_fs_t *fs = ctx->fs;
    uint32_t cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;
    uint32_t needed_clusters = new_size > 0 ? ((uint32_t)new_size + cluster_size - 1) / cluster_size : 0;

    if (new_size == 0) {
        /* Free entire chain */
        if (ctx->first_cluster >= 2)
            fat_free_chain(fs, ctx->first_cluster);
        ctx->first_cluster = 0;
        ctx->size = 0;
        node->length = 0;
        return 0;
    }

    /* Walk to the needed_clusters-th cluster, free the rest */
    uint32_t cluster = ctx->first_cluster;
    uint32_t visited = 0;
    for (uint32_t i = 1; i < needed_clusters; i++) {
        if (++visited > fs->total_clusters) return -1;
        uint32_t next = fat_get_next_cluster(fs, cluster);
        if (next >= 0x0FFFFFF8) {
            /* Extend if truncating to larger size */
            uint32_t nc = fat_alloc_cluster(fs);
            if (nc == 0) return -1;
            fat_set_fat_entry(fs, cluster, nc);
            fat_set_fat_entry(fs, nc, fat_eoc(fs));
            cluster = nc;
        } else {
            cluster = next;
        }
    }

    /* Free rest of chain after cluster */
    uint32_t next = fat_get_next_cluster(fs, cluster);
    fat_set_fat_entry(fs, cluster, fat_eoc(fs));
    if (next < 0x0FFFFFF8)
        fat_free_chain(fs, next);

    ctx->size = (uint32_t)new_size;
    node->length = new_size;
    return 0;
}

/* Generate a short (8.3) name from a long name.
 * Output must be 11 bytes (space-padded, no dot, uppercase). */
static void fat_make_short_name(const char *name, char *out11) {
    /* Space-fill */
    for (int i = 0; i < 11; i++) out11[i] = ' ';

    int name_len = 0;
    while (name[name_len]) name_len++;

    /* Find last dot */
    int dot = -1;
    for (int i = name_len - 1; i >= 0; i--) {
        if (name[i] == '.') { dot = i; break; }
    }

    int ni = 0, oi = 0;
    int end = (dot >= 0) ? dot : name_len;
    while (ni < end && oi < 8) {
        unsigned char c = (unsigned char)name[ni++];
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        if (c == ' ' || c == '.') continue;
        out11[oi++] = (char)c;
    }

    if (dot >= 0) {
        int ei = dot + 1, eo = 8;
        while (ei < name_len && eo < 11) {
            unsigned char c = (unsigned char)name[ei++];
            if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
            out11[eo++] = (char)c;
        }
    }
}

/* Compute LFN checksum over an 8.3 short name */
static uint8_t fat_lfn_checksum(const char *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)short_name[i]);
    return sum;
}

/* Count number of LFN slots needed for a name */
static int fat_lfn_slots(int name_len) {
    return (name_len + 12) / 13;
}

/* Write LFN entries for a name into a buffer starting at lfn_entries[0].
 * slots = fat_lfn_slots(name_len). The buffer must have slots * 32 bytes.
 * LFN entries are written last-to-first (highest order first in buffer). */
static void fat_write_lfn_entries(fat_lfn_t *entries, const char *name, int name_len,
                                   int slots, uint8_t checksum) {
    for (int s = 0; s < slots; s++) {
        fat_lfn_t *e = &entries[s];
        int seq = slots - s; /* slot number (1-based, highest first) */
        e->order = (uint8_t)(seq | (s == 0 ? 0x40 : 0));
        e->attr = FAT_ATTR_LFN;
        e->type = 0;
        e->checksum = checksum;
        e->cluster = 0;

        /* Character index in name for this slot */
        int char_base = (seq - 1) * 13;
        uint16_t chars[13];
        for (int i = 0; i < 13; i++) {
            int ci = char_base + i;
            if (ci < name_len)
                chars[i] = (uint16_t)(unsigned char)name[ci];
            else if (ci == name_len)
                chars[i] = 0x0000; /* null terminator */
            else
                chars[i] = 0xFFFF; /* padding */
        }
        for (int i = 0; i < 5; i++) e->name1[i] = chars[i];
        for (int i = 0; i < 6; i++) e->name2[i] = chars[5 + i];
        for (int i = 0; i < 2; i++) e->name3[i] = chars[11 + i];
    }
}

/* Find a free slot in a directory cluster chain large enough for total_entries
 * directory entries. Returns sector number and byte offset within sector, or
 * -1 on failure. Also returns the cluster that contains the slot. */
static int fat_find_dir_space(fat_fs_t *fs, uint32_t dir_cluster,
                               int total_entries,
                               uint32_t *out_sector, uint32_t *out_byte_off,
                               uint32_t *out_cluster) {
    uint32_t cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;
    static uint8_t dir_buf[32768];
    int consecutive = 0;
    uint32_t start_sector = 0, start_off = 0, start_cluster = 0;

    if (dir_cluster == 0 && fs->fat_type != 32) {
        /* FAT12/16 fixed root directory */
        for (uint32_t si = 0; si < fs->root_dir_sectors; si++) {
            uint32_t sector = fs->root_dir_first_sector + si;
            if (fat_read_sectors(fs, sector, 1, dir_buf) != 0) return -1;
            for (uint32_t off = 0; off < fs->bpb.bytes_per_sector; off += 32) {
                uint8_t first = dir_buf[off];
                if (first == 0x00 || first == 0xE5) {
                    if (consecutive == 0) {
                        start_sector = sector;
                        start_off = off;
                        start_cluster = 0;
                    }
                    consecutive++;
                    if (consecutive >= total_entries) {
                        *out_sector = start_sector;
                        *out_byte_off = start_off;
                        *out_cluster = start_cluster;
                        return 0;
                    }
                } else {
                    consecutive = 0;
                }
            }
        }
        return -1; /* Root directory full for FAT12/16 */
    }

    uint32_t cluster = dir_cluster;
    uint32_t visited = 0;
    while (cluster < 0x0FFFFFF8) {
        if (++visited > fs->total_clusters) return -1;
        uint32_t sector_base = fat_cluster_to_sector(fs, cluster);
        if (fat_read_sectors(fs, sector_base, fs->bpb.sectors_per_cluster, dir_buf) != 0) return -1;

        for (uint32_t off = 0; off < cluster_size; off += 32) {
            uint8_t first = dir_buf[off];
            if (first == 0x00 || first == 0xE5) {
                if (consecutive == 0) {
                    start_sector = sector_base + off / fs->bpb.bytes_per_sector;
                    start_off = off % fs->bpb.bytes_per_sector;
                    start_cluster = cluster;
                }
                consecutive++;
                if (consecutive >= total_entries) {
                    *out_sector = start_sector;
                    *out_byte_off = start_off;
                    *out_cluster = start_cluster;
                    return 0;
                }
            } else {
                consecutive = 0;
            }
        }

        uint32_t next = fat_get_next_cluster(fs, cluster);
        if (next >= 0x0FFFFFF8) {
            /* Extend directory */
            uint32_t nc = fat_alloc_cluster(fs);
            if (nc == 0) return -1;
            fat_set_fat_entry(fs, cluster, nc);
            fat_set_fat_entry(fs, nc, fat_eoc(fs));
            /* Zero out new cluster */
            uint8_t zero_buf[32768];
            __builtin_memset(zero_buf, 0, cluster_size);
            fat_write_sectors(fs, fat_cluster_to_sector(fs, nc), fs->bpb.sectors_per_cluster, zero_buf);
            next = nc;
        }
        cluster = next;
    }
    return -1;
}

/* Write directory entries (LFN slots + short entry) to a directory.
 * The dir node's parent cluster is dir_cluster (0 for FAT12/16 root). */
static int fat_dir_add_entry(fat_fs_t *fs, uint32_t dir_cluster,
                              const char *name, uint8_t attr,
                              uint32_t first_cluster, uint32_t file_size) {
    int name_len = 0;
    while (name[name_len]) name_len++;

    int slots = fat_lfn_slots(name_len);
    int total_entries = slots + 1; /* LFN slots + short entry */

    char short_name[11];
    fat_make_short_name(name, short_name);
    uint8_t checksum = fat_lfn_checksum(short_name);

    uint32_t start_sector, start_byte_off, start_cluster_out;
    if (fat_find_dir_space(fs, dir_cluster, total_entries,
                           &start_sector, &start_byte_off, &start_cluster_out) != 0)
        return -1;

    /* Build LFN entries */
    static fat_lfn_t lfn_entries[20]; /* max ~260 chars / 13 per slot */
    if (slots > 20) return -1;
    fat_write_lfn_entries(lfn_entries, name, name_len, slots, checksum);

    /* Write entries sequentially */
    uint32_t bytes_per_sector = fs->bpb.bytes_per_sector;
    uint32_t sector = start_sector;
    uint32_t byte_off = start_byte_off;

    static uint8_t sector_buf[4096];

    for (int e = 0; e < total_entries; e++) {
        uint8_t entry_buf[32];
        if (e < slots) {
            __builtin_memcpy(entry_buf, &lfn_entries[e], 32);
        } else {
            /* Short (8.3) directory entry */
            fat_dirent_t *de = (fat_dirent_t *)entry_buf;
            __builtin_memset(de, 0, 32);
            __builtin_memcpy(de->name, short_name, 11);
            de->attr = attr;
            de->cluster_high = (uint16_t)(first_cluster >> 16);
            de->cluster_low  = (uint16_t)(first_cluster & 0xFFFF);
            de->file_size = file_size;
            /* Timestamps: use 0 (epoch) for simplicity */
        }

        /* Read sector, patch, write back */
        if (fat_read_sectors(fs, sector, 1, sector_buf) != 0) return -1;
        __builtin_memcpy(sector_buf + byte_off, entry_buf, 32);
        if (fat_write_sectors(fs, sector, 1, sector_buf) != 0) return -1;

        byte_off += 32;
        if (byte_off >= bytes_per_sector) {
            byte_off = 0;
            sector++;
        }
    }

    return 0;
}

/* Mark a directory entry as deleted (set first byte to 0xE5).
 * Searches for the short name in dir_cluster directory. */
static int fat_dir_remove_entry(fat_fs_t *fs, uint32_t dir_cluster, const char *name) {
    static uint8_t dir_buf[32768];
    char lfn_buf[256];
    int lfn_len = 0;
    uint32_t lfn_start_sector = 0, lfn_start_off = 0;
    int lfn_count = 0;

    /* Determine iteration parameters */
    int use_root_fixed = (dir_cluster == 0 && fs->fat_type != 32);
    uint32_t cluster = use_root_fixed ? 0 : dir_cluster;
    uint32_t visited = 0;

    int done = 0;
    while (!done) {
        uint32_t sector_start, nsectors;
        if (use_root_fixed) {
            sector_start = fs->root_dir_first_sector;
            nsectors = fs->root_dir_sectors;
            done = 1; /* Only one pass */
        } else {
            if (cluster >= 0x0FFFFFF8) break;
            if (++visited > fs->total_clusters) break;
            sector_start = fat_cluster_to_sector(fs, cluster);
            nsectors = fs->bpb.sectors_per_cluster;
        }

        for (uint32_t si = 0; si < nsectors; si++) {
            uint32_t sector = sector_start + si;
            if (fat_read_sectors(fs, sector, 1, dir_buf) != 0) return -1;
            for (uint32_t off = 0; off < fs->bpb.bytes_per_sector; off += 32) {
                fat_dirent_t *de = (fat_dirent_t *)(dir_buf + off);
                if (de->name[0] == 0x00) return -1; /* End of directory */
                if ((uint8_t)de->name[0] == 0xE5) {
                    lfn_len = 0; lfn_count = 0;
                    continue;
                }
                if (de->attr == FAT_ATTR_LFN) {
                    fat_lfn_t *lfn = (fat_lfn_t *)de;
                    if (lfn->order & 0x40) {
                        __builtin_memset(lfn_buf, 0, sizeof(lfn_buf));
                        lfn_len = 0; lfn_count = 0;
                        lfn_start_sector = sector;
                        lfn_start_off = off;
                    }
                    int ret = fat_parse_lfn(lfn, lfn_buf, (int)sizeof(lfn_buf) - 1);
                    if (ret > lfn_len) lfn_len = ret;
                    lfn_count++;
                    continue;
                }
                if (de->attr & FAT_ATTR_VOLUME_ID) { lfn_len = 0; continue; }

                char entry_name[128];
                if (lfn_len > 0) {
                    lfn_buf[lfn_len] = '\0';
                    __builtin_strncpy(entry_name, lfn_buf, 127);
                    entry_name[127] = '\0';
                } else {
                    fat_parse_short_name(de->name, entry_name);
                }

                if (fat_name_matches(entry_name, name)) {
                    /* Delete LFN entries */
                    if (lfn_len > 0) {
                        uint32_t ds = lfn_start_sector;
                        uint32_t doff = lfn_start_off;
                        for (int li = 0; li < lfn_count; li++) {
                            uint8_t sb[4096];
                            if (fat_read_sectors(fs, ds, 1, sb) == 0) {
                                sb[doff] = 0xE5;
                                fat_write_sectors(fs, ds, 1, sb);
                            }
                            doff += 32;
                            if (doff >= fs->bpb.bytes_per_sector) { doff = 0; ds++; }
                        }
                    }
                    /* Delete short entry */
                    dir_buf[off] = 0xE5;
                    fat_write_sectors(fs, sector, 1, dir_buf);
                    return 0;
                }
                lfn_len = 0; lfn_count = 0;
            }
        }

        if (!use_root_fixed)
            cluster = fat_get_next_cluster(fs, cluster);
    }
    return -1; /* Not found */
}

/* Create a file or directory entry */
static int fat_create_entry(fs_node_t *parent, const char *name, uint8_t attr) {
    fat_node_t *pctx = (fat_node_t *)(uintptr_t)parent->impl;
    fat_fs_t *fs = pctx->fs;
    uint32_t dir_cluster = pctx->first_cluster;

    /* Allocate a cluster for the new entry (directory always needs one) */
    uint32_t new_cluster = fat_alloc_cluster(fs);
    if (new_cluster == 0) return -1;

    if (attr & FAT_ATTR_DIRECTORY) {
        /* Zero out the new cluster and add . and .. entries */
        uint32_t cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;
        static uint8_t zero_buf[32768];
        __builtin_memset(zero_buf, 0, cluster_size);
        uint32_t new_sector = fat_cluster_to_sector(fs, new_cluster);
        fat_write_sectors(fs, new_sector, fs->bpb.sectors_per_cluster, zero_buf);

        /* Add . and .. entries */
        fat_dirent_t dot;
        __builtin_memset(&dot, 0, sizeof(dot));
        __builtin_memcpy(dot.name, ".          ", 11);
        dot.attr = FAT_ATTR_DIRECTORY;
        dot.cluster_high = (uint16_t)(new_cluster >> 16);
        dot.cluster_low  = (uint16_t)(new_cluster & 0xFFFF);

        fat_dirent_t dotdot;
        __builtin_memset(&dotdot, 0, sizeof(dotdot));
        __builtin_memcpy(dotdot.name, "..         ", 11);
        dotdot.attr = FAT_ATTR_DIRECTORY;
        dotdot.cluster_high = (uint16_t)(dir_cluster >> 16);
        dotdot.cluster_low  = (uint16_t)(dir_cluster & 0xFFFF);

        fat_read_sectors(fs, new_sector, 1, zero_buf);
        __builtin_memcpy(zero_buf, &dot, 32);
        __builtin_memcpy(zero_buf + 32, &dotdot, 32);
        fat_write_sectors(fs, new_sector, 1, zero_buf);
    }

    return fat_dir_add_entry(fs, dir_cluster, name, attr, new_cluster, 0);
}

/* mkdir VFS callback */
static int fat_mkdir_vfs(fs_node_t *parent, const char *name, uint16_t permission) {
    (void)permission;
    return fat_create_entry(parent, name, FAT_ATTR_DIRECTORY);
}

/* unlink VFS callback */
static int fat_unlink(fs_node_t *parent, const char *name) {
    fat_node_t *pctx = (fat_node_t *)(uintptr_t)parent->impl;
    fat_fs_t *fs = pctx->fs;

    /* Find the file to get its cluster */
    fs_node_t *child = fat_finddir(parent, (char *)name);
    if (!child) return -1;
    fat_node_t *cctx = (fat_node_t *)(uintptr_t)child->impl;
    uint32_t first_cluster = cctx->first_cluster;

    /* Remove dir entry */
    if (fat_dir_remove_entry(fs, pctx->first_cluster, name) != 0) return -1;

    /* Free cluster chain */
    if (first_cluster >= 2)
        fat_free_chain(fs, first_cluster);

    return 0;
}

/* rmdir VFS callback */
static int fat_rmdir_vfs(fs_node_t *parent, const char *name) {
    fat_node_t *pctx = (fat_node_t *)(uintptr_t)parent->impl;
    fat_fs_t *fs = pctx->fs;

    /* Check directory is empty (only . and .. entries) */
    fs_node_t *child = fat_finddir(parent, (char *)name);
    if (!child) return -1;
    fat_node_t *cctx = (fat_node_t *)(uintptr_t)child->impl;

    /* readdir index 0 should be ".", index 1 "..", index 2 should be NULL */
    struct dirent *de = fat_readdir(child, 2);
    if (de != NULL) return -1; /* Directory not empty (ENOTEMPTY) */

    uint32_t dir_cluster = cctx->first_cluster;
    if (fat_dir_remove_entry(fs, pctx->first_cluster, name) != 0) return -1;
    if (dir_cluster >= 2)
        fat_free_chain(fs, dir_cluster);

    return 0;
}

/* rename VFS callback */
static int fat_rename(fs_node_t *old_parent, const char *old_name,
                      fs_node_t *new_parent, const char *new_name) {
    fat_node_t *opctx = (fat_node_t *)(uintptr_t)old_parent->impl;
    fat_node_t *npctx = (fat_node_t *)(uintptr_t)new_parent->impl;
    fat_fs_t *fs = opctx->fs;

    /* Find source */
    fs_node_t *src = fat_finddir(old_parent, (char *)old_name);
    if (!src) return -1;
    fat_node_t *src_ctx = (fat_node_t *)(uintptr_t)src->impl;

    /* If destination exists, remove it */
    fs_node_t *dst = fat_finddir(new_parent, (char *)new_name);
    if (dst) {
        fat_node_t *dctx = (fat_node_t *)(uintptr_t)dst->impl;
        fat_dir_remove_entry(fs, npctx->first_cluster, new_name);
        /* If it was a file, free its clusters; if dir we don't free (caller should ensure empty) */
        if (!(dctx->attr & FAT_ATTR_DIRECTORY) && dctx->first_cluster >= 2)
            fat_free_chain(fs, dctx->first_cluster);
    }

    /* Add new entry */
    uint8_t attr = src_ctx->attr;
    uint32_t first_cluster = src_ctx->first_cluster;
    uint32_t size = src_ctx->size;

    if (fat_dir_add_entry(fs, npctx->first_cluster, new_name, attr, first_cluster, size) != 0)
        return -1;

    /* Remove old entry (but don't free cluster chain since we reuse it) */
    fat_dir_remove_entry(fs, opctx->first_cluster, old_name);

    return 0;
}

static filesystem_t fat_filesystem = {
    .name = "fat",
    .mount = fat_mount,
};

void fat_init(void) {
    kprint("Initializing FAT Driver...\n");
    vfs_register_filesystem(&fat_filesystem);
}
