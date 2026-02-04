/*
 * udf.c - Universal Disk Format (UDF) Filesystem Driver
 *
 * Read-write UDF implementation based on ECMA-167 and OSTA UDF 2.60.
 */

#include <fs/udf/udf.h>
#include <vfs/vfs.h>
#include <kern/console.h>
#include <string.h>
#include <sys/proc.h>

/* UDF filesystem context (single mount for now) */
struct udf_fs udf_ctx;

/* Forward declarations */
static fs_node_t *udf_mount(const char *device, uint32_t flags, void *data);

/*
 * Calculate tag checksum (sum of bytes 0-3 and 5-15)
 */
uint8_t udf_tag_checksum(struct udf_tag *tag) {
    uint8_t sum = 0;
    uint8_t *p = (uint8_t *)tag;
    for (int i = 0; i < 4; i++) sum += p[i];
    for (int i = 5; i < 16; i++) sum += p[i];
    return sum;
}

/*
 * CRC-CCITT (0x1021 polynomial) for descriptor CRC
 */
static uint16_t udf_crc_table[256];
static int udf_crc_initialized = 0;

static void udf_crc_init(void) {
    if (udf_crc_initialized) return;
    for (int i = 0; i < 256; i++) {
        uint16_t crc = 0;
        uint16_t c = (uint16_t)i << 8;
        for (int j = 0; j < 8; j++) {
            if ((crc ^ c) & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
            c = c << 1;
        }
        udf_crc_table[i] = crc;
    }
    udf_crc_initialized = 1;
}

uint16_t udf_crc(const uint8_t *data, uint32_t len) {
    udf_crc_init();
    uint16_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        crc = udf_crc_table[((crc >> 8) ^ data[i]) & 0xFF] ^ (crc << 8);
    }
    return crc;
}

/*
 * Read and verify a descriptor tag
 * Returns 0 on success, -1 on failure
 */
int udf_read_tag(fs_node_t *dev, uint32_t sector, struct udf_tag *tag, 
                 void *buffer, uint32_t size) {
    if (!dev || !dev->read) return -1;
    
    /* Read the sector */
    off_t offset = (off_t)sector * UDF_SECTOR_SIZE;
    uint32_t read = dev->read(dev, offset, size, (uint8_t *)buffer);
    if (read != size) return -1;
    
    /* Copy tag from buffer */
    memcpy(tag, buffer, sizeof(struct udf_tag));
    
    /* Verify tag checksum */
    if (udf_tag_checksum(tag) != tag->tag_checksum) {
        kprint("UDF: Tag checksum mismatch\n");
        return -1;
    }
    
    /* Verify tag location */
    if (tag->tag_location != sector) {
        kprint("UDF: Tag location mismatch\n");
        return -1;
    }
    
    /* Verify CRC if length > 0 */
    if (tag->desc_crc_len > 0) {
        uint8_t *data = (uint8_t *)buffer + sizeof(struct udf_tag);
        uint16_t crc = udf_crc(data, tag->desc_crc_len);
        if (crc != tag->desc_crc) {
            kprint("UDF: Descriptor CRC mismatch\n");
            return -1;
        }
    }
    
    return 0;
}

/*
 * Find the Anchor Volume Descriptor Pointer
 * Tries sector 256, then last sector, then last-256
 */
int udf_find_avdp(fs_node_t *dev, struct udf_avdp *avdp) {
    static uint8_t sector_buf[UDF_SECTOR_SIZE];
    struct udf_tag tag;
    
    /* Try sector 256 first (most common) */
    if (udf_read_tag(dev, UDF_AVDP_SECTOR, &tag, sector_buf, UDF_SECTOR_SIZE) == 0) {
        if (tag.tag_id == UDF_TAG_ANCHOR_VDP) {
            memcpy(avdp, sector_buf, sizeof(struct udf_avdp));
            return 0;
        }
    }
    
    /* Try last sector and last-256 for completeness */
    if (dev->length >= UDF_SECTOR_SIZE) {
        uint32_t last_sector = (uint32_t)((uint64_t)dev->length / UDF_SECTOR_SIZE) - 1;

        /* Try last sector */
        if (last_sector > UDF_AVDP_SECTOR) {
            if (udf_read_tag(dev, last_sector, &tag, sector_buf, UDF_SECTOR_SIZE) == 0) {
                if (tag.tag_id == UDF_TAG_ANCHOR_VDP) {
                    memcpy(avdp, sector_buf, sizeof(struct udf_avdp));
                    return 0;
                }
            }
        }

        /* Try last-256 sector */
        if (last_sector > UDF_AVDP_SECTOR + 256) {
            if (udf_read_tag(dev, last_sector - 256, &tag, sector_buf, UDF_SECTOR_SIZE) == 0) {
                if (tag.tag_id == UDF_TAG_ANCHOR_VDP) {
                    memcpy(avdp, sector_buf, sizeof(struct udf_avdp));
                    return 0;
                }
            }
        }
    }
    
    kprint("UDF: AVDP not found\n");
    return -1;
}

