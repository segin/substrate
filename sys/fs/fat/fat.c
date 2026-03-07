#include <fs/fat/fat.h>
#include <kern/console.h>
#include <string.h>

static fat_fs_t fat_global_fs;
static fs_node_t fat_root_node;
static fat_node_t fat_root_ctx;



// Node cache for dynamically allocated nodes
static fat_node_t fat_node_cache[FAT_NODE_CACHE_SIZE];
static fs_node_t fat_fs_node_cache[FAT_NODE_CACHE_SIZE];
static int fat_node_cache_idx = 0;
static uint8_t fat_root_sector_buf[4096];

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
    off_t offset = (off_t)sector * fs->bpb.bytes_per_sector;
    size_t size = count * fs->bpb.bytes_per_sector;
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
    
    for (uint32_t i = 0; i < skip_clusters; i++) {
        cluster = fat_get_next_cluster(fs, cluster);
        if (cluster >= 0x0FFFFFFF) return 0;
    }
    
    // Read data
    static uint8_t cluster_buf[32768]; // Max cluster size
    while (size > 0 && cluster < 0x0FFFFFFF) {
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
    
    return total_read;
}

// Read directory entries
struct dirent *fat_readdir(fs_node_t *node, uint64_t index) {
    fat_node_t *ctx = (fat_node_t *)(uintptr_t)node->impl;
    fat_fs_t *fs = ctx->fs;
    
    static struct dirent dirent;
    static uint8_t dir_buf[32768];
    static char lfn_buffer[256];
    uint32_t bytes_per_sector = fs->bpb.bytes_per_sector;
    uint32_t cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;

    if (ctx->first_cluster == 0 && fs->fat_type != 32) {
        uint64_t current_idx = 0;
        int lfn_len = 0;

        for (uint32_t sector_i = 0; sector_i < fs->root_dir_sectors; sector_i++) {
            if (fat_read_root_sector(fs, sector_i, fat_root_sector_buf, sizeof(fat_root_sector_buf)) != 0) {
                return NULL;
            }

            for (uint32_t i = 0; i + sizeof(fat_dirent_t) <= bytes_per_sector; i += sizeof(fat_dirent_t)) {
                fat_dirent_t *entry = (fat_dirent_t *)(fat_root_sector_buf + i);

                if (entry->name[0] == 0x00) return NULL;
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
                        strncpy(dirent.d_name, lfn_buffer, 127);
                        dirent.d_name[127] = '\0';
                        lfn_len = 0;
                    } else {
                        fat_parse_short_name(entry->name, dirent.d_name);
                    }

                    uint32_t cluster_num = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
                    dirent.d_ino = cluster_num;
                    return &dirent;
                }

                current_idx++;
                lfn_len = 0;
            }
        }

        return NULL;
    }
    
    uint32_t cluster = ctx->first_cluster;
    if (cluster_size > sizeof(dir_buf)) return NULL;
    uint64_t current_idx = 0;
    int lfn_len = 0;
    
    while (cluster < 0x0FFFFFFF) {
        uint32_t sector = fat_cluster_to_sector(fs, cluster);
        if (fat_read_sectors(fs, sector, fs->bpb.sectors_per_cluster, dir_buf) != 0) {
            return NULL;
        }
        
        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dirent_t *entry = (fat_dirent_t *)(dir_buf + i);
            
            if (entry->name[0] == 0x00) return NULL;  // End of directory
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
                    strncpy(dirent.d_name, lfn_buffer, 127);
                    dirent.d_name[127] = '\0';
                    lfn_len = 0;
                } else {
                    fat_parse_short_name(entry->name, dirent.d_name);
                }
                
                uint32_t cluster_num = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
                dirent.d_ino = cluster_num;
                return &dirent;
            }
            
            current_idx++;
            lfn_len = 0;
        }
        
        cluster = fat_get_next_cluster(fs, cluster);
    }
    
    return NULL;
}

// Allocate a new node from cache
static fs_node_t *fat_alloc_node(fat_fs_t *fs, const char *name, uint32_t first_cluster, uint32_t size, uint8_t attr) {
    int idx = fat_node_cache_idx++ % FAT_NODE_CACHE_SIZE;
    
    fat_node_t *ctx = &fat_node_cache[idx];
    fs_node_t *node = &fat_fs_node_cache[idx];
    
    ctx->fs = fs;
    ctx->first_cluster = first_cluster;
    ctx->size = size;
    ctx->attr = attr;
    
    memset(node, 0, sizeof(fs_node_t));
    strncpy(node->name, name, 127);
    node->name[127] = '\0';
    node->impl = (uint32_t)(uintptr_t)ctx;
    node->length = size;
    node->mask = fat_default_mask(attr);
    node->uid = 0;
    node->gid = 0;
    
    if (attr & FAT_ATTR_DIRECTORY) {
        node->flags = FS_DIRECTORY;
        node->readdir = fat_readdir;
        node->finddir = fat_finddir;
    } else {
        node->flags = FS_FILE;
        node->read = fat_file_read;
    }
    
    return node;
}