/*
 * Read Volume Descriptor Sequence
 * Parses PVD, PD, and LVD from the VDS extent
 */
int udf_read_vds(fs_node_t *dev, struct udf_extent_ad *vds_extent,
                 struct udf_pvd *pvd, struct udf_pd *pd, struct udf_lvd *lvd) {
    static uint8_t sector_buf[UDF_SECTOR_SIZE];
    struct udf_tag tag;
    
    uint32_t start = vds_extent->location;
    uint32_t count = vds_extent->length / UDF_SECTOR_SIZE;
    
    int found_pvd = 0, found_pd = 0, found_lvd = 0;
    
    for (uint32_t i = 0; i < count && !(found_pvd && found_pd && found_lvd); i++) {
        if (udf_read_tag(dev, start + i, &tag, sector_buf, UDF_SECTOR_SIZE) != 0) {
            continue;  /* Skip invalid sectors */
        }
        
        switch (tag.tag_id) {
        case UDF_TAG_PRIMARY_VD:
            memcpy(pvd, sector_buf, sizeof(struct udf_pvd));
            found_pvd = 1;
            break;
        case UDF_TAG_PARTITION_D:
            memcpy(pd, sector_buf, sizeof(struct udf_pd));
            found_pd = 1;
            break;
        case UDF_TAG_LOGICAL_VD:
            memcpy(lvd, sector_buf, sizeof(struct udf_lvd));
            found_lvd = 1;
            break;
        case UDF_TAG_TERMINATING:
            i = count;  /* Stop at terminating descriptor */
            break;
        }
    }
    
    if (!found_pvd || !found_pd || !found_lvd) {
        kprint("UDF: VDS incomplete\n");
        return -1;
    }
    
    return 0;
}

/*
 * Read File Set Descriptor from the location in LVD
 */
int udf_read_fsd(fs_node_t *dev, struct udf_fs *fs, struct udf_lvd *lvd, 
                 struct udf_fsd *fsd) {
    static uint8_t sector_buf[UDF_SECTOR_SIZE];
    struct udf_tag tag;
    
    /* FSD location is relative to partition start */
    uint32_t fsd_block = lvd->fsd_location.block;
    uint32_t fsd_sector = fs->partition_start + fsd_block;
    
    if (udf_read_tag(dev, fsd_sector, &tag, sector_buf, UDF_SECTOR_SIZE) != 0) {
        kprint("UDF: Failed to read FSD\n");
        return -1;
    }
    
    if (tag.tag_id != UDF_TAG_FSD) {
        kprint("UDF: Invalid FSD tag\n");
        return -1;
    }
    
    memcpy(fsd, sector_buf, sizeof(struct udf_fsd));
    return 0;
}

/*
 * Read File Entry (or Extended File Entry) from ICB location
 */
int udf_read_fe(struct udf_fs *fs, struct udf_long_ad *icb, struct udf_fe *fe) {
    static uint8_t sector_buf[UDF_SECTOR_SIZE];
    struct udf_tag tag;
    
    uint32_t sector = fs->partition_start + icb->block;
    
    if (udf_read_tag(fs->device, sector, &tag, sector_buf, UDF_SECTOR_SIZE) != 0) {
        return -1;
    }
    
    if (tag.tag_id != UDF_TAG_FE && tag.tag_id != UDF_TAG_EFE) {
        kprint("UDF: Invalid FE/EFE tag\n");
        return -1;
    }
    
    memcpy(fe, sector_buf, sizeof(struct udf_fe));
    return 0;
}

/*
 * Read file data via allocation descriptors
 * For now: only handles inline (embedded) data and short_ad
 */
uint32_t udf_read_file(struct udf_fs *fs, struct udf_fe *fe, 
                       uint32_t offset, uint32_t size, uint8_t *buffer) {
    static uint8_t sector_buf[UDF_SECTOR_SIZE];
    
    uint8_t ad_type = fe->icb_tag.flags & 0x7;
    uint8_t *alloc_area = ((uint8_t *)fe) + sizeof(struct udf_fe) + fe->ext_attr_length;
    
    if (ad_type == UDF_ICB_FLAG_AD_INLINE) {
        /* Inline data - directly in the allocation area */
        if (offset >= fe->info_length) return 0;
        if (offset + size > fe->info_length) size = fe->info_length - offset;
        memcpy(buffer, alloc_area + offset, size);
        return size;
    }
    
    if (ad_type == UDF_ICB_FLAG_AD_SHORT) {
        /* Short allocation descriptors */
        struct udf_short_ad *ads = (struct udf_short_ad *)alloc_area;
        uint32_t num_ads = fe->alloc_desc_length / sizeof(struct udf_short_ad);
        uint32_t total_read = 0;
        uint32_t file_pos = 0;
        
        for (uint32_t i = 0; i < num_ads && size > 0; i++) {
            uint32_t ext_len = ads[i].length & 0x3FFFFFFF;
            uint32_t ext_start = fs->partition_start + ads[i].position;
            
            if (offset >= file_pos + ext_len) {
                file_pos += ext_len;
                continue;
            }
            
            /* Read from this extent */
            uint32_t ext_off = (offset > file_pos) ? offset - file_pos : 0;
            uint32_t ext_read = ext_len - ext_off;
            if (ext_read > size) ext_read = size;
            
            /* Read sector by sector */
            while (ext_read > 0) {
                uint32_t sector = ext_start + (ext_off / UDF_SECTOR_SIZE);
                uint32_t sec_off = ext_off % UDF_SECTOR_SIZE;
                uint32_t to_read = UDF_SECTOR_SIZE - sec_off;
                if (to_read > ext_read) to_read = ext_read;
                
                fs->device->read(fs->device, (off_t)sector * UDF_SECTOR_SIZE, 
                                 UDF_SECTOR_SIZE, sector_buf);
                memcpy(buffer, sector_buf + sec_off, to_read);
                
                buffer += to_read;
                total_read += to_read;
                ext_off += to_read;
                ext_read -= to_read;
                size -= to_read;
                offset += to_read;
            }
            
            file_pos += ext_len;
        }
        return total_read;
    }
    
    /* Long ADs similar - not implemented yet */
    return 0;
}

/* UDF node context for VFS integration */
typedef struct {
    struct udf_fs *fs;
    struct udf_long_ad icb;
    struct udf_fe fe;
} udf_node_t;

#define UDF_NODE_CACHE_SIZE 64
static udf_node_t udf_node_cache[UDF_NODE_CACHE_SIZE];
static fs_node_t udf_fs_node_cache[UDF_NODE_CACHE_SIZE];
static int udf_node_cache_idx = 0;
static struct dirent udf_dirent;
static fs_node_t udf_root;
static udf_node_t udf_root_ctx;

/* Forward declarations for VFS operations */
/* Forward declarations for VFS operations */
static size_t udf_vfs_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer);
static struct dirent *udf_vfs_readdir(fs_node_t *node, uint64_t index);
static fs_node_t *udf_vfs_finddir(fs_node_t *node, char *name);
static int udf_vfs_mkdir(fs_node_t *parent, const char *name, uint16_t permission);

/* External functions from udf_write.c */
extern int udf_read_space_bitmap(fs_node_t *dev, uint32_t partition_start, uint32_t bitmap_loc, uint32_t bitmap_len);
extern uint32_t udf_alloc_block(void);
extern int udf_create_fe(fs_node_t *dev, uint32_t block, uint8_t file_type, uint32_t uid, uint32_t gid, uint32_t permissions);
extern int udf_add_fid(fs_node_t *dev, struct udf_fe *dir_fe, uint32_t dir_block, const char *name, struct udf_long_ad *icb, uint8_t characteristics);

extern struct process *current_process;

static fs_node_t *udf_alloc_node(struct udf_fs *fs, struct udf_long_ad *icb, struct udf_fe *fe) {
    int idx = udf_node_cache_idx++ % UDF_NODE_CACHE_SIZE;
    
    udf_node_t *ctx = &udf_node_cache[idx];
    fs_node_t *node = &udf_fs_node_cache[idx];
    
    ctx->fs = fs;
    memcpy(&ctx->icb, icb, sizeof(struct udf_long_ad));
    memcpy(&ctx->fe, fe, sizeof(struct udf_fe));
    
    memset(node, 0, sizeof(fs_node_t));
    node->length = (uint32_t)fe->info_length;
    node->uid = fe->uid;
    node->gid = fe->gid;
    node->impl = (uintptr_t)ctx;
    
    if (fe->icb_tag.file_type == UDF_FILETYPE_DIR) {
        node->flags = FS_DIRECTORY;
        node->readdir = udf_vfs_readdir;
        node->finddir = udf_vfs_finddir;
        node->mkdir = udf_vfs_mkdir;
    } else {
        node->flags = FS_FILE;
        node->read = udf_vfs_read;
    }
    
    return node;
}