// Find directory entry by name
fs_node_t *fat_finddir(fs_node_t *node, char *name) {
    fat_node_t *ctx = (fat_node_t *)(uintptr_t)node->impl;
    fat_fs_t *fs = ctx->fs;
    uint32_t bytes_per_sector = fs->bpb.bytes_per_sector;
    
    if (ctx->first_cluster == 0 && fs->fat_type != 32) {
        char lfn_buffer[256];
        int lfn_len = 0;

        for (uint32_t sector_i = 0; sector_i < fs->root_dir_sectors; sector_i++) {
            if (fat_read_root_sector(fs, sector_i, fat_root_sector_buf, sizeof(fat_root_sector_buf)) != 0) {
                return NULL;
            }

            for (uint32_t i = 0; i + sizeof(fat_dirent_t) <= bytes_per_sector; i += sizeof(fat_dirent_t)) {
                fat_dirent_t *entry = (fat_dirent_t *)(fat_root_sector_buf + i);

                if (entry->name[0] == 0x00) return NULL;
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

                if (strcmp(entry_name, name) == 0) {
                    uint32_t cluster_num = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
                    return fat_alloc_node(fs, entry_name, cluster_num, entry->file_size, entry->attr);
                }

                lfn_len = 0;
            }
        }

        return NULL;
    }
    
    uint32_t cluster = ctx->first_cluster;
    uint32_t cluster_size = fs->bpb.bytes_per_sector * fs->bpb.sectors_per_cluster;
    
    static uint8_t dir_buf[32768];
    static char lfn_buffer[256];
    int lfn_len = 0;
    
    while (cluster < 0x0FFFFFFF) {
        uint32_t sector = fat_cluster_to_sector(fs, cluster);
        if (fat_read_sectors(fs, sector, fs->bpb.sectors_per_cluster, dir_buf) != 0) {
            return NULL;
        }
        
        for (uint32_t i = 0; i < cluster_size; i += 32) {
            fat_dirent_t *entry = (fat_dirent_t *)(dir_buf + i);
            
            if (entry->name[0] == 0x00) return NULL;  // End of directory
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
            
            if (strcmp(entry_name, name) == 0) {
                // Found it - create and return node
                uint32_t cluster_num = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
                return fat_alloc_node(fs, entry_name, cluster_num, entry->file_size, entry->attr);
            }
            
            lfn_len = 0;
        }
        
        cluster = fat_get_next_cluster(fs, cluster);
    }
    
    return NULL;
}

// Mount FAT filesystem
fs_node_t *fat_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags;
    
    fs_node_t *dev = (fs_node_t *)data;
    if (!dev || !dev->read) {
        kprint("FAT: No device or read function\n");
        return NULL;
    }
    
    fat_fs_t *fs = &fat_global_fs;
    fs->device = dev;
    
    // Read boot sector
    if (fat_read_sectors(fs, 0, 1, &fs->bpb) != 0) {
        kprint("FAT: Failed to read boot sector\n");
        return NULL;
    }
    
    // Determine FAT type
    uint32_t root_dir_sectors = ((fs->bpb.root_entries * 32) + (fs->bpb.bytes_per_sector - 1)) / fs->bpb.bytes_per_sector;
    uint32_t fat_size = (fs->bpb.fat_size_16 != 0) ? fs->bpb.fat_size_16 : 0;
    
    // Read FAT32 extended BPB if needed
    if (fat_size == 0) {
        off_t ext_offset = sizeof(fat_bpb_t);
        dev->read(dev, ext_offset, sizeof(fat32_ext_bpb_t), (uint8_t *)&fs->ext_bpb);
        fat_size = fs->ext_bpb.fat_size_32;
    }
    
    uint32_t total_sectors = (fs->bpb.total_sectors_16 != 0) ? fs->bpb.total_sectors_16 : fs->bpb.total_sectors_32;
    uint32_t data_sectors = total_sectors - (fs->bpb.reserved_sectors + (fs->bpb.fat_count * fat_size) + root_dir_sectors);
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
    extern void *kmalloc(size_t size);
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
    
    // Setup root directory node
    fat_root_ctx.fs = fs;
    if (fs->fat_type == 32) {
        fat_root_ctx.first_cluster = fs->ext_bpb.root_cluster;
    } else {
        fat_root_ctx.first_cluster = 0;  // FAT12/16 use fixed root dir
    }
    fat_root_ctx.attr = FAT_ATTR_DIRECTORY;
    
    memset(&fat_root_node, 0, sizeof(fs_node_t));
    strcpy(fat_root_node.name, "/");
    fat_root_node.flags = FS_DIRECTORY;
    fat_root_node.mask = fat_default_mask(FAT_ATTR_DIRECTORY);
    fat_root_node.uid = 0;
    fat_root_node.gid = 0;
    fat_root_node.impl = (uint32_t)(uintptr_t)&fat_root_ctx;
    fat_root_node.readdir = fat_readdir;
    fat_root_node.finddir = fat_finddir;
    
    kprint("FAT: Mounted successfully (FAT");
    if (fs->fat_type == 12) kprint("12");
    else if (fs->fat_type == 16) kprint("16");
    else kprint("32");
    kprint(")\n");
    
    return &fat_root_node;
}

static filesystem_t fat_filesystem = {
    .name = "fat",
    .mount = fat_mount,
};

void fat_init(void) {
    kprint("Initializing FAT Driver...\n");
    vfs_register_filesystem(&fat_filesystem);
}