static size_t udf_vfs_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    udf_node_t *ctx = (udf_node_t *)(uintptr_t)node->impl;
    return udf_read_file(ctx->fs, &ctx->fe, (uint32_t)offset, size, buffer);
}

/*
 * Iterate directory entries (FIDs)
 */
static struct dirent *udf_vfs_readdir(fs_node_t *node, uint64_t index) {
    udf_node_t *ctx = (udf_node_t *)(uintptr_t)node->impl;
    static uint8_t dir_buf[4096];
    
    /* Read directory data */
    uint32_t dir_size = (uint32_t)ctx->fe.info_length;
    udf_read_file(ctx->fs, &ctx->fe, 0, dir_size > 4096 ? 4096 : dir_size, dir_buf);
    
    /* Iterate FIDs */
    uint32_t pos = 0;
    uint32_t cur_idx = 0;
    
    while (pos < dir_size) {
        struct udf_fid *fid = (struct udf_fid *)(dir_buf + pos);
        
        /* Calculate FID size (38 + impl_use + file_id, padded to 4 bytes) */
        uint32_t fid_size = 38 + fid->impl_use_length + fid->file_id_length;
        fid_size = (fid_size + 3) & ~3;
        
        if (!(fid->characteristics & UDF_FID_DELETED)) {
            if (cur_idx == index) {
                if (fid->characteristics & UDF_FID_PARENT) {
                    strcpy(udf_dirent.name, "..");
                } else {
                    /* Extract filename (after impl_use) */
                    char *name = (char *)fid + 38 + fid->impl_use_length;
                    uint8_t len = fid->file_id_length;
                    /* Handle OSTA compressed unicode (type byte + chars) */
                    if (len > 0 && name[0] == 8) {
                        memcpy(udf_dirent.name, name + 1, len - 1);
                        udf_dirent.name[len - 1] = '\0';
                    } else {
                        memcpy(udf_dirent.name, name, len);
                        udf_dirent.name[len] = '\0';
                    }
                }
                return &udf_dirent;
            }
            cur_idx++;
        }
        
        pos += fid_size;
    }
    
    return NULL;
}

/*
 * Find entry by name in directory
 */
static fs_node_t *udf_vfs_finddir(fs_node_t *node, char *name) {
    udf_node_t *ctx = (udf_node_t *)(uintptr_t)node->impl;
    static uint8_t dir_buf[4096];
    
    uint32_t dir_size = (uint32_t)ctx->fe.info_length;
    udf_read_file(ctx->fs, &ctx->fe, 0, dir_size > 4096 ? 4096 : dir_size, dir_buf);
    
    uint32_t pos = 0;
    while (pos < dir_size) {
        struct udf_fid *fid = (struct udf_fid *)(dir_buf + pos);
        
        uint32_t fid_size = 38 + fid->impl_use_length + fid->file_id_length;
        fid_size = (fid_size + 3) & ~3;
        
        if (!(fid->characteristics & UDF_FID_DELETED)) {
            char fname[256];
            char *src = (char *)fid + 38 + fid->impl_use_length;
            uint8_t len = fid->file_id_length;
            
            if (len > 0 && src[0] == 8) {
                memcpy(fname, src + 1, len - 1);
                fname[len - 1] = '\0';
            } else {
                memcpy(fname, src, len);
                fname[len] = '\0';
            }
            
            if (strcmp(fname, name) == 0) {
                /* Found - read file entry */
                struct udf_fe fe;
                if (udf_read_fe(ctx->fs, &fid->icb, &fe) == 0) {
                    fs_node_t *result = udf_alloc_node(ctx->fs, &fid->icb, &fe);
                    strcpy(result->name, fname);
                    return result;
                }
            }
        }
        
        pos += fid_size;
    }
    
    return NULL;
}

static int udf_vfs_mkdir(fs_node_t *parent, const char *name, uint16_t permission) {
    if (!parent || !name) return -1;
    
    udf_node_t *pctx = (udf_node_t *)(uintptr_t)parent->impl;
    struct udf_fs *fs = pctx->fs;
    
    /* Allocate block for new directory */
    uint32_t block = udf_alloc_block();
    if (block == 0) return -1; // No space
    
    // Get current process credentials
    uint32_t uid = current_process ? current_process->uid : 0;
    uint32_t gid = current_process ? current_process->gid : 0;
    
    /* Create new directory FE */
    if (udf_create_fe(fs->device, block, UDF_FILETYPE_DIR, uid, gid, permission) != 0) {
        // TODO: Free block
        return -1;
    }
    
    /* Create ICB to point to new directory */
    struct udf_long_ad new_icb;
    new_icb.length = 2048; // Length of FE
    new_icb.block = block;
    new_icb.partition = pctx->icb.partition;
    memset(new_icb.impl_use, 0, 6);
    
    /* Add entry to parent directory */
    if (udf_add_fid(fs->device, &pctx->fe, pctx->icb.block, name, &new_icb, UDF_FID_DIRECTORY) != 0) {
        return -1;
    }
    
    /* Add . and .. to new directory */
    struct udf_fe new_fe;
    struct udf_long_ad fe_icb = new_icb;
    // Read the newly created FE so we can add to it
    if (udf_read_fe(fs, &fe_icb, &new_fe) != 0) return -1;
    
    // Add .. (parent)
    if (udf_add_fid(fs->device, &new_fe, block, "..", &pctx->icb, UDF_FID_DIRECTORY | UDF_FID_PARENT) != 0) {
        // Warning?
    }
    
    // Update parent node size
    parent->length = (uint32_t)pctx->fe.info_length;
    
    return 0;
}

/* VFS filesystem structure */
static filesystem_t udf_filesystem = {
    .name = "udf",
    .mount = udf_mount,
};

/*
 * Mount UDF filesystem
 */
static fs_node_t *udf_mount(const char *device, uint32_t flags, void *data) {
    (void)device; (void)flags;
    
    fs_node_t *dev = (fs_node_t *)data;
    if (!dev || !dev->read) {
        kprint("UDF: No device or read function\n");
        return NULL;
    }
    
    /* Find AVDP */
    struct udf_avdp avdp;
    if (udf_find_avdp(dev, &avdp) != 0) {
        return NULL;
    }
    
    /* Parse VDS */
    struct udf_pvd pvd;
    struct udf_pd pd;
    struct udf_lvd lvd;
    if (udf_read_vds(dev, &avdp.main_vds_extent, &pvd, &pd, &lvd) != 0) {
        return NULL;
    }
    
    /* Set up filesystem context */
    udf_ctx.device = dev;
    udf_ctx.sector_size = UDF_SECTOR_SIZE;
    udf_ctx.partition_start = pd.partition_start;
    udf_ctx.partition_length = pd.partition_length;
    udf_ctx.logical_block_size = lvd.logical_block_size;
    
    /* Initialize Space Bitmap */
    struct udf_partition_header_desc *phd = (struct udf_partition_header_desc *)pd.contents_use;
    if (phd->unalloc_space_bitmap.length > 0) {
        // Bitmap exists
        udf_read_space_bitmap(dev, pd.partition_start, 
                              phd->unalloc_space_bitmap.position, 
                              phd->unalloc_space_bitmap.length);
    } else {
        kprint("UDF: No space bitmap found (ReadOnly?)\n");
    }
    
    /* Read File Set Descriptor */
    struct udf_fsd fsd;
    if (udf_read_fsd(dev, &udf_ctx, &lvd, &fsd) != 0) {
        return NULL;
    }
    
    /* Save root ICB */
    memcpy(&udf_ctx.root_icb, &fsd.root_dir_icb, sizeof(struct udf_long_ad));
    
    /* Read root directory file entry */
    struct udf_fe root_fe;
    if (udf_read_fe(&udf_ctx, &udf_ctx.root_icb, &root_fe) != 0) {
        kprint("UDF: Failed to read root directory\n");
        return NULL;
    }
    
    /* Set up root node */
    udf_root_ctx.fs = &udf_ctx;
    memcpy(&udf_root_ctx.icb, &udf_ctx.root_icb, sizeof(struct udf_long_ad));
    memcpy(&udf_root_ctx.fe, &root_fe, sizeof(struct udf_fe));
    
    memset(&udf_root, 0, sizeof(fs_node_t));
    strcpy(udf_root.name, "/");
    udf_root.flags = FS_DIRECTORY;
    udf_root.length = (uint32_t)root_fe.info_length;
    udf_root.impl = (uintptr_t)&udf_root_ctx;
    udf_root.readdir = udf_vfs_readdir;
    udf_root.finddir = udf_vfs_finddir;
    udf_root.mkdir = udf_vfs_mkdir;
    
    kprint("UDF: Mounted successfully\n");
    return &udf_root;
}

void udf_init(void) {
    kprint("Initializing UDF Driver...\n");
    udf_crc_init();
    vfs_register_filesystem(&udf_filesystem);
}
